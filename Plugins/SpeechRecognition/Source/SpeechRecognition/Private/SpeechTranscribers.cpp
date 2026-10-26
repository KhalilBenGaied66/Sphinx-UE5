#include "SpeechTranscribers.h"
#include "PhraseScorer.h"
#include "SpeechRecognition.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "UObject/StrongObjectPtr.h"

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
