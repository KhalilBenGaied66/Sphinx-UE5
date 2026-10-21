#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SpeechRecognitionSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "SpeechRecognitionSettings.h"
#include "SpeechTestSupport.h"
#include "SpeechTranscribers.h"
#include "TranscribingDecoder.h"
#include "TranscriptMatcher.h"
#include "PhraseScorer.h"
#include "VoiceCommandComponent.h"

#include <atomic>

// Languages without a PocketSphinx model (Tunisian Darija): the transcribing path.
//
// Unit.TranscriptMatcher*      need nothing but the cases in Resources/TestAudio/darija,
//                              written by Tools/DarijaEval/export_cpp_cases.py.
// Unit.TranscribingDecoder     needs nothing: a stand-in engine.
// Subsystem.Darija             needs the aeb and en models and a recording device: Darija,
//                              English, Darija again on the live microphone, and grammar
//                              mode refused for Darija. Opens the device for a few seconds.
// Transcriber.Omnilingual      needs the aeb model (Tools/Get-Models.ps1 -Language aeb)
//                              and Darija clips under Tools/DarijaEval/recordings; skips
//                              without them. Writes what it heard to
//                              Saved/SpeechRecognition/darija-nne.tsv for score.py.

namespace
{
	const EAutomationTestFlags TranscriptionTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FString DarijaCasesDir()
	{
		return SpeechTest::GetAudioRoot() / TEXT("darija");
	}

	/** The transcript with its Arabic letters blanked out: what the Latin comparison sees. */
	FString LatinPart(const FString& Text)
	{
		FString Out = Text;
		for (TCHAR& C : Out)
		{
			if (C >= 0x0600 && C <= 0x06FF)
			{
				C = TEXT(' ');
			}
		}
		return Out;
	}

	bool LoadGroups(TArray<FString>& OutIds, TArray<TArray<FString>>& OutGroups)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *(DarijaCasesDir() / TEXT("matcher_groups.tsv"))))
		{
			return false;
		}
		for (const FString& Line : Lines)
		{
			FString Id, Phrase;
			if (!Line.Split(TEXT("\t"), &Id, &Phrase))
			{
				continue;
			}
			int32 Index = OutIds.Find(Id);
			if (Index == INDEX_NONE)
			{
				Index = OutIds.Add(Id);
				OutGroups.AddDefaulted();
			}
			OutGroups[Index].Add(Phrase);
		}
		return OutIds.Num() > 0;
	}

	/** Returns a fixed text and counts its calls. */
	class FStandInTranscriber final : public ISpeechTranscriber
	{
	public:
		std::atomic<int32> Calls{ 0 };
		float LastSeconds = 0.f;
		virtual FSpeechTranscription Transcribe(TConstArrayView<float> Audio, TConstArrayView<FString> Phrases) override
		{
			++Calls;
			LastSeconds = (float)Audio.Num() / 16000.f;
			FSpeechTranscription Out;
			Out.Text = TEXT("  قدّام  ");
			return Out;
		}
		virtual FString Describe() const override { return TEXT("stand-in"); }
	};

	void AppendSilence(TArray<int16>& Samples, float Seconds)
	{
		// faint noise rather than digital zero, like a real microphone
		FRandomStream Random(7);
		const int32 Count = (int32)(Seconds * 16000.f);
		for (int32 i = 0; i < Count; ++i)
		{
			Samples.Add((int16)Random.RandRange(-20, 20));
		}
	}

	void AppendVoice(TArray<int16>& Samples, float Seconds)
	{
		// a 220 Hz tone with harmonics at about -14 dBFS: loud enough to be speech
		const int32 Count = (int32)(Seconds * 16000.f);
		for (int32 i = 0; i < Count; ++i)
		{
			const float T = (float)i / 16000.f;
			const float V = 0.15f * FMath::Sin(2.f * PI * 220.f * T) + 0.08f * FMath::Sin(2.f * PI * 440.f * T) + 0.05f * FMath::Sin(2.f * PI * 880.f * T);
			Samples.Add((int16)(V * 32767.f));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTranscriptMatcherTest, "SpeechRecognition.Unit.TranscriptMatcher", TranscriptionTestFlags)
bool FTranscriptMatcherTest::RunTest(const FString& Parameters)
{
	const TArray<TArray<FString>> Groups = {
		{ TEXT("على ليسار"), TEXT("3al lisar"), TEXT("3la lisar") },
		{ TEXT("لا"), TEXT("la") },
		{ TEXT("حلّ الباب"), TEXT("7ell el beb") },
		{ TEXT("سكّر الباب"), TEXT("sakker el beb") },
	};

	TestEqual(TEXT("harakat, hamza and doubled letters"), FTranscriptMatcher::NormaliseArabic(TEXT("إمشي لِقدّام")), FString(TEXT("امشي لقدام")));
	TestEqual(TEXT("Arabizi digits"), FTranscriptMatcher::NormaliseLatin(TEXT("7ell el BEB")), FString(TEXT("hal al bab")));
	TestEqual(TEXT("an exact phrase"), FTranscriptMatcher::Match(TEXT("حل الباب"), Groups).Group, 2);
	TestEqual(TEXT("a command inside a longer one is not a rival"), FTranscriptMatcher::Match(TEXT("3la lisar"), Groups).Group, 0);
	TestEqual(TEXT("a short phrase needs an exact word"), FTranscriptMatcher::Match(TEXT("lal"), Groups).Group, (int32)INDEX_NONE);
	TestEqual(TEXT("one short word on its own"), FTranscriptMatcher::Match(TEXT("لا"), Groups).Group, 1);
	TestEqual(TEXT("chat matches nothing"), FTranscriptMatcher::Match(TEXT("نحب نشرب قهوة"), Groups).Group, (int32)INDEX_NONE);
	TestEqual(TEXT("empty"), FTranscriptMatcher::Match(TEXT(""), Groups).Group, (int32)INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTranscriptMatcherParityTest, "SpeechRecognition.Unit.TranscriptMatcherParity", TranscriptionTestFlags)
bool FTranscriptMatcherParityTest::RunTest(const FString& Parameters)
{
	TArray<FString> Ids;
	TArray<TArray<FString>> Groups;
	TArray<FString> Lines;
	if (!LoadGroups(Ids, Groups) || !FFileHelper::LoadFileToStringArray(Lines, *(DarijaCasesDir() / TEXT("matcher_cases.tsv"))))
	{
		AddError(FString::Printf(TEXT("No matcher cases in %s: run Tools/DarijaEval/export_cpp_cases.py"), *DarijaCasesDir()));
		return false;
	}

	int32 Cases = 0, Commands = 0, Differences = 0;
	for (const FString& Line : Lines)
	{
		TArray<FString> Fields;
		Line.ParseIntoArray(Fields, TEXT("\t"), false);
		if (Fields.Num() != 5)
		{
			continue;
		}
		++Cases;
		const FString& Text = Fields[0];
		const FString Arabic = FTranscriptMatcher::NormaliseArabic(Text);
		const FString Latin = FTranscriptMatcher::NormaliseLatin(LatinPart(Text));
		const FTranscriptMatcher::FResult Match = FTranscriptMatcher::Match(Text, Groups);
		const FString Command = Ids.IsValidIndex(Match.Group) ? Ids[Match.Group] : FString(TEXT("-"));
		Commands += Command != TEXT("-");

		const FString Skeleton = FTranscriptMatcher::Skeleton(Text);
		if (Arabic != Fields[1] || Latin != Fields[2] || Command != Fields[3] || Skeleton != Fields[4])
		{
			// a few lines of detail, then only the count
			if (++Differences <= 10)
			{
				AddError(FString::Printf(TEXT("\"%s\": C++ [%s | %s | %s | %s], Python [%s | %s | %s | %s] (score %.1f)"),
					*Text, *Arabic, *Latin, *Command, *Skeleton, *Fields[1], *Fields[2], *Fields[3], *Fields[4], Match.Score));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d cases, %d mapped to a command, %d differences from commands.py"), Cases, Commands, Differences));
	TestTrue(TEXT("cases were read"), Cases > 100);
	return Differences == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPhraseScorerTest, "SpeechRecognition.Unit.PhraseScorer", TranscriptionTestFlags)
bool FPhraseScorerTest::RunTest(const FString& Parameters)
{
	// the spellings tried for each phrase, against phrase_scoring.py
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *(DarijaCasesDir() / TEXT("phrase_variants.tsv"))))
	{
		AddError(TEXT("No phrase_variants.tsv: run Tools/DarijaEval/export_cpp_cases.py"));
		return false;
	}
	int32 Phrases = 0, Differences = 0;
	for (const FString& Line : Lines)
	{
		FString Phrase, Expected;
		if (!Line.Split(TEXT("\t"), &Phrase, &Expected))
		{
			continue;
		}
		++Phrases;
		const FString Got = FString::Join(FPhraseScorer::Variants(Phrase), TEXT(" | "));
		if (Got != Expected && ++Differences <= 10)
		{
			AddError(FString::Printf(TEXT("\"%s\": C++ [%s], Python [%s]"), *Phrase, *Got, *Expected));
		}
	}
	AddInfo(FString::Printf(TEXT("%d phrases, %d differences from phrase_scoring.py"), Phrases, Differences));

	// Alignment on a hand-made output: blank (0), a (1), b (2); five frames saying "a a _ b _"
	const int32 Vocab = 3;
	const float Relative[] = {
		-5.f, 0.f, -5.f,
		-5.f, 0.f, -5.f,
		0.f, -5.f, -5.f,
		-5.f, -5.f, 0.f,
		0.f, -5.f, -5.f,
	};
	const int32 AB[] = { 1, 2 };
	const int32 BA[] = { 2, 1 };
	const int32 A[] = { 1 };
	TestEqual(TEXT("the model's own path scores 0"), FPhraseScorer::AlignScore(Relative, 5, Vocab, AB), 0.f);
	TestTrue(TEXT("the wrong order scores lower"), FPhraseScorer::AlignScore(Relative, 5, Vocab, BA) < -2.f);
	// "a" alone must explain the b frame too: 5 nats for one character
	TestEqual(TEXT("a phrase missing a letter pays for it"), FPhraseScorer::AlignScore(Relative, 5, Vocab, A), -5.f);
	return Differences == 0 && Phrases > 50;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTranscribingDecoderTest, "SpeechRecognition.Unit.TranscribingDecoder", TranscriptionTestFlags)
bool FTranscribingDecoderTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FStandInTranscriber, ESPMode::ThreadSafe> Engine = MakeShared<FStandInTranscriber, ESPMode::ThreadSafe>();
	FSphinxDecoderConfig Config;
	Config.Transcriber = Engine;

	FTranscribingDecoder Decoder;
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;
	TestTrue(TEXT("initialises with a transcriber"), Decoder.Initialize(Config, Error, Message));

	// silence, a 1 s "word", silence, a 120 ms click, silence
	TArray<int16> Audio;
	AppendSilence(Audio, 1.f);
	AppendVoice(Audio, 1.f);
	AppendSilence(Audio, 1.f);
	AppendVoice(Audio, 0.12f);
	AppendSilence(Audio, 1.f);

	TArray<FString> Events;
	TArray<FRecognisedPhraseDetail> Heard;
	auto OnEvent = [&](ESphinxDecoderEvent Event, const TArray<FRecognisedPhraseDetail>& Phrases)
	{
		switch (Event)
		{
		case ESphinxDecoderEvent::SpeechStarted: Events.Add(TEXT("start")); break;
		case ESphinxDecoderEvent::Recognised: Events.Add(TEXT("recognised")); Heard.Append(Phrases); break;
		case ESphinxDecoderEvent::Unrecognised: Events.Add(TEXT("unrecognised")); break;
		default: Events.Add(TEXT("end")); break;
		}
	};
	for (int32 Offset = 0; Offset < Audio.Num(); Offset += 1024)
	{
		Decoder.ProcessAudio(Audio.GetData() + Offset, FMath::Min(1024, Audio.Num() - Offset), OnEvent);
	}
	Decoder.EndStream(OnEvent);

	AddInfo(FString::Join(Events, TEXT(", ")));
	// A start of speech is reported as it happens. The audio here is fed faster
	// than real time, so the click may start before the word's text is back;
	// what must hold is that results come in the order the utterances were said.
	TArray<FString> Results = Events.FilterByPredicate([](const FString& E) { return E != TEXT("start"); });
	TestEqual(TEXT("two starts of speech"), Events.Num() - Results.Num(), 2);
	TestEqual(TEXT("results in order"), FString::Join(Results, TEXT(",")), FString(TEXT("recognised,end,unrecognised,end")));
	TestEqual(TEXT("the click is not sent to the engine"), Engine->Calls.load(), 1);
	if (Heard.Num() == 1)
	{
		TestTrue(TEXT("marked as a transcript"), Heard[0].bIsTranscript);
		TestEqual(TEXT("trimmed text"), Heard[0].Phrase, FString(TEXT("قدّام")));
		// the word, the pre-roll before it and the silence that ended it
		TestTrue(TEXT("utterance length"), Engine->LastSeconds > 1.f && Engine->LastSeconds < 2.f);
	}
	Decoder.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOmnilingualTranscriberTest, "SpeechRecognition.Transcriber.Omnilingual", TranscriptionTestFlags)
bool FOmnilingualTranscriberTest::RunTest(const FString& Parameters)
{
	const FString ModelDir = USpeechRecognitionSettings::GetTranscriptionModelDir(TEXT("aeb"));
	if (!USpeechRecognitionSettings::HasTranscriptionModel(TEXT("aeb")))
	{
		AddInfo(FString::Printf(TEXT("Skipped: no Omnilingual model in %s (Tools/Get-Models.ps1 -Language omnilingual)"), *ModelDir));
		return true;
	}

	// every Darija clip the measurement tools know of: placeholders and recordings
	const FString EvalDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/DarijaEval"));
	TArray<FString> Manifests;
	IFileManager::Get().FindFilesRecursive(Manifests, *(EvalDir / TEXT("recordings")), TEXT("manifest.csv"), true, false);
	if (Manifests.Num() == 0)
	{
		AddInfo(FString::Printf(TEXT("Skipped: no manifest.csv under %s/recordings (record.py, placeholder.py)"), *EvalDir));
		return true;
	}

	FString CreateError;
	const double LoadStart = FPlatformTime::Seconds();
	FSpeechTranscriberPtr Engine = SpeechTranscribers::CreateOmnilingual(ModelDir, CreateError);
	if (!Engine.IsValid())
	{
		AddError(CreateError);
		return false;
	}
	AddInfo(FString::Printf(TEXT("%s, loaded in %.1f s"), *Engine->Describe(), FPlatformTime::Seconds() - LoadStart));

	// the commands of commands.toml, as a voice command component would hold them
	TArray<FString> Ids;
	TArray<TArray<FString>> Groups;
	LoadGroups(Ids, Groups);
	UVoiceCommandComponent* Component = NewObject<UVoiceCommandComponent>(GetTransientPackage());
	TArray<FString> AllPhrases;
	for (int32 g = 0; g < Ids.Num(); ++g)
	{
		FVoiceCommand Command;
		Command.Command = FName(*Ids[g]);
		Command.Phrases = Groups[g];
		Component->Commands.Add(Command);
		AllPhrases.Append(Groups[g]);
	}

	FString Output = TEXT("file\ttext\tms\n");
	FString Decisions = TEXT("file\tcommand\tscore\tphrase\tms\n");
	int32 Clips = 0, NonEmpty = 0;
	TArray<double> Times, TextTimes;
	for (const FString& Manifest : Manifests)
	{
		TArray<FString> Rows;
		FFileHelper::LoadFileToStringArray(Rows, *Manifest);
		if (Rows.Num() < 2)
		{
			continue;
		}
		TArray<FString> Header;
		Rows[0].ParseIntoArray(Header, TEXT(","), false);
		const int32 FileColumn = Header.Find(TEXT("file"));
		for (int32 r = 1; r < Rows.Num() && FileColumn != INDEX_NONE; ++r)
		{
			TArray<FString> Fields;
			Rows[r].ParseIntoArray(Fields, TEXT(","), false);
			if (!Fields.IsValidIndex(FileColumn))
			{
				continue;
			}
			const FString Wav = FPaths::GetPath(Manifest) / Fields[FileColumn];
			TArray<int16> Samples;
			int32 Rate = 0;
			FString LoadError;
			if (!SpeechTest::LoadPcm16(Wav, Samples, Rate, LoadError) || Rate != 16000)
			{
				AddWarning(FString::Printf(TEXT("%s: %s"), *Wav, LoadError.IsEmpty() ? TEXT("not 16 kHz") : *LoadError));
				continue;
			}
			TArray<float> Audio;
			Audio.SetNumUninitialized(Samples.Num());
			for (int32 i = 0; i < Samples.Num(); ++i)
			{
				Audio[i] = (float)Samples[i] / 32768.f;
			}
			// the model alone, then the model and the phrase scoring: what scoring costs
			const double TextStart = FPlatformTime::Seconds();
			Engine->Transcribe(Audio, TArray<FString>());
			const double TextMs = (FPlatformTime::Seconds() - TextStart) * 1000.0;
			const double Start = FPlatformTime::Seconds();
			const FSpeechTranscription Result = Engine->Transcribe(Audio, AllPhrases);
			const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
			if (!Result.Error.IsEmpty())
			{
				AddError(FString::Printf(TEXT("%s: %s"), *Wav, *Result.Error));
				continue;
			}
			// the first call pays for warming up; leave it out of the times
			if (Clips > 0)
			{
				Times.Add(Ms);
				TextTimes.Add(TextMs);
			}
			++Clips;
			NonEmpty += !Result.Text.IsEmpty();
			FString Relative = Wav;
			FPaths::MakePathRelativeTo(Relative, *(EvalDir + TEXT("/")));
			Output += FString::Printf(TEXT("%s\t%s\t%.1f\n"), *Relative, *Result.Text.Replace(TEXT("\t"), TEXT(" ")), Ms);
			FString Phrase;
			float Score = 0.f;
			const FVoiceCommand* Command = Component->FindCommandForPhraseScores(Result.PhraseScores, Phrase, Score);
			Decisions += FString::Printf(TEXT("%s\t%s\t%.3f\t%s\t%.1f\n"), *Relative,
				Command != nullptr ? *Command->Command.ToString() : TEXT("-"), Score, *Phrase, Ms);
		}
	}

	const FString OutPath = FPaths::ProjectSavedDir() / TEXT("SpeechRecognition") / TEXT("darija-nne.tsv");
	FFileHelper::SaveStringToFile(Output, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(Decisions, *(FPaths::GetPath(OutPath) / TEXT("darija-nne-kws.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	Times.Sort();
	TextTimes.Sort();
	const double Median = Times.Num() > 0 ? Times[Times.Num() / 2] : 0.0;
	const double TextMedian = TextTimes.Num() > 0 ? TextTimes[TextTimes.Num() / 2] : 0.0;
	AddInfo(FString::Printf(TEXT("%d clips, %d with text, median %.0f ms per clip with %d phrases scored, %.0f ms for the model alone; transcripts in %s"),
		Clips, NonEmpty, Median, AllPhrases.Num(), TextMedian, *OutPath));
	TestTrue(TEXT("some clips were transcribed"), Clips > 0 && NonEmpty > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDarijaSubsystemTest, "SpeechRecognition.Subsystem.Darija", TranscriptionTestFlags)
bool FDarijaSubsystemTest::RunTest(const FString& Parameters)
{
	if (!USpeechRecognitionSettings::HasTranscriptionModel(TEXT("aeb")) || !USpeechRecognitionSettings::HasSphinxModel(TEXT("en")))
	{
		AddInfo(TEXT("Skipped: needs the omnilingual and en models (Tools/Get-Models.ps1 -Language en,omnilingual)"));
		return true;
	}
	USpeechRecognitionSubsystem* Subsystem = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (UWorld* World = Context.World())
		{
			Subsystem = Subsystem != nullptr ? Subsystem : World->GetSubsystem<USpeechRecognitionSubsystem>();
		}
	}
	if (!TestNotNull(TEXT("a world with the subsystem"), Subsystem))
	{
		return true;
	}
	Subsystem->Shutdown();

	// grammar mode needs a PocketSphinx model: refused at once, with a reason
	AddExpectedError(TEXT("is transcribed: use keyword mode"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("Init with aeb"), Subsystem->InitWithLanguageCode(TEXT("aeb")));
	TestTrue(TEXT("aeb is transcribed"), Subsystem->IsTranscribing());
	TestFalse(TEXT("grammar mode is refused"), Subsystem->EnableGrammarMode(TEXT("move")));
	TestEqual(TEXT("with the transcription error"), Subsystem->GetLastError(), ESpeechRecognitionError::TranscriberUnavailable);

	struct FRun
	{
		int32 Step = 0;
		double Deadline = 0.0;
		double LoadSeconds = 0.0;
	};
	const TSharedRef<FRun> Run = MakeShared<FRun>();
	const TWeakObjectPtr<USpeechRecognitionSubsystem> WeakSubsystem(Subsystem);

	// Darija, English, Darija: each must reach Listening on the live microphone
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, WeakSubsystem]() -> bool
	{
		USpeechRecognitionSubsystem* Speech = WeakSubsystem.Get();
		if (Speech == nullptr)
		{
			AddError(TEXT("The subsystem went away during the test."));
			return true;
		}
		const double Now = FPlatformTime::Seconds();
		static const TCHAR* const Languages[] = { TEXT("aeb"), TEXT("en"), TEXT("ar"), TEXT("any") };

		if (Run->Step % 2 == 0)
		{
			const int32 Index = Run->Step / 2;
			if (Index >= 4)
			{
				Speech->Shutdown();
				return true;
			}
			TArray<FRecognitionPhrase> Phrases;
			Phrases.Emplace(Index == 1 ? TEXT("go forward") : Index == 3 ? TEXT("amorix valor") : TEXT("حلّ الباب"));
			const double Start = FPlatformTime::Seconds();
			if (!Speech->InitWithLanguageCode(Languages[Index]) || !Speech->EnableKeywordMode(Phrases))
			{
				AddError(FString::Printf(TEXT("%s: could not start: %s"), Languages[Index], *Speech->GetLastErrorMessage()));
				Speech->Shutdown();
				return true;
			}
			if (Index == 0)
			{
				// the first Darija start loads the model on the game thread
				Run->LoadSeconds = FPlatformTime::Seconds() - Start;
			}
			Run->Deadline = Now + 30.0;
			++Run->Step;
			return false;
		}

		const int32 Index = Run->Step / 2;
		if (Speech->GetState() == ESpeechRecognitionState::Listening)
		{
			TestEqual(FString::Printf(TEXT("%s: transcribed or not"), Languages[Index]), Speech->IsTranscribing(), Index != 1);
			AddInfo(FString::Printf(TEXT("%s: listening (%s)%s"), Languages[Index],
				Speech->IsTranscribing() ? *Speech->GetTranscriberDescription() : TEXT("PocketSphinx"),
				Index == 0 ? *FString::Printf(TEXT(", model loaded in %.1f s"), Run->LoadSeconds) : TEXT("")));
			++Run->Step;
			return false;
		}
		if (Speech->GetState() == ESpeechRecognitionState::Failed)
		{
			if (Speech->GetLastError() == ESpeechRecognitionError::AudioDeviceFailed)
			{
				AddExpectedError(TEXT("recording device"), EAutomationExpectedErrorFlags::Contains, 0);
				AddInfo(TEXT("Skipped: no recording device could be opened on this machine."));
			}
			else
			{
				AddError(FString::Printf(TEXT("%s: failed: %s"), Languages[Index], *Speech->GetLastErrorMessage()));
			}
			Speech->Shutdown();
			return true;
		}
		if (Now > Run->Deadline)
		{
			AddError(FString::Printf(TEXT("%s: still not listening after 30 s"), Languages[Index]));
			Speech->Shutdown();
			return true;
		}
		return false;
	}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
