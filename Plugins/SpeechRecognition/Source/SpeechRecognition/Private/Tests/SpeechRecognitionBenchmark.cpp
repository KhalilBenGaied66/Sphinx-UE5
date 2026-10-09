#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SpeechRecognitionSettings.h"
#include "SpeechTestSupport.h"
#include "SphinxDecoder.h"

// Run with:  Automation RunTests SpeechBenchmark
//
// Decodes every utterance listed in Resources/TestAudio/utterances.tsv and writes
// what it measured to Saved/SpeechRecognition/benchmark.md. It asserts nothing
// about accuracy: it is a measuring tool, and the report says what was measured on.

namespace
{
	struct FKeywordRun
	{
		/** Report a phrase as soon as it is spotted, rather than at the end of the utterance. */
		bool bReportEarly = false;
		/** A new decoder for every utterance: each one is the first thing the decoder hears. */
		bool bFreshDecoderPerUtterance = false;
		/** The two safeguards that are on by default. */
		bool bKeepBestOfOverlapping = true;
		bool bSettleAtStartUp = true;
		/** Near-silence fed before each file: how long the decoder has been running when the speech starts. */
		float LeadInSeconds = 0.5f;
		/** PocketSphinx parameters set on top of Init's defaults. */
		TArray<FSphinxParam> ExtraParams;
	};

	struct FKeywordScore
	{
		int32 Commands = 0;       // utterances that contain a command
		int32 Hits = 0;           // ... and it was spotted
		int32 Negatives = 0;      // utterances that contain no command
		int32 CleanNegatives = 0; // ... and nothing was spotted
		int32 FalseAlarms = 0;    // phrases spotted that were not said
		double AudioSeconds = 0.0;
		double DecodeSeconds = 0.0;
		TArray<double> AloneLags;    // command said on its own: end of speech to report
		TArray<double> SentenceLags; // command inside a longer sentence: end of the sentence to report
		TArray<FString> Misses;
		TArray<FString> FalseAlarmNotes;
	};

	double Median(TArray<double> Values)
	{
		if (Values.Num() == 0)
		{
			return 0.0;
		}
		Values.Sort();
		return Values[Values.Num() / 2];
	}

	FString Milliseconds(const TArray<double>& Lags)
	{
		return Lags.Num() > 0 ? FString::Printf(TEXT("%.0f ms"), Median(Lags) * 1000.0) : FString(TEXT("-"));
	}

	bool HasAudio(const SpeechTest::FUtterance& Utterance)
	{
		return IFileManager::Get().FileExists(*Utterance.File);
	}

	FString Describe(const FRecognisedPhraseDetail& Phrase)
	{
		return FString::Printf(TEXT("\"%s\" at %.2f-%.2f s, score %d"), *Phrase.Phrase, Phrase.StartTime, Phrase.EndTime, Phrase.Score);
	}

	/** Decodes every keyword utterance of a language against a list of key phrases. */
	bool ScoreKeywords(const FString& Language, const TArray<SpeechTest::FUtterance>& Utterances, const TArray<FRecognitionPhrase>& Phrases, const FKeywordRun& Run, FKeywordScore& Score, FString& OutError)
	{
		FSphinxDecoderConfig Config = SpeechTest::MakeConfig(Language, ESpeechRecognitionMode::VE_KEYWORD);
		Config.Keywords = Phrases;
		Config.bReportKeyPhrasesEarly = Run.bReportEarly;
		Config.bKeepBestOfOverlappingKeyPhrases = Run.bKeepBestOfOverlapping;
		Config.bSettleAtStartUp = Run.bSettleAtStartUp;
		for (const FSphinxParam& Extra : Run.ExtraParams)
		{
			Config.Params.RemoveAll([&Extra](const FSphinxParam& Param) { return Param.Name == Extra.Name; });
			Config.Params.Add(Extra);
		}

		FSphinxDecoder Decoder;
		ESpeechRecognitionError Error = ESpeechRecognitionError::None;
		if (!Run.bFreshDecoderPerUtterance && !Decoder.Initialize(Config, Error, OutError))
		{
			return false;
		}

		for (const SpeechTest::FUtterance& Utterance : Utterances)
		{
			if (Utterance.Language != Language || !HasAudio(Utterance))
			{
				continue;
			}
			// sentences that only exist for the grammar tests are not keyword material,
			// except the recorded one, which does contain a command
			if (Utterance.bGrammarSentence && Utterance.Expected.IsEmpty())
			{
				continue;
			}

			if (Run.bFreshDecoderPerUtterance)
			{
				// Shutdown first: Initialize alone would keep the decoder, and everything it has adapted to
				Decoder.Shutdown();
				if (!Decoder.Initialize(Config, Error, OutError))
				{
					return false;
				}
			}

			SpeechTest::FDecodeResult Result;
			if (!SpeechTest::DecodeFile(Decoder, Utterance.File, Result, OutError, Run.LeadInSeconds))
			{
				return false;
			}
			Score.AudioSeconds += Result.AudioSeconds;
			Score.DecodeSeconds += Result.DecodeSeconds;

			if (Utterance.Expected.IsEmpty())
			{
				++Score.Negatives;
				if (Result.Phrases.Num() == 0)
				{
					++Score.CleanNegatives;
				}
			}
			else
			{
				++Score.Commands;
				double Lag = 0.0;
				if (Result.GetReportLag(Utterance.Expected, Lag))
				{
					++Score.Hits;
					(Utterance.Text == Utterance.Expected ? Score.AloneLags : Score.SentenceLags).Add(Lag);
				}
				else
				{
					Score.Misses.Add(FString::Printf(TEXT("%s: said \"%s\", heard \"%s\""), *Utterance.Id, *Utterance.Text, *Result.Joined()));
				}
			}

			const FRecognisedPhraseDetail* Right = Result.Phrases.FindByPredicate([&Utterance](const FRecognisedPhraseDetail& Phrase) { return Phrase.Phrase == Utterance.Expected; });
			for (const FRecognisedPhraseDetail& Phrase : Result.Phrases)
			{
				if (Phrase.Phrase != Utterance.Expected)
				{
					++Score.FalseAlarms;
					Score.FalseAlarmNotes.Add(FString::Printf(TEXT("%s: said \"%s\", heard %s%s"), *Utterance.Id, *Utterance.Text, *Describe(Phrase),
						Right != nullptr ? *FString::Printf(TEXT(" as well as %s"), *Describe(*Right)) : TEXT("")));
				}
			}
		}
		return true;
	}

	double MeasureInitSeconds(const FSphinxDecoderConfig& Config, int32 Runs, double& OutMemoryMb)
	{
		ESpeechRecognitionError Error = ESpeechRecognitionError::None;
		FString Message;
		double Total = 0.0;
		OutMemoryMb = 0.0;
		for (int32 Run = 0; Run < Runs; ++Run)
		{
			FSphinxDecoder Decoder;
			const uint64 Before = FPlatformMemory::GetStats().UsedPhysical;
			const double Start = FPlatformTime::Seconds();
			if (!Decoder.Initialize(Config, Error, Message))
			{
				return -1.0;
			}
			Total += FPlatformTime::Seconds() - Start;
			OutMemoryMb = FMath::Max(OutMemoryMb, ((double)FPlatformMemory::GetStats().UsedPhysical - (double)Before) / (1024.0 * 1024.0));
		}
		return Total / (double)Runs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechBenchmarkReport, "SpeechBenchmark.Report", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::PerfFilter)
bool FSpeechBenchmarkReport::RunTest(const FString& Parameters)
{
	TArray<SpeechTest::FUtterance> Utterances;
	if (!TestTrue(TEXT("utterances.tsv loads"), SpeechTest::LoadUtterances(Utterances)))
	{
		return true;
	}

	FString Report;
	Report += TEXT("# Benchmark\n\n");
	Report += FString::Printf(TEXT("Written by `Automation RunTests SpeechBenchmark` on %s.\n\n"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d")));
	Report += FString::Printf(TEXT("- Engine: %s\n"), *FEngineVersion::Current().ToString(EVersionComponent::Patch));
	Report += FString::Printf(TEXT("- CPU: %s (%d cores, %d threads); decoding uses one thread\n"), *FPlatformMisc::GetCPUBrand().TrimStartAndEnd(), FPlatformMisc::NumberOfCores(), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
	Report += TEXT("- Decoder parameters: the ones `Init` sets by default\n");
	Report += TEXT("- Audio is fed 1024 samples (64 ms) at a time, as the microphone thread does\n\n");

	int32 Recorded = 0;
	int32 Synthetic = 0;
	for (const SpeechTest::FUtterance& Utterance : Utterances)
	{
		if (HasAudio(Utterance))
		{
			(Utterance.Source == TEXT("recorded") ? Recorded : Synthetic)++;
		}
	}
	Report += FString::Printf(TEXT("**What the audio is.** %d utterance(s) recorded by a person and %d synthesised by Windows text-to-speech voices. "), Recorded, Synthetic);
	Report += TEXT("Synthetic speech is clean, evenly paced and free of room noise, so the figures below are a regression baseline for this code, not a forecast of how the recogniser does with players and their microphones.\n\n");
	Report += TEXT("Times are audio time: how much sound had gone into the decoder when it answered. Decoding itself runs far faster than real time and adds next to nothing.\n\n");

	for (const FString& Language : { FString(TEXT("en")), FString(TEXT("fr")), FString(TEXT("de")) })
	{
		TArray<FRecognitionPhrase> Commands;
		SpeechTest::LoadCommands(Language, Commands);
		const bool bHasAudio = Utterances.ContainsByPredicate([&Language](const SpeechTest::FUtterance& Utterance) { return Utterance.Language == Language && HasAudio(Utterance); });
		if (Commands.Num() == 0 || !bHasAudio || !SpeechTest::IsLanguageInstalled(Language))
		{
			continue;
		}

		Report += FString::Printf(TEXT("## Keyword spotting, `%s`\n\n"), *Language);
		Report += FString::Printf(TEXT("%d key phrases are listened for at once (`Resources/TestAudio/commands.tsv`): single words at tolerance V8, longer phrases at V5.\n\n"), Commands.Num());
		if (Language == TEXT("de"))
		{
			Report += TEXT("The German audio is 30 sentences read by one speaker, from VoxForge. They belong to the corpus the German model was trained on, so this section shows that German works from end to end; it says little about how well it does with another voice. The key phrases are fragments of 15 of the sentences; the other 15 contain none.\n\n");
		}
		else
		{
			Report += TEXT("Each phrase is said alone at three speaking rates and once inside a sentence, by a Windows text-to-speech voice; six sentences contain no phrase.\n\n");
		}

		// start-up cost
		FSphinxDecoderConfig Config = SpeechTest::MakeConfig(Language, ESpeechRecognitionMode::VE_KEYWORD);
		Config.Keywords = Commands;
		double MemoryMb = 0.0;
		const double InitSeconds = MeasureInitSeconds(Config, 3, MemoryMb);

		FKeywordScore AtEnd;
		FKeywordScore Early;
		FKeywordScore AllOverlaps;
		FString Error;
		FKeywordRun EarlyRun;
		EarlyRun.bReportEarly = true;
		FKeywordRun AllOverlapsRun;
		AllOverlapsRun.bKeepBestOfOverlapping = false;
		if (!ScoreKeywords(Language, Utterances, Commands, FKeywordRun(), AtEnd, Error)
			|| !ScoreKeywords(Language, Utterances, Commands, EarlyRun, Early, Error)
			|| !ScoreKeywords(Language, Utterances, Commands, AllOverlapsRun, AllOverlaps, Error))
		{
			AddError(FString::Printf(TEXT("%s: %s"), *Language, *Error));
			continue;
		}

		// the first thing a new decoder hears: a new decoder for every utterance
		FKeywordScore FirstSoon;
		FKeywordScore FirstSoonOnce;
		FKeywordScore FirstLater;
		FKeywordScore FirstLaterOnce;
		{
			FKeywordRun Run;
			Run.bFreshDecoderPerUtterance = true;
			Run.LeadInSeconds = 0.5f;
			ScoreKeywords(Language, Utterances, Commands, Run, FirstSoon, Error);
			Run.bSettleAtStartUp = false;
			ScoreKeywords(Language, Utterances, Commands, Run, FirstSoonOnce, Error);
			Run.LeadInSeconds = 3.0f;
			ScoreKeywords(Language, Utterances, Commands, Run, FirstLaterOnce, Error);
			Run.bSettleAtStartUp = true;
			ScoreKeywords(Language, Utterances, Commands, Run, FirstLater, Error);
		}

		Report += TEXT("| | Reported when the speaker pauses (default) | Reported as soon as spotted |\n|---|---|---|\n");
		Report += FString::Printf(TEXT("| Commands spotted | %d of %d | %d of %d |\n"), AtEnd.Hits, AtEnd.Commands, Early.Hits, Early.Commands);
		Report += FString::Printf(TEXT("| Phrases spotted that were not said | %d | %d |\n"), AtEnd.FalseAlarms, Early.FalseAlarms);
		Report += FString::Printf(TEXT("| Sentences without a command left alone | %d of %d | %d of %d |\n"), AtEnd.CleanNegatives, AtEnd.Negatives, Early.CleanNegatives, Early.Negatives);
		Report += FString::Printf(TEXT("| Command said on its own: end of speech to report (median) | %s | %s |\n"), *Milliseconds(AtEnd.AloneLags), *Milliseconds(Early.AloneLags));
		Report += FString::Printf(TEXT("| Command inside a sentence: end of the sentence to report (median) | %s | %s |\n\n"), *Milliseconds(AtEnd.SentenceLags), *Milliseconds(Early.SentenceLags));
		Report += TEXT("A negative time means the phrase was reported while the speaker was still talking.\n\n");

		Report += TEXT("When two different phrases are spotted over the same stretch of speech, only the better-scored one is kept. Without that:\n\n");
		Report += TEXT("| | Commands spotted | Spotted but not said |\n|---|---|---|\n");
		Report += FString::Printf(TEXT("| Best of overlapping phrases (default) | %d of %d | %d |\n"), AtEnd.Hits, AtEnd.Commands, AtEnd.FalseAlarms);
		Report += FString::Printf(TEXT("| Every phrase spotted | %d of %d | %d |\n\n"), AllOverlaps.Hits, AllOverlaps.Commands, AllOverlaps.FalseAlarms);

		Report += TEXT("The first thing a new decoder hears (a new decoder for every utterance; the figures above are from a decoder left running):\n\n");
		Report += TEXT("| | Speech 0.5 s after start-up | Speech 3 s after start-up |\n|---|---|---|\n");
		Report += FString::Printf(TEXT("| Decoded once, as up to July 2026 | %d of %d spotted, %d not said | %d of %d spotted, %d not said |\n"),
			FirstSoonOnce.Hits, FirstSoonOnce.Commands, FirstSoonOnce.FalseAlarms, FirstLaterOnce.Hits, FirstLaterOnce.Commands, FirstLaterOnce.FalseAlarms);
		Report += FString::Printf(TEXT("| With start-up handling (default) | %d of %d spotted, %d not said | %d of %d spotted, %d not said |\n\n"),
			FirstSoon.Hits, FirstSoon.Commands, FirstSoon.FalseAlarms, FirstLater.Hits, FirstLater.Commands, FirstLater.FalseAlarms);

		Report += TEXT("| Measure | Value |\n|---|---|\n");
		Report += FString::Printf(TEXT("| Model load (acoustic model + dictionary + key phrases) | %.0f ms |\n"), InitSeconds * 1000.0);
		Report += FString::Printf(TEXT("| Memory taken by a loaded decoder | about %.0f MB |\n"), MemoryMb);
		Report += FString::Printf(TEXT("| Decoding speed | %.0fx faster than real time (%.1f s of audio in %.2f s) |\n\n"), AtEnd.AudioSeconds / FMath::Max(AtEnd.DecodeSeconds, 1e-6), AtEnd.AudioSeconds, AtEnd.DecodeSeconds);

		if (AtEnd.Misses.Num() > 0)
		{
			Report += TEXT("Missed (default reporting):\n\n");
			for (const FString& Miss : AtEnd.Misses)
			{
				Report += FString::Printf(TEXT("- %s\n"), *Miss);
			}
			Report += TEXT("\n");
		}
		if (AtEnd.FalseAlarmNotes.Num() > 0)
		{
			Report += TEXT("Spotted but not said (default reporting):\n\n");
			for (const FString& Note : AtEnd.FalseAlarmNotes)
			{
				Report += FString::Printf(TEXT("- %s\n"), *Note);
			}
			Report += TEXT("\n");
		}

		AddInfo(FString::Printf(TEXT("%s keyword, at end: %d/%d commands, %d not said, %d/%d clean, lag alone %s / in sentence %s"),
			*Language, AtEnd.Hits, AtEnd.Commands, AtEnd.FalseAlarms, AtEnd.CleanNegatives, AtEnd.Negatives, *Milliseconds(AtEnd.AloneLags), *Milliseconds(AtEnd.SentenceLags)));
		AddInfo(FString::Printf(TEXT("%s keyword, early: %d/%d commands, %d not said, %d/%d clean, lag alone %s / in sentence %s"),
			*Language, Early.Hits, Early.Commands, Early.FalseAlarms, Early.CleanNegatives, Early.Negatives, *Milliseconds(Early.AloneLags), *Milliseconds(Early.SentenceLags)));
		AddInfo(FString::Printf(TEXT("%s keyword, all overlapping phrases kept: %d/%d commands, %d not said"), *Language, AllOverlaps.Hits, AllOverlaps.Commands, AllOverlaps.FalseAlarms));
		AddInfo(FString::Printf(TEXT("%s first utterance, 0.5 s after start-up: %d/%d (%d not said); decoded once: %d/%d (%d not said)"),
			*Language, FirstSoon.Hits, FirstSoon.Commands, FirstSoon.FalseAlarms, FirstSoonOnce.Hits, FirstSoonOnce.Commands, FirstSoonOnce.FalseAlarms));
		AddInfo(FString::Printf(TEXT("%s first utterance, 3 s after start-up: %d/%d (%d not said); decoded once: %d/%d (%d not said)"),
			*Language, FirstLater.Hits, FirstLater.Commands, FirstLater.FalseAlarms, FirstLaterOnce.Hits, FirstLaterOnce.Commands, FirstLaterOnce.FalseAlarms));
		AddInfo(FString::Printf(TEXT("%s: load %.0f ms, %.0f MB, %.0fx real time"), *Language, InitSeconds * 1000.0, MemoryMb, AtEnd.AudioSeconds / FMath::Max(AtEnd.DecodeSeconds, 1e-6)));

		// The beam: how many competing hypotheses the search keeps alive. Init narrows it.
		Report += TEXT("The search beam (`-beam`): `Init` sets 1e-40 by default, PocketSphinx's own default is 1e-48, which keeps more hypotheses alive.\n\n");
		Report += TEXT("| Beam | Commands spotted | Spotted but not said | Decoding speed |\n|---|---|---|---|\n");
		for (const TCHAR* Beam : { TEXT("1e-40"), TEXT("1e-48"), TEXT("1e-60"), TEXT("1e-80") })
		{
			FKeywordRun BeamRun;
			BeamRun.ExtraParams.Add({ TEXT("-beam"), ESpeechRecognitionParamType::VE_FLOAT, Beam });
			FKeywordScore BeamScore;
			if (ScoreKeywords(Language, Utterances, Commands, BeamRun, BeamScore, Error))
			{
				Report += FString::Printf(TEXT("| %s | %d of %d | %d | %.0fx real time |\n"), Beam, BeamScore.Hits, BeamScore.Commands, BeamScore.FalseAlarms, BeamScore.AudioSeconds / FMath::Max(BeamScore.DecodeSeconds, 1e-6));
				AddInfo(FString::Printf(TEXT("%s beam %s: %d/%d commands, %d not said, %.0fx real time"), *Language, Beam, BeamScore.Hits, BeamScore.Commands, BeamScore.FalseAlarms, BeamScore.AudioSeconds / FMath::Max(BeamScore.DecodeSeconds, 1e-6)));
			}
		}
		Report += TEXT("\n");

		// what the tolerance setting trades
		Report += TEXT("The same audio with every phrase at one tolerance (default reporting):\n\n");
		Report += TEXT("| Tolerance | Commands spotted | Spotted but not said | Sentences without a command left alone |\n|---|---|---|---|\n");
		for (int32 Level = 1; Level <= 10; ++Level)
		{
			TArray<FRecognitionPhrase> Uniform = Commands;
			for (FRecognitionPhrase& Phrase : Uniform)
			{
				Phrase.tolerance = (EPhraseRecognitionTolerance)(Level - 1);
			}
			FKeywordScore Sweep;
			if (ScoreKeywords(Language, Utterances, Uniform, FKeywordRun(), Sweep, Error))
			{
				Report += FString::Printf(TEXT("| V%d | %d of %d | %d | %d of %d |\n"), Level, Sweep.Hits, Sweep.Commands, Sweep.FalseAlarms, Sweep.CleanNegatives, Sweep.Negatives);
			}
		}
		Report += TEXT("\n");
	}

	// Grammar mode, and what reading the result after the decoder's final pass changes
	if (SpeechTest::IsLanguageInstalled(TEXT("en")))
	{
		Report += TEXT("## Grammar mode, `en`\n\n");
		Report += TEXT("Sentences of `Resources/TestAudio/grammars/en_move.gram`; a sentence counts when every word is right.\n\n");
		Report += TEXT("| When the result is read | Sentences right |\n|---|---|\n");

		IConsoleVariable* LegacyOrder = IConsoleManager::Get().FindConsoleVariable(TEXT("SpeechRecognition.LegacyHypothesisOrder"));
		for (const bool bLegacy : { false, true })
		{
			if (LegacyOrder != nullptr)
			{
				LegacyOrder->Set(bLegacy ? 1 : 0, ECVF_SetByCode);
			}

			FSphinxDecoderConfig Config = SpeechTest::MakeConfig(TEXT("en"), ESpeechRecognitionMode::VE_GRAMMAR);
			Config.GrammarPath = SpeechTest::GetAudioRoot() / TEXT("grammars/en_move.gram");
			FSphinxDecoder Decoder;
			ESpeechRecognitionError Error = ESpeechRecognitionError::None;
			FString Message;
			if (!Decoder.Initialize(Config, Error, Message))
			{
				AddError(Message);
				break;
			}

			int32 Sentences = 0;
			int32 Right = 0;
			TArray<FString> Wrong;
			for (const SpeechTest::FUtterance& Utterance : Utterances)
			{
				if (!Utterance.bGrammarSentence || !HasAudio(Utterance))
				{
					continue;
				}
				SpeechTest::FDecodeResult Result;
				if (!SpeechTest::DecodeFile(Decoder, Utterance.File, Result, Message))
				{
					continue;
				}
				++Sentences;
				if (Result.Joined() == Utterance.Text)
				{
					++Right;
				}
				else
				{
					Wrong.Add(FString::Printf(TEXT("said \"%s\", heard \"%s\""), *Utterance.Text, *Result.Joined()));
				}
			}
			Report += FString::Printf(TEXT("| %s | %d of %d%s |\n"),
				bLegacy ? TEXT("Before the utterance is closed (as up to July 2026)") : TEXT("After the utterance is closed (now)"),
				Right, Sentences, Wrong.Num() > 0 ? *FString::Printf(TEXT(" (%s)"), *FString::Join(Wrong, TEXT("; "))) : TEXT(""));
			AddInfo(FString::Printf(TEXT("en grammar (%s order): %d/%d sentences"), bLegacy ? TEXT("legacy") : TEXT("current"), Right, Sentences));
		}
		if (LegacyOrder != nullptr)
		{
			LegacyOrder->Set(0, ECVF_SetByCode);
		}
		Report += TEXT("\n");
	}

	const FString ReportPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SpeechRecognition/benchmark.md"));
	if (TestTrue(TEXT("the report is written"), FFileHelper::SaveStringToFile(Report, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
	{
		AddInfo(FString::Printf(TEXT("Report: %s"), *ReportPath));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
