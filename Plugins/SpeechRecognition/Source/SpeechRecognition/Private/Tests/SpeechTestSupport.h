#pragma once

#include "CoreMinimal.h"
#include "SphinxDecoder.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Shared by the automated tests and the benchmark: test audio, its manifest, and file decoding. */
namespace SpeechTest
{
	/** One row of Resources/TestAudio/utterances.tsv. */
	struct FUtterance
	{
		FString Id;
		FString Language;
		/** "recorded" (a person) or "synthetic" (a Windows text-to-speech voice). */
		FString Source;
		/** Path of the audio file, absolute. */
		FString File;
		/** Key phrase the utterance contains, or empty when it contains none. */
		FString Expected;
		/** True when the text is a sentence of grammars/en_move.gram. */
		bool bGrammarSentence = false;
		/** What is said. */
		FString Text;
	};

	/** What came out of decoding one audio file. */
	struct FDecodeResult
	{
		TArray<FRecognisedPhraseDetail> Phrases;
		int32 SpeechStartedEvents = 0;
		int32 RecognisedEvents = 0;
		int32 UnrecognisedEvents = 0;
		int32 SpeechEndedEvents = 0;

		/** Length of the audio that was fed, padding included. */
		double AudioSeconds = 0.0;
		/** Wall-clock time spent decoding it. */
		double DecodeSeconds = 0.0;

		/** For each entry of Phrases: how many seconds of the file had been fed when it was reported. */
		TArray<double> ReportTimes;
		/** Where the speech starts and ends in the file, judged by amplitude. */
		double SpeechStartSeconds = 0.0;
		double SpeechEndSeconds = 0.0;

		FString Joined() const;
		bool Contains(const FString& Phrase) const;

		/**
		 * Audio time between the end of the speech and the report of Phrase. Negative
		 * when the phrase was reported while the speaker was still talking.
		 */
		bool GetReportLag(const FString& Phrase, double& OutSeconds) const;
	};

	/** Plugins/SpeechRecognition/Resources/TestAudio */
	FString GetAudioRoot();

	/** Reads a .wav (16-bit PCM mono) or a headerless .raw (16-bit, 16 kHz, mono). */
	bool LoadPcm16(const FString& Path, TArray<int16>& OutSamples, int32& OutSampleRate, FString& OutError);

	bool LoadUtterances(TArray<FUtterance>& OutUtterances);
	bool LoadCommands(const FString& Language, TArray<FRecognitionPhrase>& OutCommands);

	/** A configuration for an installed language, with the parameters Init applies by default. */
	FSphinxDecoderConfig MakeConfig(const FString& Language, ESpeechRecognitionMode Mode);

	bool IsLanguageInstalled(const FString& Language);

	/**
	 * Feeds a file to the decoder the way the worker feeds the microphone: 1024
	 * samples at a time, between a lead-in and a tail of near-silence.
	 */
	FDecodeResult DecodeSamples(FSphinxDecoder& Decoder, const TArray<int16>& Samples, int32 SampleRate, float LeadInSeconds = 0.5f);
	bool DecodeFile(FSphinxDecoder& Decoder, const FString& Path, FDecodeResult& OutResult, FString& OutError, float LeadInSeconds = 0.5f);
}

#endif // WITH_DEV_AUTOMATION_TESTS
