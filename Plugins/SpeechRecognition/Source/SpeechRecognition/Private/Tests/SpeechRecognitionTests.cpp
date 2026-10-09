#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "SpeechRecognitionSettings.h"
#include "SpeechRecognitionSubsystem.h"
#include "SpeechRecognitionTestListener.h"
#include "SpeechTestSupport.h"
#include "SphinxDecoder.h"
#include "UObject/Package.h"
#include "VoiceCommandComponent.h"

// Run with:  Automation RunTests SpeechRecognition
//
// Unit.*       need nothing.
// Decoder.*    need the English model under the model folder; French and German
//              tests skip themselves when that language is not installed.
// Subsystem.*  also need a recording device, and open it for a few seconds.

namespace
{
	const EAutomationTestFlags SpeechTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	USpeechRecognitionSubsystem* FindSubsystem()
	{
		if (GEngine != nullptr)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (UWorld* World = Context.World())
				{
					if (USpeechRecognitionSubsystem* Subsystem = World->GetSubsystem<USpeechRecognitionSubsystem>())
					{
						return Subsystem;
					}
				}
			}
		}
		return nullptr;
	}

	FString RecordedSpeech(const TCHAR* Name)
	{
		return SpeechTest::GetAudioRoot() / TEXT("pocketsphinx") / Name;
	}
}

#define SKIP_UNLESS_INSTALLED(Language) \
	if (!SpeechTest::IsLanguageInstalled(TEXT(Language))) \
	{ \
		AddInfo(FString::Printf(TEXT("Skipped: no '%s' model under %s"), TEXT(Language), *USpeechRecognitionSettings::GetModelRoot())); \
		return true; \
	}

/**************************
// Unit
**************************/
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechPhraseTextTest, "SpeechRecognition.Unit.PhraseText", SpeechTestFlags)
bool FSpeechPhraseTextTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("pronunciation suffix"), FSphinxDecoder::StripPronunciationSuffixes(TEXT("read(2) the book")), FString(TEXT("read the book")));
	TestEqual(TEXT("several suffixes"), FSphinxDecoder::StripPronunciationSuffixes(TEXT("the(3) record(2)")), FString(TEXT("the record")));
	TestEqual(TEXT("digits that are not a suffix stay"), FSphinxDecoder::StripPronunciationSuffixes(TEXT("route 66")), FString(TEXT("route 66")));
	TestEqual(TEXT("brackets that are not a suffix stay"), FSphinxDecoder::StripPronunciationSuffixes(TEXT("a (b) c")), FString(TEXT("a (b) c")));

	TestTrue(TEXT("<sil>"), FSphinxDecoder::IsFillerWord(TEXT("<sil>")));
	TestTrue(TEXT("</s>"), FSphinxDecoder::IsFillerWord(TEXT("</s>")));
	TestTrue(TEXT("++noise++"), FSphinxDecoder::IsFillerWord(TEXT("++noise++")));
	TestTrue(TEXT("[NOISE]"), FSphinxDecoder::IsFillerWord(TEXT("[NOISE]")));
	TestFalse(TEXT("a word"), FSphinxDecoder::IsFillerWord(TEXT("open")));

	TestEqual(TEXT("case and spacing"), FSphinxDecoder::NormalisePhrase(TEXT("  Open   THE Door ")), FString(TEXT("open the door")));
	// escaped so that the test does not depend on how the compiler reads this file
	TestEqual(TEXT("French capitals"), FSphinxDecoder::NormalisePhrase(TEXT("ARR\u00CATE")), FString(TEXT("arr\u00EAte")));
	TestEqual(TEXT("German capitals"), FSphinxDecoder::NormalisePhrase(TEXT("\u00D6FFNE DIE T\u00DCR")), FString(TEXT("\u00F6ffne die t\u00FCr")));
	TestEqual(TEXT("Russian capitals"), FSphinxDecoder::NormalisePhrase(TEXT("\u0421\u0422\u041E\u041F")), FString(TEXT("\u0441\u0442\u043E\u043F")));

	const FRecognitionPhrase Phrase(TEXT("jump"));
	TestEqual(TEXT("the one-argument constructor keeps its phrase"), Phrase.phrase, FString(TEXT("jump")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechVolumeTest, "SpeechRecognition.Unit.Volume", SpeechTestFlags)
bool FSpeechVolumeTest::RunTest(const FString& Parameters)
{
	const int32 NumSamples = 1024;
	const float Amplitude = 10000.f;

	// 440 Hz at 16 kHz, started at two different phases
	auto MakeSine = [&](float Phase)
	{
		TArray<int16> Samples;
		Samples.SetNumUninitialized(NumSamples);
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			Samples[Index] = (int16)(Amplitude * FMath::Sin(Phase + 2.f * UE_PI * 440.f * (float)Index / 16000.f));
		}
		return Samples;
	};

	const TArray<int16> StartsPositive = MakeSine(0.3f);
	const TArray<int16> StartsNegative = MakeSine(UE_PI + 0.3f);
	TestTrue(TEXT("the second buffer does start below zero"), StartsNegative[0] < 0);

	const int32 PeakPositive = FSphinxDecoder::ComputeAveragePeak(StartsPositive.GetData(), NumSamples);
	const int32 PeakNegative = FSphinxDecoder::ComputeAveragePeak(StartsNegative.GetData(), NumSamples);
	TestTrue(FString::Printf(TEXT("peak of a sine is its amplitude (got %d)"), PeakPositive), FMath::Abs(PeakPositive - (int32)Amplitude) < 300);
	// The original loop only measured buffers whose first sample was positive.
	TestTrue(FString::Printf(TEXT("a buffer that starts below zero is measured too (got %d)"), PeakNegative), FMath::Abs(PeakNegative - (int32)Amplitude) < 300);

	TArray<int16> Silence;
	Silence.SetNumZeroed(NumSamples);
	TestEqual(TEXT("silence"), FSphinxDecoder::ComputeAveragePeak(Silence.GetData(), NumSamples), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechVoiceCommandMappingTest, "SpeechRecognition.Unit.VoiceCommandMapping", SpeechTestFlags)
bool FSpeechVoiceCommandMappingTest::RunTest(const FString& Parameters)
{
	UVoiceCommandComponent* Component = NewObject<UVoiceCommandComponent>(GetTransientPackage());
	USpeechRecognitionTestListener* Listener = NewObject<USpeechRecognitionTestListener>(GetTransientPackage());
	Component->OnVoiceCommand.AddDynamic(Listener, &USpeechRecognitionTestListener::HandleVoiceCommand);

	FVoiceCommand Door;
	Door.Command = TEXT("OpenDoor");
	Door.Phrases = { TEXT("Open the door"), TEXT("open  door") };
	FVoiceCommand Jump;
	Jump.Command = TEXT("Jump");
	Jump.Phrases = { TEXT("jump(2)") };
	Component->Commands = { Door, Jump };

	const FVoiceCommand* Found = Component->FindCommandForPhrase(TEXT("open the door"));
	TestTrue(TEXT("a phrase finds its command whatever its case"), Found != nullptr && Found->Command == TEXT("OpenDoor"));
	Found = Component->FindCommandForPhrase(TEXT("open door"));
	TestTrue(TEXT("extra spaces in the command do not matter"), Found != nullptr && Found->Command == TEXT("OpenDoor"));
	Found = Component->FindCommandForPhrase(TEXT("jump"));
	TestTrue(TEXT("a pronunciation suffix does not matter"), Found != nullptr && Found->Command == TEXT("Jump"));
	TestNull(TEXT("another component's phrase"), Component->FindCommandForPhrase(TEXT("close the door")));

	FRecognisedPhraseDetail Heard;
	Heard.Phrase = TEXT("open the door");
	FRecognisedPhraseDetail Other;
	Other.Phrase = TEXT("close the door");
	Component->HandlePhrasesRecognised({ Other, Heard });
	TestEqual(TEXT("one event for the one phrase that is ours"), Listener->Commands.Num(), 1);
	if (Listener->Commands.Num() == 1)
	{
		TestEqual(TEXT("the event names the command"), Listener->Commands[0], FName(TEXT("OpenDoor")));
	}
	return true;
}

/**************************
// Decoder
**************************/
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechDecoderMissingFilesTest, "SpeechRecognition.Decoder.MissingFiles", SpeechTestFlags)
bool FSpeechDecoderMissingFilesTest::RunTest(const FString& Parameters)
{
	FSphinxDecoder Decoder;
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;

	FSphinxDecoderConfig Config;
	Config.AcousticModelDir = FPaths::ProjectSavedDir() / TEXT("no/such/model");
	Config.DictionaryPath = FPaths::ProjectSavedDir() / TEXT("no/such.dict");
	Config.Keywords.Emplace(TEXT("jump"));
	TestFalse(TEXT("no model"), Decoder.Initialize(Config, Error, Message));
	TestEqual(TEXT("no model: error"), Error, ESpeechRecognitionError::ModelNotFound);
	TestFalse(TEXT("no model: the message is not empty"), Message.IsEmpty());
	TestFalse(TEXT("no model: nothing is left initialised"), Decoder.IsInitialized());

	SKIP_UNLESS_INSTALLED("en");

	Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_KEYWORD);
	Config.DictionaryPath = FPaths::ProjectSavedDir() / TEXT("no/such.dict");
	Config.Keywords.Emplace(TEXT("jump"));
	TestFalse(TEXT("no dictionary"), Decoder.Initialize(Config, Error, Message));
	TestEqual(TEXT("no dictionary: error"), Error, ESpeechRecognitionError::DictionaryNotFound);

	Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_GRAMMAR);
	Config.GrammarPath = FPaths::ProjectSavedDir() / TEXT("no/such.gram");
	TestFalse(TEXT("no grammar"), Decoder.Initialize(Config, Error, Message));
	TestEqual(TEXT("no grammar: error"), Error, ESpeechRecognitionError::GrammarNotFound);

	Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_LANGUAGE_MODEL);
	Config.LanguageModelPath = FPaths::ProjectSavedDir() / TEXT("no/such.lm");
	TestFalse(TEXT("no language model"), Decoder.Initialize(Config, Error, Message));
	TestEqual(TEXT("no language model: error"), Error, ESpeechRecognitionError::LanguageModelNotFound);

	Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_KEYWORD);
	TestFalse(TEXT("no key phrase"), Decoder.Initialize(Config, Error, Message));
	TestEqual(TEXT("no key phrase: error"), Error, ESpeechRecognitionError::NoValidKeywords);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechDecoderKeywordValidationTest, "SpeechRecognition.Decoder.KeywordValidation", SpeechTestFlags)
bool FSpeechDecoderKeywordValidationTest::RunTest(const FString& Parameters)
{
	SKIP_UNLESS_INSTALLED("en");

	FSphinxDecoder Decoder;
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;

	FSphinxDecoderConfig Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_KEYWORD);
	Config.Keywords.Emplace(TEXT("Go Forward"));
	Config.Keywords.Emplace(TEXT("zzyzxq the door"));
	// The unknown word comes second: the original check only ever looked at the first word.
	Config.Keywords.Emplace(TEXT("open the zzyzxq"));
	Config.Keywords.Emplace(TEXT("jump"));

	TestTrue(FString::Printf(TEXT("starts with the phrases that are usable (%s)"), *Message), Decoder.Initialize(Config, Error, Message));
	const TArray<FString>& Rejected = Decoder.GetRejectedPhrases();
	TestEqual(TEXT("two phrases are rejected"), Rejected.Num(), 2);
	TestTrue(TEXT("unknown first word"), Rejected.Contains(TEXT("zzyzxq the door")));
	TestTrue(TEXT("unknown last word"), Rejected.Contains(TEXT("open the zzyzxq")));

	TestTrue(TEXT("dictionary lookup, known"), Decoder.IsWordInDictionary(TEXT("Forward")));
	TestFalse(TEXT("dictionary lookup, unknown"), Decoder.IsWordInDictionary(TEXT("zzyzxq")));

	Config.Keywords.Reset();
	Config.Keywords.Emplace(TEXT("zzyzxq"));
	TestFalse(TEXT("nothing usable"), Decoder.Initialize(Config, Error, Message));
	TestEqual(TEXT("nothing usable: error"), Error, ESpeechRecognitionError::NoValidKeywords);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechDecoderRecordedSpeechTest, "SpeechRecognition.Decoder.RecordedSpeech", SpeechTestFlags)
bool FSpeechDecoderRecordedSpeechTest::RunTest(const FString& Parameters)
{
	SKIP_UNLESS_INSTALLED("en");

	// A person saying "go forward ten meters", from the PocketSphinx test suite.
	const FString Recording = RecordedSpeech(TEXT("goforward.raw"));
	FSphinxDecoder Decoder;
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;
	SpeechTest::FDecodeResult Result;

	// Keyword spotting
	FSphinxDecoderConfig Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_KEYWORD);
	Config.Keywords.Emplace(TEXT("go forward"), EPhraseRecognitionTolerance::VE_5);
	Config.Keywords.Emplace(TEXT("turn left"), EPhraseRecognitionTolerance::VE_5);
	Config.Keywords.Emplace(TEXT("open the door"), EPhraseRecognitionTolerance::VE_5);
	if (TestTrue(FString::Printf(TEXT("keyword mode starts (%s)"), *Message), Decoder.Initialize(Config, Error, Message))
		&& TestTrue(FString::Printf(TEXT("the recording loads (%s)"), *Message), SpeechTest::DecodeFile(Decoder, Recording, Result, Message)))
	{
		TestEqual(TEXT("keyword mode hears the one phrase that was said"), Result.Joined(), FString(TEXT("go forward")));
		TestEqual(TEXT("one start of speech"), Result.SpeechStartedEvents, 1);
		TestEqual(TEXT("one end of speech"), Result.SpeechEndedEvents, 1);
		if (Result.Phrases.Num() == 1)
		{
			const FRecognisedPhraseDetail& Phrase = Result.Phrases[0];
			TestTrue(FString::Printf(TEXT("the phrase has a position (%.2f to %.2f)"), Phrase.StartTime, Phrase.EndTime), Phrase.StartTime >= 0.f && Phrase.EndTime > Phrase.StartTime && Phrase.EndTime < 3.f);
		}
	}

	// Grammar
	Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_GRAMMAR);
	Config.GrammarPath = SpeechTest::GetAudioRoot() / TEXT("grammars/en_move.gram");
	if (TestTrue(FString::Printf(TEXT("grammar mode starts (%s)"), *Message), Decoder.Initialize(Config, Error, Message))
		&& SpeechTest::DecodeFile(Decoder, Recording, Result, Message))
	{
		TestEqual(TEXT("grammar mode hears the sentence"), Result.Joined(), FString(TEXT("go forward ten meters")));
		// words come out in the order they were said
		for (int32 Index = 1; Index < Result.Phrases.Num(); ++Index)
		{
			TestTrue(TEXT("word order follows time"), Result.Phrases[Index].StartTime >= Result.Phrases[Index - 1].StartTime);
		}
	}

	// Language model
	Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_LANGUAGE_MODEL);
	Config.LanguageModelPath = USpeechRecognitionSettings::GetLanguageModelPath(TEXT("en"), TEXT("en"));
	if (!IFileManager::Get().FileExists(*Config.LanguageModelPath))
	{
		AddInfo(TEXT("Language model part skipped: no English language model installed."));
	}
	else if (TestTrue(FString::Printf(TEXT("language model mode starts (%s)"), *Message), Decoder.Initialize(Config, Error, Message))
		&& SpeechTest::DecodeFile(Decoder, Recording, Result, Message))
	{
		TestEqual(TEXT("language model mode hears the sentence"), Result.Joined(), FString(TEXT("go forward ten meters")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechDecoderReinitialiseTest, "SpeechRecognition.Decoder.Reinitialise", SpeechTestFlags)
bool FSpeechDecoderReinitialiseTest::RunTest(const FString& Parameters)
{
	SKIP_UNLESS_INSTALLED("en");

	// Switch mode many times and watch the process's memory: the original plugin lost
	// a decoder, a configuration and two arrays on every switch.
	FSphinxDecoder Decoder;
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;

	FSphinxDecoderConfig Keyword = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_KEYWORD);
	Keyword.Keywords.Emplace(TEXT("go forward"));
	Keyword.Keywords.Emplace(TEXT("open the door"));
	FSphinxDecoderConfig Grammar = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_GRAMMAR);
	Grammar.GrammarPath = SpeechTest::GetAudioRoot() / TEXT("grammars/en_move.gram");

	// bReload changes a decoder parameter on every switch, which forces the model to be loaded again
	auto Cycle = [&](int32 Count, bool bReload, double& OutSeconds)
	{
		const double Start = FPlatformTime::Seconds();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FSphinxDecoderConfig Config = (Index % 2 == 0) ? Keyword : Grammar;
			if (bReload)
			{
				Config.Params.Add({ TEXT("-vad_postspeech"), ESpeechRecognitionParamType::VE_INTEGER, (Index % 2 == 0) ? TEXT("10") : TEXT("11") });
			}
			const bool bKept = Decoder.CanReuse(Config);
			if (bKept == bReload && Index > 0)
			{
				Message = bReload ? TEXT("expected the model to be reloaded") : TEXT("expected the decoder to be kept");
				return false;
			}
			if (!Decoder.Initialize(Config, Error, Message))
			{
				return false;
			}
		}
		OutSeconds = (FPlatformTime::Seconds() - Start) / (double)Count;
		return true;
	};

	const int32 WarmUp = 6;
	const int32 Measured = 40;
	double Seconds = 0.0;

	// 1. Same model, another mode: the decoder is kept
	if (!TestTrue(FString::Printf(TEXT("warm-up switches (%s)"), *Message), Cycle(WarmUp, false, Seconds)))
	{
		return true;
	}
	uint64 Before = FPlatformMemory::GetStats().UsedPhysical;
	TestTrue(FString::Printf(TEXT("%d switches on a kept decoder (%s)"), Measured, *Message), Cycle(Measured, false, Seconds));
	double GrowthMb = ((double)FPlatformMemory::GetStats().UsedPhysical - (double)Before) / (1024.0 * 1024.0);
	AddInfo(FString::Printf(TEXT("Mode switch on a kept decoder: %.1f ms each; memory after %d switches: %+.1f MB"), Seconds * 1000.0, Measured, GrowthMb));
	TestTrue(FString::Printf(TEXT("memory does not grow with the number of switches (%+.1f MB over %d)"), GrowthMb, Measured), GrowthMb < 20.0);

	// 2. A parameter changes: the model is loaded again each time
	if (!TestTrue(FString::Printf(TEXT("warm-up reloads (%s)"), *Message), Cycle(WarmUp, true, Seconds)))
	{
		return true;
	}
	Before = FPlatformMemory::GetStats().UsedPhysical;
	TestTrue(FString::Printf(TEXT("%d reloads (%s)"), Measured, *Message), Cycle(Measured, true, Seconds));
	GrowthMb = ((double)FPlatformMemory::GetStats().UsedPhysical - (double)Before) / (1024.0 * 1024.0);
	AddInfo(FString::Printf(TEXT("Mode switch with a model reload: %.0f ms each; memory after %d reloads: %+.1f MB"), Seconds * 1000.0, Measured, GrowthMb));
	// One loaded English decoder is tens of megabytes: a leaked one per reload would show as hundreds here.
	TestTrue(FString::Printf(TEXT("memory does not grow with the number of reloads (%+.1f MB over %d)"), GrowthMb, Measured), GrowthMb < 40.0);

	// and the last decoder still works
	SpeechTest::FDecodeResult Result;
	if (Decoder.Initialize(Keyword, Error, Message) && SpeechTest::DecodeFile(Decoder, RecordedSpeech(TEXT("goforward.raw")), Result, Message))
	{
		TestEqual(TEXT("still recognises after the switches"), Result.Joined(), FString(TEXT("go forward")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechDecoderStartUpTest, "SpeechRecognition.Decoder.StartUp", SpeechTestFlags)
bool FSpeechDecoderStartUpTest::RunTest(const FString& Parameters)
{
	SKIP_UNLESS_INSTALLED("en");
	const FString Recording = SpeechTest::GetAudioRoot() / TEXT("generated/en/go_back_normal.wav");
	if (!IFileManager::Get().FileExists(*Recording))
	{
		AddInfo(TEXT("Skipped: run Tools/New-TestAudio.ps1 first."));
		return true;
	}

	// The very first thing a new decoder hears, half a second after it starts.
	FSphinxDecoderConfig Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_KEYWORD);
	SpeechTest::LoadCommands(TEXT("en"), Config.Keywords);

	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;
	SpeechTest::FDecodeResult Result;

	// As the plugin behaved up to July 2026: the voice detector's false start is
	// reported as an utterance, and the phrase that follows it is lost.
	{
		FSphinxDecoder Decoder;
		Config.bSettleAtStartUp = false;
		if (TestTrue(*Message, Decoder.Initialize(Config, Error, Message)) && SpeechTest::DecodeFile(Decoder, Recording, Result, Message))
		{
			AddInfo(FString::Printf(TEXT("Without start-up handling: heard '%s', %d start(s) of speech reported, %d unrecognised"), *Result.Joined(), Result.SpeechStartedEvents, Result.UnrecognisedEvents));
		}
	}

	{
		FSphinxDecoder Decoder;
		Config.bSettleAtStartUp = true;
		if (TestTrue(*Message, Decoder.Initialize(Config, Error, Message)) && SpeechTest::DecodeFile(Decoder, Recording, Result, Message))
		{
			TestEqual(TEXT("the first phrase after start-up is heard"), Result.Joined(), FString(TEXT("go back")));
			TestEqual(TEXT("one start of speech, not two"), Result.SpeechStartedEvents, 1);
			TestEqual(TEXT("one end of speech"), Result.SpeechEndedEvents, 1);
			TestEqual(TEXT("no 'unknown phrase' for the false start"), Result.UnrecognisedEvents, 0);
		}

		// near-silence alone, right after start-up: nothing at all is reported
		FSphinxDecoder Quiet;
		TArray<int16> Nothing;
		if (Quiet.Initialize(Config, Error, Message))
		{
			Result = SpeechTest::DecodeSamples(Quiet, Nothing, Quiet.GetSampleRate());
			TestEqual(TEXT("silence after start-up: no start of speech"), Result.SpeechStartedEvents, 0);
			TestEqual(TEXT("silence after start-up: no unknown phrase"), Result.UnrecognisedEvents, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechDecoderNonAsciiPathTest, "SpeechRecognition.Decoder.NonAsciiPath", SpeechTestFlags)
bool FSpeechDecoderNonAsciiPathTest::RunTest(const FString& Parameters)
{
	SKIP_UNLESS_INSTALLED("en");

	// A model kept under a folder with accents in its name, as in "C:/Users/Zo\u00E9/Documents/...".
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SpeechRecognitionTests") / TEXT("mod\u00E8le \u00E9t\u00E9"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.DeleteDirectoryRecursively(*Root);
	const FString ModelDir = Root / TEXT("en/en");
	const FString Dictionary = Root / TEXT("en/en.dict");
	if (!TestTrue(TEXT("copy the model"), PlatformFile.CopyDirectoryTree(*ModelDir, *USpeechRecognitionSettings::GetAcousticModelDir(TEXT("en")), true))
		|| !TestTrue(TEXT("copy the dictionary"), PlatformFile.CopyFile(*Dictionary, *USpeechRecognitionSettings::GetDictionaryPath(TEXT("en")))))
	{
		return true;
	}

	FSphinxDecoderConfig Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_KEYWORD);
	Config.AcousticModelDir = ModelDir;
	Config.DictionaryPath = Dictionary;
	Config.Keywords.Emplace(TEXT("go forward"));

	FSphinxDecoder Decoder;
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;
	SpeechTest::FDecodeResult Result;
	if (TestTrue(FString::Printf(TEXT("loads a model from '%s' (%s)"), *Root, *Message), Decoder.Initialize(Config, Error, Message))
		&& SpeechTest::DecodeFile(Decoder, RecordedSpeech(TEXT("goforward.raw")), Result, Message))
	{
		TestEqual(TEXT("and recognises with it"), Result.Joined(), FString(TEXT("go forward")));
	}
	Decoder.Shutdown();
	PlatformFile.DeleteDirectoryRecursively(*Root);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechDecoderFrenchTest, "SpeechRecognition.Decoder.French", SpeechTestFlags)
bool FSpeechDecoderFrenchTest::RunTest(const FString& Parameters)
{
	SKIP_UNLESS_INSTALLED("fr");

	// Accented words, typed in capitals: they must be lower-cased by Unicode rules to match the dictionary.
	FSphinxDecoderConfig Config = SpeechTest::MakeConfig(TEXT("fr"), ESpeechRecognitionMode::VE_KEYWORD);
	Config.Keywords.Emplace(TEXT("ARR\u00CATE"), EPhraseRecognitionTolerance::VE_8);
	Config.Keywords.Emplace(TEXT("\u00C0 Gauche"), EPhraseRecognitionTolerance::VE_5);
	Config.Keywords.Emplace(TEXT("Ouvre la porte"), EPhraseRecognitionTolerance::VE_5);
	// "zzyzxq\u00E9" is in no dictionary
	Config.Keywords.Emplace(TEXT("zzyzxq\u00E9 la lumi\u00E8re"), EPhraseRecognitionTolerance::VE_5);

	FSphinxDecoder Decoder;
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;
	if (!TestTrue(FString::Printf(TEXT("French keyword mode starts (%s)"), *Message), Decoder.Initialize(Config, Error, Message)))
	{
		return true;
	}
	TestEqual(TEXT("only the phrase with the unknown word is rejected"), Decoder.GetRejectedPhrases().Num(), 1);
	TestTrue(TEXT("accented word, in capitals, is found"), Decoder.IsWordInDictionary(TEXT("ARR\u00CATE")));

	const FString Generated = SpeechTest::GetAudioRoot() / TEXT("generated/fr");
	if (!IFileManager::Get().FileExists(*(Generated / TEXT("arrete_normal.wav"))))
	{
		AddInfo(TEXT("Audio part skipped: run Tools/New-TestAudio.ps1 (needs a French Windows voice)."));
		return true;
	}

	SpeechTest::FDecodeResult Result;
	if (SpeechTest::DecodeFile(Decoder, Generated / TEXT("arrete_normal.wav"), Result, Message))
	{
		TestEqual(TEXT("hears 'arr\u00EAte'"), Result.Joined(), FString(TEXT("arr\u00EAte")));
	}
	if (SpeechTest::DecodeFile(Decoder, Generated / TEXT("a_gauche_normal.wav"), Result, Message))
	{
		TestEqual(TEXT("hears '\u00E0 gauche'"), Result.Joined(), FString(TEXT("\u00E0 gauche")));
	}
	if (SpeechTest::DecodeFile(Decoder, Generated / TEXT("ouvre_la_porte_normal.wav"), Result, Message))
	{
		TestEqual(TEXT("hears 'ouvre la porte'"), Result.Joined(), FString(TEXT("ouvre la porte")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechDecoderGermanTest, "SpeechRecognition.Decoder.German", SpeechTestFlags)
bool FSpeechDecoderGermanTest::RunTest(const FString& Parameters)
{
	SKIP_UNLESS_INSTALLED("de");

	FSphinxDecoderConfig Config = SpeechTest::MakeConfig(TEXT("de"), ESpeechRecognitionMode::VE_KEYWORD);
	Config.Keywords.Emplace(TEXT("\u00D6FFNE DIE T\u00DCR"), EPhraseRecognitionTolerance::VE_5);
	Config.Keywords.Emplace(TEXT("schlie\u00DFe die T\u00FCr"), EPhraseRecognitionTolerance::VE_5);
	Config.Keywords.Emplace(TEXT("Licht an"), EPhraseRecognitionTolerance::VE_5);
	Config.Keywords.Emplace(TEXT("vorw\u00E4rts"), EPhraseRecognitionTolerance::VE_8);
	Config.Keywords.Emplace(TEXT("zzyzxq\u00FC"), EPhraseRecognitionTolerance::VE_8);

	FSphinxDecoder Decoder;
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;
	if (!TestTrue(FString::Printf(TEXT("German keyword mode starts (%s)"), *Message), Decoder.Initialize(Config, Error, Message)))
	{
		return true;
	}
	TestEqual(FString::Printf(TEXT("only the made-up word is rejected (%s)"), *FString::Join(Decoder.GetRejectedPhrases(), TEXT(", "))), Decoder.GetRejectedPhrases().Num(), 1);
	TestTrue(TEXT("a word with an umlaut, in capitals, is found"), Decoder.IsWordInDictionary(TEXT("T\u00DCR")));
	TestTrue(TEXT("a word with an eszett is found"), Decoder.IsWordInDictionary(TEXT("schlie\u00DFe")));

	// A German speaker, when the recordings have been fetched (Tools/Get-TestSpeech.ps1).
	// Sentence 97 of the set is one the model's authors held back from training.
	const FString Recording = SpeechTest::GetAudioRoot() / TEXT("generated/de/de115-97.wav");
	if (!IFileManager::Get().FileExists(*Recording))
	{
		AddInfo(TEXT("Audio part skipped: run Tools/Get-TestSpeech.ps1 for German recordings."));
		return true;
	}

	Config.Keywords.Reset();
	SpeechTest::LoadCommands(TEXT("de"), Config.Keywords);
	SpeechTest::FDecodeResult Result;
	if (TestTrue(FString::Printf(TEXT("German key phrases load (%s)"), *Message), Decoder.Initialize(Config, Error, Message))
		&& TestTrue(*Message, SpeechTest::DecodeFile(Decoder, Recording, Result, Message)))
	{
		// "das wird uns dann ganz direkt betreffen"
		TestEqual(TEXT("spots the phrase in a German sentence"), Result.Joined(), FString(TEXT("ganz direkt")));
	}

	// the whole sentence, with the German language model
	FSphinxDecoderConfig Dictation = SpeechTest::MakeConfig(TEXT("de"), ESpeechRecognitionMode::VE_LANGUAGE_MODEL);
	Dictation.LanguageModelPath = USpeechRecognitionSettings::GetLanguageModelPath(TEXT("de"), TEXT("de"));
	if (!IFileManager::Get().FileExists(*Dictation.LanguageModelPath))
	{
		AddInfo(TEXT("Language model part skipped: fetch it with Tools/Get-Models.ps1 -Language de -LanguageModel."));
	}
	else if (TestTrue(FString::Printf(TEXT("German language model loads (%s)"), *Message), Decoder.Initialize(Dictation, Error, Message))
		&& SpeechTest::DecodeFile(Decoder, Recording, Result, Message))
	{
		// Dictation with this technology is approximate: ask for most of the words, not all
		const TCHAR* Said[] = { TEXT("das"), TEXT("wird"), TEXT("uns"), TEXT("dann"), TEXT("ganz"), TEXT("direkt"), TEXT("betreffen") };
		int32 Heard = 0;
		for (const TCHAR* Word : Said)
		{
			Heard += Result.Contains(Word) ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("German dictation: said 'das wird uns dann ganz direkt betreffen', heard '%s' (%d of 7 words)"), *Result.Joined(), Heard));
		TestTrue(TEXT("dictation gets at least half of the words"), Heard >= 4);
	}
	return true;
}

/**************************
// Subsystem
**************************/
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechSubsystemConfigParamsTest, "SpeechRecognition.Subsystem.ConfigParams", SpeechTestFlags)
bool FSpeechSubsystemConfigParamsTest::RunTest(const FString& Parameters)
{
	USpeechRecognitionSubsystem* Subsystem = FindSubsystem();
	if (!TestNotNull(TEXT("a world with the subsystem"), Subsystem))
	{
		return true;
	}
	Subsystem->Shutdown();

	TestFalse(TEXT("refused before Init"), Subsystem->SetConfigParam(TEXT("-beam"), ESpeechRecognitionParamType::VE_FLOAT, TEXT("1e-40")));
	if (!TestTrue(TEXT("Init"), Subsystem->Init(ESpeechRecognitionLanguage::VE_English)))
	{
		return true;
	}
	TestEqual(TEXT("state after Init"), Subsystem->GetState(), ESpeechRecognitionState::Initialised);

	TestTrue(TEXT("float in exponent form"), Subsystem->SetConfigParam(TEXT("-beam"), ESpeechRecognitionParamType::VE_FLOAT, TEXT("1e-40")));
	// The original validation took any float that parsed to 0 for a parse failure.
	TestTrue(TEXT("float zero"), Subsystem->SetConfigParam(TEXT("-vad_threshold"), ESpeechRecognitionParamType::VE_FLOAT, TEXT("0")));
	TestTrue(TEXT("float zero, written 0.0"), Subsystem->SetConfigParam(TEXT("-vad_threshold"), ESpeechRecognitionParamType::VE_FLOAT, TEXT("0.0")));
	TestFalse(TEXT("float that is text"), Subsystem->SetConfigParam(TEXT("-beam"), ESpeechRecognitionParamType::VE_FLOAT, TEXT("abc")));
	TestFalse(TEXT("float with trailing text"), Subsystem->SetConfigParam(TEXT("-beam"), ESpeechRecognitionParamType::VE_FLOAT, TEXT("1.5x")));
	TestTrue(TEXT("boolean true"), Subsystem->SetConfigParam(TEXT("-remove_noise"), ESpeechRecognitionParamType::VE_BOOLEAN, TEXT("TRUE")));
	TestFalse(TEXT("boolean that is neither"), Subsystem->SetConfigParam(TEXT("-remove_noise"), ESpeechRecognitionParamType::VE_BOOLEAN, TEXT("maybe")));
	TestTrue(TEXT("integer"), Subsystem->SetConfigParam(TEXT("-vad_postspeech"), ESpeechRecognitionParamType::VE_INTEGER, TEXT("25")));
	TestFalse(TEXT("integer with a fraction"), Subsystem->SetConfigParam(TEXT("-vad_postspeech"), ESpeechRecognitionParamType::VE_INTEGER, TEXT("2.5")));
	TestTrue(TEXT("string"), Subsystem->SetConfigParam(TEXT("-agc"), ESpeechRecognitionParamType::VE_STRING, TEXT("none")));

	TestTrue(TEXT("Shutdown"), Subsystem->Shutdown());
	TestEqual(TEXT("state after Shutdown"), Subsystem->GetState(), ESpeechRecognitionState::Stopped);
	TestFalse(TEXT("a second Shutdown has nothing to do"), Subsystem->Shutdown());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechSubsystemFailsCleanlyTest, "SpeechRecognition.Subsystem.FailsCleanly", SpeechTestFlags)
bool FSpeechSubsystemFailsCleanlyTest::RunTest(const FString& Parameters)
{
	USpeechRecognitionSubsystem* Subsystem = FindSubsystem();
	if (!TestNotNull(TEXT("a world with the subsystem"), Subsystem))
	{
		return true;
	}
	Subsystem->Shutdown();

	USpeechRecognitionTestListener* Listener = NewObject<USpeechRecognitionTestListener>(GetTransientPackage());
	Subsystem->OnRecognitionFailed.AddDynamic(Listener, &USpeechRecognitionTestListener::HandleRecognitionFailed);

	// each failure below is logged as an error, on purpose
	AddExpectedError(TEXT("Call Init before enabling a recognition mode"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("No acoustic model in"), EAutomationExpectedErrorFlags::Contains, 1);

	TArray<FRecognitionPhrase> Phrases;
	Phrases.Emplace(TEXT("jump"));

	TestFalse(TEXT("a mode before Init is refused"), Subsystem->EnableKeywordMode(Phrases));
	TestEqual(TEXT("and says why"), Subsystem->GetLastError(), ESpeechRecognitionError::NotInitialised);
	TestEqual(TEXT("and stays stopped"), Subsystem->GetState(), ESpeechRecognitionState::Stopped);

	// a language that has no model: the missing files are reported at once, not on another thread
	TestTrue(TEXT("Init with an unknown language still starts the thread"), Subsystem->InitWithLanguageCode(TEXT("no-such-language")));
	TestFalse(TEXT("the mode is refused"), Subsystem->EnableKeywordMode(Phrases));
	TestEqual(TEXT("the error names the model"), Subsystem->GetLastError(), ESpeechRecognitionError::ModelNotFound);
	TestEqual(TEXT("the state is Failed"), Subsystem->GetState(), ESpeechRecognitionState::Failed);
	TestEqual(TEXT("both failures were broadcast"), Listener->Failures.Num(), 2);

	Subsystem->OnRecognitionFailed.RemoveDynamic(Listener, &USpeechRecognitionTestListener::HandleRecognitionFailed);
	Subsystem->Shutdown();
	return true;
}

/**
 * Starts and stops live recognition several times, switching mode in between.
 * This is the part of the plugin that needs a microphone: the thread, the
 * recording device, and the teardown that used to leak a worker each time.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechSubsystemLiveTest, "SpeechRecognition.Subsystem.Live", SpeechTestFlags)
bool FSpeechSubsystemLiveTest::RunTest(const FString& Parameters)
{
	SKIP_UNLESS_INSTALLED("en");
	USpeechRecognitionSubsystem* Subsystem = FindSubsystem();
	if (!TestNotNull(TEXT("a world with the subsystem"), Subsystem))
	{
		return true;
	}
	Subsystem->Shutdown();

	struct FLiveRun
	{
		int32 Cycle = 0;
		int32 Step = 0;
		double Deadline = 0.0;
		bool bNoMicrophone = false;
	};
	const TSharedRef<FLiveRun> Run = MakeShared<FLiveRun>();
	const TWeakObjectPtr<USpeechRecognitionSubsystem> WeakSubsystem(Subsystem);
	const int32 Cycles = 4;
	const double TimeoutSeconds = 15.0;

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, WeakSubsystem, Cycles, TimeoutSeconds]() -> bool
	{
		USpeechRecognitionSubsystem* Speech = WeakSubsystem.Get();
		if (Speech == nullptr)
		{
			AddError(TEXT("The subsystem went away during the test."));
			return true;
		}
		const double Now = FPlatformTime::Seconds();

		auto WaitForListening = [&]() -> int32 // 1 listening, 0 keep waiting, -1 give up
		{
			if (Speech->GetState() == ESpeechRecognitionState::Listening)
			{
				return 1;
			}
			if (Speech->GetState() == ESpeechRecognitionState::Failed)
			{
				if (Speech->GetLastError() == ESpeechRecognitionError::AudioDeviceFailed)
				{
					Run->bNoMicrophone = true;
				}
				else
				{
					AddError(FString::Printf(TEXT("Cycle %d: recognition failed: %s"), Run->Cycle, *Speech->GetLastErrorMessage()));
				}
				return -1;
			}
			if (Now > Run->Deadline)
			{
				AddError(FString::Printf(TEXT("Cycle %d: still not listening after %.0f s."), Run->Cycle, TimeoutSeconds));
				return -1;
			}
			return 0;
		};

		switch (Run->Step)
		{
		case 0: // start in keyword mode
		{
			TArray<FRecognitionPhrase> Phrases;
			Phrases.Emplace(TEXT("go forward"));
			Phrases.Emplace(TEXT("open the door"));
			if (!Speech->Init(ESpeechRecognitionLanguage::VE_English) || !Speech->EnableKeywordMode(Phrases))
			{
				AddError(FString::Printf(TEXT("Cycle %d: could not start: %s"), Run->Cycle, *Speech->GetLastErrorMessage()));
				Speech->Shutdown();
				return true;
			}
			Run->Deadline = Now + TimeoutSeconds;
			Run->Step = 1;
			return false;
		}
		case 1: // listening in keyword mode: switch to grammar mode, which rebuilds the decoder
		{
			const int32 Outcome = WaitForListening();
			if (Outcome == 0)
			{
				return false;
			}
			if (Outcome < 0)
			{
				Speech->Shutdown();
				if (Run->bNoMicrophone)
				{
					AddExpectedError(TEXT("recording device"), EAutomationExpectedErrorFlags::Contains, 0);
					AddInfo(TEXT("Skipped: no recording device could be opened on this machine."));
				}
				return true;
			}
			// Tools/Get-Models.ps1 puts a "move" grammar next to the English model
			if (!Speech->EnableGrammarMode(TEXT("move")))
			{
				// no such grammar with this model: another list of key phrases is a switch too
				AddExpectedError(TEXT("Grammar file not found"), EAutomationExpectedErrorFlags::Contains, 0);
				TArray<FRecognitionPhrase> Phrases;
				Phrases.Emplace(TEXT("turn left"));
				Speech->EnableKeywordMode(Phrases);
			}
			Run->Deadline = Now + TimeoutSeconds;
			Run->Step = 2;
			return false;
		}
		case 2: // listening again after the switch: shut down, and go round again
		{
			const int32 Outcome = WaitForListening();
			if (Outcome == 0)
			{
				return false;
			}
			TestTrue(FString::Printf(TEXT("Cycle %d: shuts down"), Run->Cycle), Speech->Shutdown());
			TestEqual(FString::Printf(TEXT("Cycle %d: stopped"), Run->Cycle), Speech->GetState(), ESpeechRecognitionState::Stopped);
			if (Outcome < 0)
			{
				return true;
			}
			if (++Run->Cycle >= Cycles)
			{
				AddInfo(FString::Printf(TEXT("%d start / switch mode / shutdown cycles on the live microphone."), Cycles));
				return true;
			}
			Run->Step = 0;
			return false;
		}
		}
		return true;
	}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
