#include "SphinxDecoder.h"
#include "SphinxRuntime.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Text.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

THIRD_PARTY_INCLUDES_START
#include <sphinxbase/err.h>
#include <sphinxbase/ckd_alloc.h>
#include <pocketsphinx.h>
THIRD_PARTY_INCLUDES_END

// PocketSphinx scales senone scores by this shift; key phrase thresholds must match it.
#define SPHINX_SENSCR_SHIFT 10

static bool GSphinxLegacyHypothesisOrder = false;
static FAutoConsoleVariableRef CVarSphinxLegacyHypothesisOrder(
	TEXT("SpeechRecognition.LegacyHypothesisOrder"),
	GSphinxLegacyHypothesisOrder,
	TEXT("0: read the hypothesis once the utterance is closed, after the decoder's final pass (default).\n")
	TEXT("1: read it just before closing the utterance, as this plugin did up to the July 2026 fixes."),
	ECVF_Default);

static int32 GSphinxStartUpReplays = 2;
static FAutoConsoleVariableRef CVarSphinxStartUpReplays(
	TEXT("SpeechRecognition.StartUpReplays"),
	GSphinxStartUpReplays,
	TEXT("While the decoder settles after start-up, how many more times an utterance may be played through it\n")
	TEXT("when nothing was recognised. 0 decodes every utterance once, as the plugin did up to July 2026."),
	ECVF_Default);

/**************************
// PocketSphinx's own log
**************************/
namespace SphinxRuntime
{
	static FCriticalSection LastErrorMutex;
	static FString LastErrorLine;

	static void LogCallback(void* /*UserData*/, err_lvl_t Level, const char* Format, ...)
	{
		const bool bIsError = (Level == ERR_ERROR || Level == ERR_FATAL);
		const bool bIsWarning = (Level == ERR_WARN);
		if (!bIsError && !bIsWarning && !UE_LOG_ACTIVE(SpeechRecognitionPlugin, Verbose))
		{
			// the decoder is chatty at INFO level: do not even format what nobody will read
			return;
		}

		char Buffer[2048];
		va_list Args;
		va_start(Args, Format);
		vsnprintf(Buffer, sizeof(Buffer), Format, Args);
		va_end(Args);
		Buffer[sizeof(Buffer) - 1] = '\0';

		FString Line(UTF8_TO_TCHAR(Buffer));
		Line.TrimEndInline();
		if (Line.IsEmpty())
		{
			return;
		}

		if (bIsError)
		{
			{
				FScopeLock Lock(&LastErrorMutex);
				LastErrorLine = Line;
			}
			UE_LOG(SpeechRecognitionPlugin, Warning, TEXT("Sphinx: %s"), *Line);
		}
		else if (bIsWarning)
		{
			UE_LOG(SpeechRecognitionPlugin, Log, TEXT("Sphinx: %s"), *Line);
		}
		else
		{
			UE_LOG(SpeechRecognitionPlugin, Verbose, TEXT("Sphinx: %s"), *Line);
		}
	}

	void InstallLogCallback()
	{
		err_set_callback(&LogCallback, nullptr);
	}

	void RemoveLogCallback()
	{
		err_set_callback(&err_logfp_cb, nullptr);
	}

	void ClearLastError()
	{
		FScopeLock Lock(&LastErrorMutex);
		LastErrorLine.Reset();
	}

	FString GetLastError()
	{
		FScopeLock Lock(&LastErrorMutex);
		return LastErrorLine;
	}
}

/**************************
// Paths
// The bundled PocketSphinx opens files with the narrow C runtime, so a path has
// to be handed over in whatever narrow encoding that runtime resolves.
**************************/
namespace
{
	enum class ENarrowEncoding : uint8 { ActiveCodePage, Utf8, ShortPath };

	bool ToNarrow(const FString& Path, ENarrowEncoding Encoding, std::string& Out)
	{
		if (Encoding == ENarrowEncoding::Utf8)
		{
			Out = TCHAR_TO_UTF8(*Path);
			return true;
		}

#if PLATFORM_WINDOWS
		FString Wide = Path;
		if (Encoding == ENarrowEncoding::ShortPath)
		{
			FString Native = Path;
			FPaths::MakePlatformFilename(Native);
			TCHAR Short[2048];
			const DWORD Length = ::GetShortPathNameW(*Native, Short, UE_ARRAY_COUNT(Short));
			if (Length == 0 || Length >= UE_ARRAY_COUNT(Short))
			{
				return false;
			}
			Wide = Short;
			if (!FCString::IsPureAnsi(*Wide))
			{
				// the volume has no 8.3 names: the short path is the long one
				return false;
			}
		}

		const UINT CodePage = ::GetACP();
		const bool bLossless = (CodePage == CP_UTF8);
		BOOL bUsedDefaultChar = 0;
		const int32 Needed = ::WideCharToMultiByte(CodePage, bLossless ? 0 : WC_NO_BEST_FIT_CHARS, *Wide, -1, nullptr, 0, nullptr, bLossless ? nullptr : &bUsedDefaultChar);
		if (Needed <= 0)
		{
			return false;
		}
		std::string Buffer(static_cast<size_t>(Needed), '\0');
		::WideCharToMultiByte(CodePage, bLossless ? 0 : WC_NO_BEST_FIT_CHARS, *Wide, -1, &Buffer[0], Needed, nullptr, bLossless ? nullptr : &bUsedDefaultChar);
		if (bUsedDefaultChar)
		{
			return false;
		}
		Buffer.resize(static_cast<size_t>(Needed) - 1);
		Out = Buffer;
		return true;
#else
		return false;
#endif
	}

	bool CanOpenNarrow(const std::string& Path)
	{
		FILE* File = nullptr;
#if PLATFORM_WINDOWS
		fopen_s(&File, Path.c_str(), "rb");
#else
		File = fopen(Path.c_str(), "rb");
#endif
		if (File != nullptr)
		{
			fclose(File);
			return true;
		}
		return false;
	}

	/**
	 * Finds a narrow spelling of Path that the C runtime can open.
	 * ProbeSuffix names a file under Path to test with when Path is a folder.
	 */
	bool ResolveNarrowPath(const FString& Path, const TCHAR* ProbeSuffix, std::string& Out)
	{
		if (FCString::IsPureAnsi(*Path))
		{
			Out = TCHAR_TO_UTF8(*Path);
			return true;
		}

		const FString Probe = (ProbeSuffix != nullptr) ? (Path / ProbeSuffix) : Path;
		for (const ENarrowEncoding Encoding : { ENarrowEncoding::ActiveCodePage, ENarrowEncoding::Utf8, ENarrowEncoding::ShortPath })
		{
			std::string NarrowProbe;
			std::string NarrowPath;
			if (ToNarrow(Probe, Encoding, NarrowProbe) && CanOpenNarrow(NarrowProbe) && ToNarrow(Path, Encoding, NarrowPath))
			{
				Out = NarrowPath;
				return true;
			}
		}
		return false;
	}

	double ToleranceToThreshold(EPhraseRecognitionTolerance Tolerance)
	{
		switch (Tolerance)
		{
		case EPhraseRecognitionTolerance::VE_1:  return 1e-60;
		case EPhraseRecognitionTolerance::VE_2:  return 1e-55;
		case EPhraseRecognitionTolerance::VE_3:  return 1e-50;
		case EPhraseRecognitionTolerance::VE_4:  return 1e-40;
		case EPhraseRecognitionTolerance::VE_5:  return 1e-30;
		case EPhraseRecognitionTolerance::VE_6:  return 1e-20;
		case EPhraseRecognitionTolerance::VE_7:  return 1e-10;
		case EPhraseRecognitionTolerance::VE_8:  return 1e-5;
		case EPhraseRecognitionTolerance::VE_9:  return 1e-4;
		case EPhraseRecognitionTolerance::VE_10: return 1e-3;
		default:                                 return 1e-2;
		}
	}

	/** The type PocketSphinx declares for a parameter, or 0 when it has no such parameter. */
	int32 FindDeclaredParamType(const char* Name)
	{
		for (const arg_t* Arg = ps_args(); Arg != nullptr && Arg->name != nullptr; ++Arg)
		{
			if (strcmp(Arg->name, Name) == 0)
			{
				return Arg->type;
			}
		}
		return 0;
	}

	bool ParseBoolean(const FString& Value, bool& Out)
	{
		if (Value.Equals(TEXT("1")) || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("yes"), ESearchCase::IgnoreCase))
		{
			Out = true;
			return true;
		}
		if (Value.Equals(TEXT("0")) || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("no"), ESearchCase::IgnoreCase))
		{
			Out = false;
			return true;
		}
		return false;
	}

	bool ParseDouble(const FString& Value, double& Out)
	{
		const std::string Narrow(TCHAR_TO_UTF8(*Value.TrimStartAndEnd()));
		if (Narrow.empty())
		{
			return false;
		}
		char* End = nullptr;
		Out = strtod(Narrow.c_str(), &End);
		return End != nullptr && *End == '\0';
	}

	bool ParseInteger(const FString& Value, long& Out)
	{
		const std::string Narrow(TCHAR_TO_UTF8(*Value.TrimStartAndEnd()));
		if (Narrow.empty())
		{
			return false;
		}
		char* End = nullptr;
		Out = strtol(Narrow.c_str(), &End, 10);
		return End != nullptr && *End == '\0';
	}
}

/**************************
// Decoder
**************************/
struct FSphinxDecoder::FImpl
{
	cmd_ln_t* Config = nullptr;
	ps_decoder_t* Decoder = nullptr;
	ESpeechRecognitionMode Mode = ESpeechRecognitionMode::VE_KEYWORD;
	int32 SampleRate = 16000;
	int32 FrameRate = 100;
	bool bUtteranceOpen = false;
	bool bSpeechStarted = false;
	bool bReportKeyPhrasesEarly = false;
	bool bKeepBestOfOverlappingKeyPhrases = true;
	bool bSettleAtStartUp = true;
	TArray<FString> RejectedPhrases;

	// What the loaded decoder was built from, and the search it is running
	FSphinxDecoderConfig Loaded;
	std::string ActiveSearch;

	// key phrases already reported in the current utterance
	TSet<FString> ReportedPhrases;

	// --- Start-up -------------------------------------------------------------
	// A new decoder still has to learn three things from the audio: how loud the
	// room is (its noise tracker), what silence looks like (its voice detector),
	// and the average spectrum of this voice on this microphone (the mean it
	// subtracts from every frame). Until it has:
	//  - the voice detector takes the first moments of any stream for speech;
	//  - the first real utterance is decoded against statistics that are a guess,
	//    or worse, the statistics of that false start.
	// So while the decoder is settling, each utterance is kept and, once heard in
	// full, played through the decoder again: by then the statistics describe that
	// very utterance. If it still yields nothing it is forgotten, and the
	// statistics go back to where they started. Settling ends with the first
	// utterance in which something is recognised.
	bool bSettling = false;
	int32 SettlingUtterances = 0;
	int64 SamplesFed = 0;
	bool bSpeechStartedEventPending = false;
	TArray<int16> KeptAudio;
	bool bKeptAudioTooLong = false;
	TArray<mfcc_t> StartupMean;

	// Audio kept from before the speech starts, so that a replay sees the same lead-in
	static constexpr float PreRollSeconds = 1.0f;
	// Beyond this an utterance is not kept (and not replayed)
	static constexpr float MaxKeptSeconds = 20.0f;
	// Speech "heard" this soon after the stream starts is most likely the voice detector finding its feet
	static constexpr float WarmUpSeconds = 0.3f;
	// Settling stops by itself after this many utterances with nothing recognised
	static constexpr int32 MaxSettlingUtterances = 6;

	cmn_t* GetNormalisation() const
	{
		feat_t* Features = (Decoder != nullptr) ? ps_get_feat(Decoder) : nullptr;
		cmn_t* Normalisation = (Features != nullptr) ? Features->cmn_struct : nullptr;
		const bool bUsable = Normalisation != nullptr && Normalisation->cmn_mean != nullptr && Normalisation->sum != nullptr && Normalisation->veclen > 0;
		return bUsable ? Normalisation : nullptr;
	}

	void RememberStartupNormalisation()
	{
		StartupMean.Reset();
		if (const cmn_t* Normalisation = GetNormalisation())
		{
			StartupMean.Append(Normalisation->cmn_mean, Normalisation->veclen);
		}
	}

	/** Puts the mean back to its start-up value and forgets every frame counted since. */
	void RestoreStartupNormalisation()
	{
		cmn_t* Normalisation = GetNormalisation();
		if (Normalisation != nullptr && StartupMean.Num() == Normalisation->veclen)
		{
			FMemory::Memcpy(Normalisation->cmn_mean, StartupMean.GetData(), (SIZE_T)StartupMean.Num() * sizeof(mfcc_t));
			FMemory::Memzero(Normalisation->sum, (SIZE_T)Normalisation->veclen * sizeof(mfcc_t));
			Normalisation->nframe = 0;
		}
	}

	void KeepForReplay(const int16* Samples, int32 NumSamples)
	{
		if (!bSettling || bKeptAudioTooLong)
		{
			return;
		}
		KeptAudio.Append(Samples, NumSamples);
		if (KeptAudio.Num() > (int32)(MaxKeptSeconds * (float)SampleRate))
		{
			bKeptAudioTooLong = true;
			KeptAudio.Empty();
		}
	}

	void TrimKeptAudioToPreRoll()
	{
		const int32 PreRoll = (int32)(PreRollSeconds * (float)SampleRate);
		if (KeptAudio.Num() > PreRoll)
		{
			KeptAudio.RemoveAt(0, KeptAudio.Num() - PreRoll);
		}
	}

	/** Decodes the kept utterance again, as one closed utterance. */
	bool Replay()
	{
		if (KeptAudio.Num() == 0 || ps_start_utt(Decoder) < 0)
		{
			return false;
		}
		ps_process_raw(Decoder, KeptAudio.GetData(), (size_t)KeptAudio.Num(), 0, 0);
		ps_end_utt(Decoder);
		return true;
	}

	void Release()
	{
		if (Decoder != nullptr)
		{
			if (bUtteranceOpen)
			{
				ps_end_utt(Decoder);
			}
			// ps_free releases the decoder's own reference to the config
			ps_free(Decoder);
			Decoder = nullptr;
		}
		if (Config != nullptr)
		{
			cmd_ln_free_r(Config);
			Config = nullptr;
		}
		bUtteranceOpen = false;
		bSpeechStarted = false;
		ActiveSearch.clear();
		Loaded = FSphinxDecoderConfig();
	}

	bool LookupWord(const FString& Word) const
	{
		if (Decoder == nullptr || Word.IsEmpty())
		{
			return false;
		}
		char* Phones = ps_lookup_word(Decoder, TCHAR_TO_UTF8(*Word));
		if (Phones == nullptr)
		{
			return false;
		}
		ckd_free(Phones);
		return true;
	}

	void ApplyParams(const TArray<FSphinxParam>& Params)
	{
		for (const FSphinxParam& Param : Params)
		{
			const std::string Name(TCHAR_TO_UTF8(*Param.Name));
			const int32 DeclaredType = FindDeclaredParamType(Name.c_str());
			if (DeclaredType == 0)
			{
				UE_LOG(SpeechRecognitionPlugin, Warning, TEXT("Ignoring unknown PocketSphinx parameter '%s'."), *Param.Name);
				continue;
			}

			// The value is written according to the type PocketSphinx declares for the
			// parameter, whatever type the caller named: the setters do not check, and
			// a float written into an integer slot is read back as garbage.
			if (DeclaredType & ARG_BOOLEAN)
			{
				bool bValue = false;
				if (ParseBoolean(Param.Value, bValue))
				{
					cmd_ln_set_boolean_r(Config, Name.c_str(), bValue);
					continue;
				}
			}
			else if (DeclaredType & ARG_FLOATING)
			{
				double Value = 0.0;
				if (ParseDouble(Param.Value, Value))
				{
					cmd_ln_set_float_r(Config, Name.c_str(), Value);
					continue;
				}
			}
			else if (DeclaredType & ARG_INTEGER)
			{
				long Value = 0;
				if (ParseInteger(Param.Value, Value))
				{
					cmd_ln_set_int_r(Config, Name.c_str(), Value);
					continue;
				}
			}
			else
			{
				cmd_ln_set_str_r(Config, Name.c_str(), TCHAR_TO_UTF8(*Param.Value));
				continue;
			}

			UE_LOG(SpeechRecognitionPlugin, Warning, TEXT("Ignoring PocketSphinx parameter '%s': '%s' is not a valid value for it."), *Param.Name, *Param.Value);
		}
	}

	/** Keeps the phrases whose every word is in the dictionary, and makes them the active search. */
	bool SetKeyPhrases(const TArray<FRecognitionPhrase>& Keywords, bool& bOutNoUsablePhrase)
	{
		bOutNoUsablePhrase = false;
		TArray<FString> Phrases;
		TArray<EPhraseRecognitionTolerance> Tolerances;

		for (const FRecognitionPhrase& Keyword : Keywords)
		{
			const FString Phrase = FSphinxDecoder::NormalisePhrase(Keyword.phrase);
			if (Phrase.IsEmpty())
			{
				continue;
			}

			TArray<FString> Words;
			Phrase.ParseIntoArrayWS(Words);

			bool bAllWordsKnown = true;
			for (const FString& Word : Words)
			{
				// "read(2)" names the second pronunciation of "read": both must exist
				const FString BaseWord = FSphinxDecoder::StripPronunciationSuffixes(Word);
				if (!LookupWord(BaseWord) || (Word != BaseWord && !LookupWord(Word)))
				{
					UE_LOG(SpeechRecognitionPlugin, Warning, TEXT("SKIPPED PHRASE: '%s' cannot be added, '%s' is not in the dictionary."), *Phrase, *Word);
					bAllWordsKnown = false;
					break;
				}
				if (Word == BaseWord && LookupWord(BaseWord + TEXT("(2)")))
				{
					UE_LOG(SpeechRecognitionPlugin, Verbose, TEXT("The word '%s' has several pronunciations; add one phrase per pronunciation (e.g. '%s(2)') to catch them all."), *BaseWord, *BaseWord);
				}
			}
			if (!bAllWordsKnown)
			{
				RejectedPhrases.AddUnique(Phrase);
				continue;
			}

			// a phrase listed twice keeps its last tolerance
			const int32 Existing = Phrases.IndexOfByKey(Phrase);
			if (Existing != INDEX_NONE)
			{
				Tolerances[Existing] = Keyword.tolerance;
			}
			else
			{
				Phrases.Add(Phrase);
				Tolerances.Add(Keyword.tolerance);
			}
		}

		if (Phrases.Num() == 0)
		{
			bOutNoUsablePhrase = true;
			return false;
		}

		TArray<std::string> NarrowPhrases;
		TArray<const char*> PhrasePointers;
		TArray<int32> Thresholds;
		NarrowPhrases.Reserve(Phrases.Num());
		for (int32 Index = 0; Index < Phrases.Num(); ++Index)
		{
			NarrowPhrases.Emplace(TCHAR_TO_UTF8(*Phrases[Index]));
			Thresholds.Add((int32)logmath_log(ps_get_logmath(Decoder), ToleranceToThreshold(Tolerances[Index])) >> SPHINX_SENSCR_SHIFT);
		}
		for (const std::string& Narrow : NarrowPhrases)
		{
			PhrasePointers.Add(Narrow.c_str());
		}

		// ps_set_keyphrase copies the strings and the thresholds
		if (ps_set_keyphrase(Decoder, "keyphrase_search", PhrasePointers.GetData(), Thresholds.GetData(), PhrasePointers.Num()) < 0)
		{
			return false;
		}
		return ps_set_search(Decoder, "keyphrase_search") >= 0;
	}

	void CollectPhrases(TArray<FRecognisedPhraseDetail>& OutPhrases) const
	{
		int32 BestScore = 0;
		if (ps_get_hyp(Decoder, &BestScore) == nullptr)
		{
			return;
		}

		for (ps_seg_t* Segment = ps_seg_iter(Decoder); Segment != nullptr; Segment = ps_seg_next(Segment))
		{
			const char* RawWord = ps_seg_word(Segment);
			if (RawWord == nullptr)
			{
				continue;
			}
			const FString Word(UTF8_TO_TCHAR(RawWord));
			if (FSphinxDecoder::IsFillerWord(Word))
			{
				continue;
			}

			int StartFrame = 0;
			int EndFrame = 0;
			ps_seg_frames(Segment, &StartFrame, &EndFrame);
			int32 AcousticScore = 0;
			int32 LanguageScore = 0;
			int32 Backoff = 0;
			const int32 Probability = ps_seg_prob(Segment, &AcousticScore, &LanguageScore, &Backoff);

			FRecognisedPhraseDetail Detail;
			Detail.Phrase = FSphinxDecoder::StripPronunciationSuffixes(Word);
			Detail.StartTime = (float)StartFrame / (float)FrameRate;
			Detail.EndTime = (float)(EndFrame + 1) / (float)FrameRate;
			// key phrase detections carry their score in the probability slot; the
			// other searches carry it in the acoustic score
			Detail.Score = (Mode == ESpeechRecognitionMode::VE_KEYWORD) ? Probability : AcousticScore;
			UE_LOG(SpeechRecognitionPlugin, Verbose, TEXT("Word: %s Start Time: %.3f End Time %.3f Score %d"), *Word, Detail.StartTime, Detail.EndTime, Detail.Score);
			OutPhrases.Add(MoveTemp(Detail));
		}

		// in time order; equal start times keep the decoder's order
		OutPhrases.StableSort([](const FRecognisedPhraseDetail& A, const FRecognisedPhraseDetail& B) { return A.StartTime < B.StartTime; });

		if (Mode == ESpeechRecognitionMode::VE_KEYWORD && bKeepBestOfOverlappingKeyPhrases)
		{
			// Two phrases cannot be said at the same time. When two detections share
			// most of their time span ("lights on" inside "lights off"), the decoder
			// heard one stretch of speech two ways: keep the way it scored higher.
			for (int32 First = 0; First < OutPhrases.Num(); ++First)
			{
				for (int32 Second = First + 1; Second < OutPhrases.Num(); )
				{
					const FRecognisedPhraseDetail& A = OutPhrases[First];
					const FRecognisedPhraseDetail& B = OutPhrases[Second];
					const float Overlap = FMath::Min(A.EndTime, B.EndTime) - FMath::Max(A.StartTime, B.StartTime);
					const float Shorter = FMath::Min(A.EndTime - A.StartTime, B.EndTime - B.StartTime);
					if (A.Phrase != B.Phrase && Overlap > 0.5f * Shorter)
					{
						if (B.Score > A.Score)
						{
							OutPhrases[First] = B;
						}
						OutPhrases.RemoveAt(Second);
						continue;
					}
					++Second;
				}
			}
		}

		if (Mode == ESpeechRecognitionMode::VE_KEYWORD)
		{
			// a phrase spotted several times in one utterance is reported once, at its first position
			TSet<FString> Seen;
			for (int32 Index = 0; Index < OutPhrases.Num(); )
			{
				bool bAlreadySeen = false;
				Seen.Add(OutPhrases[Index].Phrase, &bAlreadySeen);
				if (bAlreadySeen)
				{
					OutPhrases.RemoveAt(Index);
				}
				else
				{
					++Index;
				}
			}
		}
	}

	/** Reports the key phrases spotted so far in the utterance that have not been reported yet. */
	void ReportNewKeyPhrases(FSphinxDecoder::FEventCallback OnEvent)
	{
		TArray<FRecognisedPhraseDetail> Phrases;
		CollectPhrases(Phrases);
		Phrases.RemoveAll([this](const FRecognisedPhraseDetail& Phrase) { return ReportedPhrases.Contains(Phrase.Phrase); });
		if (Phrases.Num() == 0)
		{
			return;
		}
		if (bSpeechStartedEventPending)
		{
			bSpeechStartedEventPending = false;
			OnEvent(ESphinxDecoderEvent::SpeechStarted, TArray<FRecognisedPhraseDetail>());
		}
		for (const FRecognisedPhraseDetail& Phrase : Phrases)
		{
			ReportedPhrases.Add(Phrase.Phrase);
			UE_LOG(SpeechRecognitionPlugin, Verbose, TEXT("Phrases: %s "), *Phrase.Phrase);
		}
		OnEvent(ESphinxDecoderEvent::Recognised, Phrases);
	}

	void FinishUtterance(FSphinxDecoder::FEventCallback OnEvent)
	{
		TArray<FRecognisedPhraseDetail> Phrases;
		if (GSphinxLegacyHypothesisOrder)
		{
			CollectPhrases(Phrases);
			ps_end_utt(Decoder);
		}
		else
		{
			// closing the utterance runs the decoder's final pass: read the result after it
			ps_end_utt(Decoder);
			CollectPhrases(Phrases);

			if (bSettling && KeptAudio.Num() > 0)
			{
				// Closing the utterance has updated the statistics with it. Play it again
				// against them: always once when the first answer cannot be trusted (it
				// is empty, or it comes from a search that always answers something),
				// and once more if that still gives nothing.
				const bool bFirstAnswerTrusted = (Mode == ESpeechRecognitionMode::VE_KEYWORD) && (Phrases.Num() > 0 || ReportedPhrases.Num() > 0);
				for (int32 Replays = 0; Replays < GSphinxStartUpReplays && !bFirstAnswerTrusted; ++Replays)
				{
					if (Replays > 0 && Phrases.Num() > 0)
					{
						break;
					}
					if (!Replay())
					{
						break;
					}
					Phrases.Reset();
					CollectPhrases(Phrases);
				}
			}
		}
		bUtteranceOpen = false;
		bSpeechStarted = false;

		// what was already reported while the speaker was talking is not reported again
		const bool bReportedSomething = ReportedPhrases.Num() > 0;
		Phrases.RemoveAll([this](const FRecognisedPhraseDetail& Phrase) { return ReportedPhrases.Contains(Phrase.Phrase); });
		ReportedPhrases.Reset();
		const bool bRecognised = Phrases.Num() > 0 || bReportedSomething;

		if (bSettling)
		{
			++SettlingUtterances;
			if (bRecognised || SettlingUtterances >= MaxSettlingUtterances)
			{
				// from here on the decoder adapts by itself, utterance after utterance
				bSettling = false;
				KeptAudio.Empty();
			}
			else
			{
				// noise, or speech nobody asked for: leave no trace of it
				RestoreStartupNormalisation();
				KeptAudio.Reset();
				bKeptAudioTooLong = false;
			}
		}

		for (const FRecognisedPhraseDetail& Phrase : Phrases)
		{
			UE_LOG(SpeechRecognitionPlugin, Verbose, TEXT("Phrases: %s "), *Phrase.Phrase);
		}

		// The start of speech was not announced yet when the utterance began in the
		// stream's first moments. If nothing came of it, it was the voice detector
		// finding its feet: say nothing at all.
		const bool bFalseStart = bSpeechStartedEventPending && !bRecognised;
		if (!bFalseStart)
		{
			if (bSpeechStartedEventPending)
			{
				OnEvent(ESphinxDecoderEvent::SpeechStarted, TArray<FRecognisedPhraseDetail>());
			}
			if (Phrases.Num() > 0)
			{
				OnEvent(ESphinxDecoderEvent::Recognised, Phrases);
			}
			else if (!bReportedSomething)
			{
				OnEvent(ESphinxDecoderEvent::Unrecognised, Phrases);
			}
			OnEvent(ESphinxDecoderEvent::SpeechEnded, TArray<FRecognisedPhraseDetail>());
		}
		bSpeechStartedEventPending = false;

		if (ps_start_utt(Decoder) < 0)
		{
			UE_LOG(SpeechRecognitionPlugin, Warning, TEXT("Failed to start the next utterance."));
		}
		else
		{
			bUtteranceOpen = true;
		}
	}
};

FSphinxDecoder::FSphinxDecoder()
	: Impl(MakeUnique<FImpl>())
{
}

FSphinxDecoder::~FSphinxDecoder()
{
	Shutdown();
}

void FSphinxDecoder::Shutdown()
{
	Impl->Release();
}

bool FSphinxDecoder::IsInitialized() const
{
	return Impl->Decoder != nullptr;
}

int32 FSphinxDecoder::GetSampleRate() const
{
	return Impl->SampleRate;
}

const TArray<FString>& FSphinxDecoder::GetRejectedPhrases() const
{
	return Impl->RejectedPhrases;
}

bool FSphinxDecoder::IsWordInDictionary(const FString& Word) const
{
	return Impl->LookupWord(NormalisePhrase(Word));
}

bool FSphinxDecoder::ValidateFiles(const FSphinxDecoderConfig& InConfig, ESpeechRecognitionError& OutError, FString& OutMessage)
{
	auto Fail = [&](ESpeechRecognitionError Error, const FString& Message)
	{
		OutError = Error;
		OutMessage = Message;
		return false;
	};

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.FileExists(*(InConfig.AcousticModelDir / TEXT("mdef"))))
	{
		return Fail(ESpeechRecognitionError::ModelNotFound, FString::Printf(TEXT("No acoustic model in '%s' (the folder must contain the model's 'mdef' file)."), *InConfig.AcousticModelDir));
	}
	if (!FileManager.FileExists(*InConfig.DictionaryPath))
	{
		return Fail(ESpeechRecognitionError::DictionaryNotFound, FString::Printf(TEXT("Pronunciation dictionary not found: '%s'."), *InConfig.DictionaryPath));
	}
	if (InConfig.Mode == ESpeechRecognitionMode::VE_GRAMMAR && !FileManager.FileExists(*InConfig.GrammarPath))
	{
		return Fail(ESpeechRecognitionError::GrammarNotFound, FString::Printf(TEXT("Grammar file not found: '%s'."), *InConfig.GrammarPath));
	}
	if (InConfig.Mode == ESpeechRecognitionMode::VE_LANGUAGE_MODEL && !FileManager.FileExists(*InConfig.LanguageModelPath))
	{
		return Fail(ESpeechRecognitionError::LanguageModelNotFound, FString::Printf(TEXT("Language model not found: '%s'."), *InConfig.LanguageModelPath));
	}
	if (InConfig.Mode == ESpeechRecognitionMode::VE_KEYWORD && InConfig.Keywords.Num() == 0)
	{
		return Fail(ESpeechRecognitionError::NoValidKeywords, TEXT("Keyword mode needs at least one phrase."));
	}

	OutError = ESpeechRecognitionError::None;
	OutMessage.Reset();
	return true;
}

bool FSphinxDecoder::CanReuse(const FSphinxDecoderConfig& InConfig) const
{
	// The model, the dictionary and the parameters are what takes time to load, and
	// what the decoder has adapted its ear to. While they stay the same, another mode
	// or another list of phrases is only another search on the same decoder.
	const FSphinxDecoderConfig& Loaded = Impl->Loaded;
	if (Impl->Decoder == nullptr
		|| Loaded.AcousticModelDir != InConfig.AcousticModelDir
		|| Loaded.DictionaryPath != InConfig.DictionaryPath
		|| Loaded.bSettleAtStartUp != InConfig.bSettleAtStartUp
		|| Loaded.Params.Num() != InConfig.Params.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Loaded.Params.Num(); ++Index)
	{
		if (Loaded.Params[Index].Name != InConfig.Params[Index].Name || Loaded.Params[Index].Value != InConfig.Params[Index].Value)
		{
			return false;
		}
	}
	return true;
}

bool FSphinxDecoder::Initialize(const FSphinxDecoderConfig& InConfig, ESpeechRecognitionError& OutError, FString& OutMessage)
{
	const bool bReuse = CanReuse(InConfig);
	if (!bReuse)
	{
		Shutdown();
	}

	auto Fail = [&](ESpeechRecognitionError Error, const FString& Message)
	{
		Impl->Release();
		OutError = Error;
		OutMessage = Message;
		return false;
	};

	// Check the files first. The bundled PocketSphinx reports a missing model with a
	// line on stderr at best, and at worst ends the process.
	if (!ValidateFiles(InConfig, OutError, OutMessage))
	{
		Impl->Release();
		return false;
	}

	std::string ModelDir;
	std::string Dictionary;
	std::string SearchFile;
	auto UnsupportedPath = [&](const FString& Path)
	{
		return Fail(ESpeechRecognitionError::UnsupportedPath, FString::Printf(TEXT("The decoder cannot open '%s': the path contains characters outside this system's code page. Move the model to a folder with a plainer name."), *Path));
	};
	if (!bReuse && !ResolveNarrowPath(InConfig.AcousticModelDir, TEXT("mdef"), ModelDir))
	{
		return UnsupportedPath(InConfig.AcousticModelDir);
	}
	if (!bReuse && !ResolveNarrowPath(InConfig.DictionaryPath, nullptr, Dictionary))
	{
		return UnsupportedPath(InConfig.DictionaryPath);
	}
	if (InConfig.Mode == ESpeechRecognitionMode::VE_GRAMMAR && !ResolveNarrowPath(InConfig.GrammarPath, nullptr, SearchFile))
	{
		return UnsupportedPath(InConfig.GrammarPath);
	}
	if (InConfig.Mode == ESpeechRecognitionMode::VE_LANGUAGE_MODEL && !ResolveNarrowPath(InConfig.LanguageModelPath, nullptr, SearchFile))
	{
		return UnsupportedPath(InConfig.LanguageModelPath);
	}

	SphinxRuntime::ClearLastError();
	auto DecoderFailure = [&](const TCHAR* What)
	{
		const FString Detail = SphinxRuntime::GetLastError();
		return Fail(ESpeechRecognitionError::DecoderInitFailed, Detail.IsEmpty() ? FString(What) : FString::Printf(TEXT("%s (%s)"), What, *Detail));
	};

	if (!bReuse)
	{
		// Load the model. No search is named here: it is added below, the same way
		// whether the decoder is new or already running.
		Impl->Config = cmd_ln_init(nullptr, ps_args(), 1,
			"-hmm", ModelDir.c_str(),
			"-dict", Dictionary.c_str(),
			nullptr);
		if (Impl->Config == nullptr)
		{
			return DecoderFailure(TEXT("PocketSphinx rejected the configuration."));
		}

		Impl->ApplyParams(InConfig.Params);

		Impl->Decoder = ps_init(Impl->Config);
		if (Impl->Decoder == nullptr)
		{
			return DecoderFailure(TEXT("PocketSphinx could not load the model."));
		}

		Impl->SampleRate = (int32)cmd_ln_float32_r(Impl->Config, "-samprate");
		Impl->FrameRate = FMath::Max<int32>(1, (int32)cmd_ln_int32_r(Impl->Config, "-frate"));

		Impl->bSettleAtStartUp = InConfig.bSettleAtStartUp;
		Impl->RememberStartupNormalisation();
		// the legacy order reads the answer before the utterance is closed: nothing to replay against
		Impl->bSettling = Impl->bSettleAtStartUp && !GSphinxLegacyHypothesisOrder && GSphinxStartUpReplays > 0;
		Impl->SettlingUtterances = 0;
		Impl->SamplesFed = 0;
	}
	else if (Impl->bUtteranceOpen)
	{
		// a running decoder changes search between two utterances
		ps_end_utt(Impl->Decoder);
		Impl->bUtteranceOpen = false;
	}

	Impl->Loaded = InConfig;
	Impl->Mode = InConfig.Mode;
	Impl->bReportKeyPhrasesEarly = InConfig.bReportKeyPhrasesEarly && InConfig.Mode == ESpeechRecognitionMode::VE_KEYWORD;
	Impl->bKeepBestOfOverlappingKeyPhrases = InConfig.bKeepBestOfOverlappingKeyPhrases;
	Impl->RejectedPhrases.Reset();
	Impl->ReportedPhrases.Reset();
	Impl->bSpeechStarted = false;
	Impl->bSpeechStartedEventPending = false;
	Impl->bKeptAudioTooLong = false;
	Impl->KeptAudio.Reset();

	// The search: what the decoder listens for.
	const char* SearchName = nullptr;
	switch (InConfig.Mode)
	{
	case ESpeechRecognitionMode::VE_KEYWORD:
	{
		SearchName = "keyphrase_search";
		bool bNoUsablePhrase = false;
		if (!Impl->SetKeyPhrases(InConfig.Keywords, bNoUsablePhrase))
		{
			if (!bNoUsablePhrase)
			{
				return DecoderFailure(TEXT("PocketSphinx rejected the key phrases."));
			}
			// Release() keeps the list, so the caller can still say which phrases were dropped
			return Fail(ESpeechRecognitionError::NoValidKeywords, Impl->RejectedPhrases.Num() > 0
				? FString::Printf(TEXT("None of the key phrases can be used: each contains a word that is not in the dictionary (%s)."), *FString::Join(Impl->RejectedPhrases, TEXT(", ")))
				: FString(TEXT("Keyword mode needs at least one phrase.")));
		}
		break;
	}
	case ESpeechRecognitionMode::VE_GRAMMAR:
		SearchName = "grammar_search";
		if (ps_set_jsgf_file(Impl->Decoder, SearchName, SearchFile.c_str()) < 0 || ps_set_search(Impl->Decoder, SearchName) < 0)
		{
			return DecoderFailure(TEXT("PocketSphinx could not load the grammar."));
		}
		break;
	case ESpeechRecognitionMode::VE_LANGUAGE_MODEL:
		SearchName = "language_model_search";
		if (ps_set_lm_file(Impl->Decoder, SearchName, SearchFile.c_str()) < 0 || ps_set_search(Impl->Decoder, SearchName) < 0)
		{
			return DecoderFailure(TEXT("PocketSphinx could not load the language model."));
		}
		break;
	}

	// the search this one replaces is of no more use: a language model is tens of megabytes
	if (!Impl->ActiveSearch.empty() && Impl->ActiveSearch != SearchName)
	{
		ps_unset_search(Impl->Decoder, Impl->ActiveSearch.c_str());
	}
	Impl->ActiveSearch = SearchName;

	if (ps_start_utt(Impl->Decoder) < 0)
	{
		return DecoderFailure(TEXT("PocketSphinx could not start an utterance."));
	}
	Impl->bUtteranceOpen = true;
	return true;
}

void FSphinxDecoder::ProcessAudio(const int16* Samples, int32 NumSamples, FEventCallback OnEvent)
{
	if (Impl->Decoder == nullptr || !Impl->bUtteranceOpen || Samples == nullptr || NumSamples <= 0)
	{
		return;
	}

	const bool bWarmingUp = Impl->SamplesFed < (int64)(FImpl::WarmUpSeconds * (float)Impl->SampleRate);
	Impl->KeepForReplay(Samples, NumSamples);

	ps_process_raw(Impl->Decoder, Samples, (size_t)NumSamples, 0, 0);
	Impl->SamplesFed += NumSamples;
	const bool bInSpeech = ps_get_in_speech(Impl->Decoder) != 0;

	// transition from silence to listening
	if (bInSpeech && !Impl->bSpeechStarted)
	{
		Impl->bSpeechStarted = true;
		if (Impl->bSettling && bWarmingUp)
		{
			// too early to be believed: announced later, if something is recognised
			Impl->bSpeechStartedEventPending = true;
		}
		else
		{
			OnEvent(ESphinxDecoderEvent::SpeechStarted, TArray<FRecognisedPhraseDetail>());
		}
	}

	// nobody is speaking: of the audio kept for a replay, only the last moments matter
	if (!Impl->bSpeechStarted)
	{
		Impl->TrimKeptAudioToPreRoll();
	}

	// still talking: a key phrase can be handed over the moment it is spotted
	if (bInSpeech && Impl->bReportKeyPhrasesEarly)
	{
		Impl->ReportNewKeyPhrases(OnEvent);
	}

	// transition from listening to silence
	if (!bInSpeech && Impl->bSpeechStarted)
	{
		Impl->FinishUtterance(OnEvent);
	}
}

void FSphinxDecoder::EndStream(FEventCallback OnEvent)
{
	if (Impl->Decoder != nullptr && Impl->bUtteranceOpen && Impl->bSpeechStarted)
	{
		Impl->FinishUtterance(OnEvent);
	}
}

FString FSphinxDecoder::StripPronunciationSuffixes(const FString& Phrase)
{
	// removes every "(digits)" group
	FString Result;
	Result.Reserve(Phrase.Len());
	const int32 Length = Phrase.Len();
	for (int32 Index = 0; Index < Length; )
	{
		if (Phrase[Index] == TEXT('('))
		{
			int32 Close = Index + 1;
			while (Close < Length && FChar::IsDigit(Phrase[Close]))
			{
				++Close;
			}
			if (Close > Index + 1 && Close < Length && Phrase[Close] == TEXT(')'))
			{
				Index = Close + 1;
				continue;
			}
		}
		Result.AppendChar(Phrase[Index]);
		++Index;
	}
	return Result;
}

bool FSphinxDecoder::IsFillerWord(const FString& Word)
{
	return Word.IsEmpty()
		|| Word.StartsWith(TEXT("<"))
		|| Word.StartsWith(TEXT("++"))
		|| Word.StartsWith(TEXT("["))
		|| Word.Equals(TEXT("(NULL)"))
		|| Word.Equals(TEXT("sil"), ESearchCase::IgnoreCase);
}

FString FSphinxDecoder::NormalisePhrase(const FString& Phrase)
{
	// FText lower-cases with Unicode rules; FString::ToLower only knows A to Z
	const FString Lower = FText::FromString(Phrase).ToLower().ToString();
	TArray<FString> Words;
	Lower.ParseIntoArrayWS(Words);
	return FString::Join(Words, TEXT(" "));
}

int32 FSphinxDecoder::ComputeAveragePeak(const int16* Samples, int32 NumSamples)
{
	// average the positive peak of each half-wave in the buffer
	int32 PeakSum = 0;
	int32 PeakCount = 0;
	int16 RunningPeak = 0;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		if (Samples[Index] > RunningPeak)
		{
			RunningPeak = Samples[Index];
		}
		if (Samples[Index] < 0 && RunningPeak != 0)
		{
			// half-wave completed: record its peak, and reset
			PeakSum += RunningPeak;
			++PeakCount;
			RunningPeak = 0;
		}
	}
	return PeakCount > 0 ? PeakSum / PeakCount : 0;
}
