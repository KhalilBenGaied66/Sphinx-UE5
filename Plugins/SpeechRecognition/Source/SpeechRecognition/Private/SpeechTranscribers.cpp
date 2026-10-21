#include "SpeechTranscribers.h"
#include "PhraseScorer.h"
#include "SpeechRecognition.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StrongObjectPtr.h"

#include <atomic>
#include <limits>

FString SpeechTranscribers::FindOnnxModel(const FString& Folder)
{
	for (const TCHAR* Name : { TEXT("model.int8.onnx"), TEXT("model.onnx") })
	{
		const FString Path = Folder / Name;
		if (IFileManager::Get().FileExists(*Path))
		{
			return Path;
		}
	}
	return FString();
}

// --- Omnilingual ASR through NNE --------------------------------------------

namespace
{
	class FOmnilingualTranscriber final : public ISpeechTranscriber
	{
	public:
		TSharedPtr<UE::NNE::IModelCPU> Model;
		TSharedPtr<UE::NNE::IModelInstanceCPU> Instance;
		TArray<FString> Vocabulary;
		// single characters to token ids, for phrase scoring
		TMap<TCHAR, int32> CharIds;
		FString ModelPath;

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("Omnilingual ASR CTC (%s), NNE ONNX Runtime on the CPU, phrase scoring and greedy CTC decoding"), *FPaths::GetCleanFilename(ModelPath));
		}

		/** Output frames of the wav2vec2 feature extractor for N input samples. */
		static int32 FramesFor(int32 Samples)
		{
			static const int32 Layers[][2] = { { 10, 5 }, { 3, 2 }, { 3, 2 }, { 3, 2 }, { 3, 2 }, { 2, 2 }, { 2, 2 } };
			int32 Length = Samples;
			for (const auto& Layer : Layers)
			{
				Length = (Length - Layer[0]) / Layer[1] + 1;
			}
			return Length;
		}

		virtual FSpeechTranscription Transcribe(TConstArrayView<float> Audio, TConstArrayView<FString> Phrases) override
		{
			FSpeechTranscription Out;

			// at least half a second, and normalised to zero mean and unit variance
			TArray<float> Input(Audio.GetData(), Audio.Num());
			if (Input.Num() < 8000)
			{
				Input.SetNumZeroed(8000);
			}
			double Mean = 0.0;
			for (const float S : Input)
			{
				Mean += S;
			}
			Mean /= Input.Num();
			double Var = 0.0;
			for (const float S : Input)
			{
				Var += (S - Mean) * (S - Mean);
			}
			const float Scale = 1.f / (float)(FMath::Sqrt(Var / Input.Num()) + 1e-7);
			for (float& S : Input)
			{
				S = (float)((S - Mean) * Scale);
			}

			const uint32 InputShape[] = { 1u, (uint32)Input.Num() };
			if (Instance->SetInputTensorShapes({ UE::NNE::FTensorShape::Make(InputShape) }) != UE::NNE::EResultStatus::Ok)
			{
				Out.Error = TEXT("the model refused the input shape");
				return Out;
			}

			const int32 Frames = FramesFor(Input.Num());
			const int32 Vocab = Vocabulary.Num();
			TArray<float> Logits;
			Logits.SetNumUninitialized(Frames * Vocab);

			UE::NNE::FTensorBindingCPU InputBinding{ Input.GetData(), (uint64)Input.Num() * sizeof(float) };
			UE::NNE::FTensorBindingCPU OutputBinding{ Logits.GetData(), (uint64)Logits.Num() * sizeof(float) };
			if (Instance->RunSync({ InputBinding }, { OutputBinding }) != UE::NNE::EResultStatus::Ok)
			{
				Out.Error = TEXT("the model failed to run");
				return Out;
			}

			// greedy CTC: best token per frame, repeats merged, blanks (token 0) dropped
			FString Text;
			int32 Previous = -1;
			for (int32 f = 0; f < Frames; ++f)
			{
				const float* Row = Logits.GetData() + (int64)f * Vocab;
				int32 Best = 0;
				for (int32 v = 1; v < Vocab; ++v)
				{
					if (Row[v] > Row[Best])
					{
						Best = v;
					}
				}
				if (Best != Previous && Best != 0 && Vocabulary.IsValidIndex(Best))
				{
					const FString& Token = Vocabulary[Best];
					if (!Token.StartsWith(TEXT("<")))
					{
						Text += Token;
					}
				}
				Previous = Best;
			}
			TArray<FString> Words;
			Text.ParseIntoArrayWS(Words);
			Out.Text = FString::Join(Words, TEXT(" "));

			if (Phrases.Num() > 0)
			{
				// each frame relative to its best token: the model's own path scores 0
				for (int32 f = 0; f < Frames; ++f)
				{
					float* Row = Logits.GetData() + (int64)f * Vocab;
					float Max = Row[0];
					for (int32 v = 1; v < Vocab; ++v)
					{
						Max = FMath::Max(Max, Row[v]);
					}
					for (int32 v = 0; v < Vocab; ++v)
					{
						Row[v] -= Max;
					}
				}
				TArray<int32> Ids;
				for (const FString& Phrase : Phrases)
				{
					float Best = -std::numeric_limits<float>::infinity();
					for (const FString& Spelling : FPhraseScorer::Variants(Phrase))
					{
						Ids.Reset();
						for (const TCHAR C : Spelling)
						{
							if (const int32* Id = CharIds.Find(C))
							{
								Ids.Add(*Id);
							}
						}
						Best = FMath::Max(Best, FPhraseScorer::AlignScore(Logits.GetData(), Frames, Vocab, Ids));
					}
					Out.PhraseScores.Add(Phrase, Best);
				}
			}
			return Out;
		}
	};
}

FSpeechTranscriberPtr SpeechTranscribers::CreateOmnilingual(const FString& Folder, FString& OutError)
{
	check(IsInGameThread());

	const FString ModelPath = FindOnnxModel(Folder);
	const FString TokensPath = Folder / TEXT("tokens.txt");
	if (ModelPath.IsEmpty() || !IFileManager::Get().FileExists(*TokensPath))
	{
		OutError = FString::Printf(TEXT("No Omnilingual model in %s: it needs model.int8.onnx (or model.onnx) and tokens.txt. Tools\\Get-Models.ps1 -Language omnilingual downloads them."), *Folder);
		return nullptr;
	}

	// "<token> <id>" per line; the space token is written as a space
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *TokensPath);
	TMap<int32, FString> ById;
	int32 MaxId = -1;
	for (const FString& Line : Lines)
	{
		int32 Split = INDEX_NONE;
		if (!Line.FindLastChar(TEXT(' '), Split) || Split == Line.Len() - 1)
		{
			continue;
		}
		const FString Id = Line.Mid(Split + 1);
		if (!Id.IsNumeric())
		{
			continue;
		}
		const int32 Index = FCString::Atoi(*Id);
		ById.Add(Index, Line.Left(Split));
		MaxId = FMath::Max(MaxId, Index);
	}
	if (MaxId < 1)
	{
		OutError = FString::Printf(TEXT("%s holds no tokens."), *TokensPath);
		return nullptr;
	}

	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));
	if (!Runtime.IsValid())
	{
		OutError = TEXT("The NNERuntimeORT plugin is not loaded: enable it (Edit > Plugins > NNERuntimeORT).");
		return nullptr;
	}

	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *ModelPath))
	{
		OutError = FString::Printf(TEXT("Could not read %s."), *ModelPath);
		return nullptr;
	}

	TStrongObjectPtr<UNNEModelData> ModelData(NewObject<UNNEModelData>());
	ModelData->Init(TEXT("onnx"), Bytes);

	TSharedPtr<FOmnilingualTranscriber, ESPMode::ThreadSafe> Transcriber = MakeShared<FOmnilingualTranscriber, ESPMode::ThreadSafe>();
	Transcriber->ModelPath = ModelPath;
	Transcriber->Model = Runtime->CreateModelCPU(ModelData.Get());
	if (!Transcriber->Model.IsValid())
	{
		OutError = FString::Printf(TEXT("NNE could not create a model from %s."), *ModelPath);
		return nullptr;
	}
	Transcriber->Instance = Transcriber->Model->CreateModelInstanceCPU();
	if (!Transcriber->Instance.IsValid())
	{
		OutError = TEXT("NNE could not create a model instance.");
		return nullptr;
	}

	Transcriber->Vocabulary.SetNum(MaxId + 1);
	for (const TPair<int32, FString>& Pair : ById)
	{
		Transcriber->Vocabulary[Pair.Key] = Pair.Value;
		if (Pair.Value.Len() == 1)
		{
			Transcriber->CharIds.Add(Pair.Value[0], Pair.Key);
		}
	}
	return Transcriber;
}

// --- Gemini, online, for development ----------------------------------------

namespace
{
	FString LanguageNameForPrompt(const FString& InCode)
	{
		const FString Code = InCode.ToLower();
		if (Code == TEXT("aeb") || Code == TEXT("ar-tn"))
		{
			return TEXT("Tunisian Derja (Tunisian Arabic), possibly with French loanwords");
		}
		if (Code == TEXT("ary") || Code == TEXT("ar-ma"))
		{
			return TEXT("Moroccan Darija (Moroccan Arabic), possibly with French loanwords");
		}
		if (Code == TEXT("arq") || Code == TEXT("ar-dz"))
		{
			return TEXT("Algerian Arabic, possibly with French loanwords");
		}
		if (Code == TEXT("arz") || Code == TEXT("ar-eg"))
		{
			return TEXT("Egyptian Arabic");
		}
		if (Code == TEXT("apc") || Code == TEXT("ajp") || Code == TEXT("ar-lb") || Code == TEXT("ar-sy") || Code == TEXT("ar-jo"))
		{
			return TEXT("Levantine Arabic");
		}
		if (Code == TEXT("ar") || Code == TEXT("ara") || Code == TEXT("arb"))
		{
			return TEXT("Arabic (Modern Standard Arabic or a dialect)");
		}
		if (Code == TEXT("any"))
		{
			return TEXT("any language, or invented words such as the names of spells; write invented words in Latin letters, as they sound");
		}
		return FString::Printf(TEXT("the language with the ISO 639 code \"%s\""), *InCode);
	}

	TArray<uint8> MakeWav(TConstArrayView<float> Audio)
	{
		const uint32 DataBytes = (uint32)Audio.Num() * 2;
		TArray<uint8> Wav;
		Wav.Reserve(44 + DataBytes);
		auto Put32 = [&Wav](uint32 V) { for (int32 i = 0; i < 4; ++i) { Wav.Add((uint8)(V >> (8 * i))); } };
		auto Put16 = [&Wav](uint16 V) { Wav.Add((uint8)V); Wav.Add((uint8)(V >> 8)); };
		auto PutTag = [&Wav](const char* Tag) { for (int32 i = 0; i < 4; ++i) { Wav.Add((uint8)Tag[i]); } };
		PutTag("RIFF"); Put32(36 + DataBytes); PutTag("WAVE");
		PutTag("fmt "); Put32(16); Put16(1); Put16(1); Put32(16000); Put32(32000); Put16(2); Put16(16);
		PutTag("data"); Put32(DataBytes);
		for (const float S : Audio)
		{
			Put16((uint16)(int16)FMath::Clamp(S * 32767.f, -32768.f, 32767.f));
		}
		return Wav;
	}

	class FGeminiTranscriber final : public ISpeechTranscriber
	{
	public:
		FString Key;
		FString Model;
		FString Prompt;

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("Gemini API (%s), online, development only"), *Model);
		}

		virtual FSpeechTranscription Transcribe(TConstArrayView<float> Audio, TConstArrayView<FString> /*Phrases: text only*/) override
		{
			FSpeechTranscription Out;

			TSharedRef<FJsonObject> AudioPart = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Inline = MakeShared<FJsonObject>();
			Inline->SetStringField(TEXT("mime_type"), TEXT("audio/wav"));
			Inline->SetStringField(TEXT("data"), FBase64::Encode(MakeWav(Audio)));
			AudioPart->SetObjectField(TEXT("inline_data"), Inline);
			TSharedRef<FJsonObject> TextPart = MakeShared<FJsonObject>();
			TextPart->SetStringField(TEXT("text"), Prompt);
			TSharedRef<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetArrayField(TEXT("parts"), { MakeShared<FJsonValueObject>(AudioPart), MakeShared<FJsonValueObject>(TextPart) });
			TSharedRef<FJsonObject> Generation = MakeShared<FJsonObject>();
			Generation->SetNumberField(TEXT("temperature"), 0.0);
			Generation->SetNumberField(TEXT("maxOutputTokens"), 128);
			if (Model.StartsWith(TEXT("gemini-3")))
			{
				// thinking only adds latency to a transcription
				TSharedRef<FJsonObject> Thinking = MakeShared<FJsonObject>();
				Thinking->SetStringField(TEXT("thinkingLevel"), TEXT("low"));
				Generation->SetObjectField(TEXT("thinkingConfig"), Thinking);
			}
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetArrayField(TEXT("contents"), { MakeShared<FJsonValueObject>(Content) });
			Body->SetObjectField(TEXT("generationConfig"), Generation);
			FString BodyText;
			FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&BodyText));

			struct FState
			{
				std::atomic<bool> bDone{ false };
				int32 Code = 0;
				FString Response;
				FString Failure;
			};
			TSharedRef<FState, ESPMode::ThreadSafe> State = MakeShared<FState, ESPMode::ThreadSafe>();

			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
			Request->SetURL(FString::Printf(TEXT("https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent"), *Model));
			Request->SetVerb(TEXT("POST"));
			Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			Request->SetHeader(TEXT("x-goog-api-key"), Key);
			Request->SetContentAsString(BodyText);
			Request->SetTimeout(20.f);
			Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
			Request->OnProcessRequestComplete().BindLambda([State](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
			{
				if (bConnected && Response.IsValid())
				{
					State->Code = Response->GetResponseCode();
					State->Response = Response->GetContentAsString();
				}
				else
				{
					State->Failure = TEXT("no response (network down, or the request timed out)");
				}
				State->bDone.store(true);
			});
			Request->ProcessRequest();

			const double Deadline = FPlatformTime::Seconds() + 25.0;
			while (!State->bDone.load() && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::Sleep(0.01f);
			}
			if (!State->bDone.load())
			{
				Request->CancelRequest();
				Out.Error = TEXT("Gemini did not answer within 25 s");
				return Out;
			}
			if (!State->Failure.IsEmpty())
			{
				Out.Error = State->Failure;
				return Out;
			}

			TSharedPtr<FJsonObject> Json;
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(State->Response), Json);
			if (State->Code != 200 || !Json.IsValid())
			{
				FString Message = State->Response.Left(300);
				const TSharedPtr<FJsonObject>* Error = nullptr;
				if (Json.IsValid() && Json->TryGetObjectField(TEXT("error"), Error))
				{
					Message = (*Error)->GetStringField(TEXT("message")).Left(300);
				}
				Out.Error = FString::Printf(TEXT("Gemini answered HTTP %d: %s"), State->Code, *Message);
				return Out;
			}

			const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
			if (Json->TryGetArrayField(TEXT("candidates"), Candidates) && Candidates->Num() > 0)
			{
				const TSharedPtr<FJsonObject>* ContentObject = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if ((*Candidates)[0]->AsObject()->TryGetObjectField(TEXT("content"), ContentObject)
					&& (*ContentObject)->TryGetArrayField(TEXT("parts"), Parts))
				{
					for (const TSharedPtr<FJsonValue>& Part : *Parts)
					{
						FString Piece;
						if (Part->AsObject()->TryGetStringField(TEXT("text"), Piece))
						{
							Out.Text += Piece;
						}
					}
				}
			}
			Out.Text.TrimStartAndEndInline();
			return Out;
		}
	};
}

FSpeechTranscriberPtr SpeechTranscribers::CreateGemini(const FString& LanguageCode, const FString& Model, FString& OutError)
{
	const FString Key = FPlatformMisc::GetEnvironmentVariable(TEXT("GEMINI_API_KEY")).TrimStartAndEnd();
	if (Key.IsEmpty())
	{
		OutError = TEXT("The Gemini transcriber needs the GEMINI_API_KEY environment variable. Set it for the editor's process; never put the key in a config file that ships.");
		return nullptr;
	}

	TSharedPtr<FGeminiTranscriber, ESPMode::ThreadSafe> Transcriber = MakeShared<FGeminiTranscriber, ESPMode::ThreadSafe>();
	Transcriber->Key = Key;
	Transcriber->Model = Model.IsEmpty() ? FString(TEXT("gemini-3.6-flash")) : Model;
	Transcriber->Prompt = FString::Printf(
		TEXT("This audio is one short voice command spoken to a video game in %s. Transcribe exactly what is said; ")
		TEXT("for Arabic speech use Arabic script and write French loanwords in Arabic letters too. Do not translate and ")
		TEXT("do not correct to Modern Standard Arabic. If nothing intelligible is said, output nothing. Output the transcript only."),
		*LanguageNameForPrompt(LanguageCode));
	return Transcriber;
}
