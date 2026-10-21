#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "HAL/CriticalSection.h"
#include "SphinxDecoder.h"
#include "SpeechTranscriber.h"

/**
 * The decoder used for languages PocketSphinx has no model for. Same contract as
 * FSphinxDecoder: give it 16 kHz 16-bit mono PCM, get utterance events back.
 *
 * An energy detector finds each utterance (with some audio from before it, so
 * the first syllable is not cut). When it ends, the utterance is handed to a
 * transcriber on a background task, so that the microphone keeps being read
 * while a model or a web service works. The text comes back as a single
 * recognised phrase marked as a transcript, which voice command components match
 * against their own phrases.
 *
 * Not thread-safe. Create, use and destroy an instance on one thread.
 */
class SPEECHRECOGNITION_API FTranscribingDecoder
{
public:
	using FEventCallback = FSphinxDecoder::FEventCallback;

	struct FSettings
	{
		/** Audio kept from before the detector fires. */
		float PreRollSeconds = 0.3f;
		/** Silence that ends an utterance. */
		float HangoverSeconds = 0.45f;
		/** Shorter bursts are dropped as clicks and coughs. */
		float MinSpeechSeconds = 0.2f;
		/** A longer utterance is cut and transcribed as it is. */
		float MaxSpeechSeconds = 6.f;
		/** How far above the noise floor a frame must be to count as speech. */
		float StartAboveNoiseDb = 12.f;
		float StayAboveNoiseDb = 6.f;
		/** Frames quieter than this are never speech, whatever the noise floor. */
		float MinSpeechDbfs = -50.f;
	};

	FTranscribingDecoder();
	~FTranscribingDecoder();

	FTranscribingDecoder(const FTranscribingDecoder&) = delete;
	FTranscribingDecoder& operator=(const FTranscribingDecoder&) = delete;

	/** Takes the transcriber from the configuration. False when it has none. */
	bool Initialize(const FSphinxDecoderConfig& Config, ESpeechRecognitionError& OutError, FString& OutMessage);

	/** Waits for a transcription still running, then lets the transcriber go. Safe to call twice. */
	void Shutdown();

	bool IsInitialized() const;

	static constexpr int32 SampleRate = 16000;

	/** Feeds audio, and reports transcriptions that finished since the last call. */
	void ProcessAudio(const int16* Samples, int32 NumSamples, FEventCallback OnEvent);

	/** Closes an utterance that is still open and waits for every transcription. For files and tests. */
	void EndStream(FEventCallback OnEvent);

	FSettings Settings;

private:
	struct FPending
	{
		TFuture<FSpeechTranscription> Result;
		float AudioSeconds = 0.f;
	};

	void ProcessFrame(const int16* Frame, FEventCallback OnEvent);
	void FinishUtterance(FEventCallback OnEvent);
	void Report(const FSpeechTranscription& Transcription, float AudioSeconds, FEventCallback OnEvent);
	void DeliverFinished(bool bWait, FEventCallback OnEvent);

	FSpeechTranscriberPtr Transcriber;
	// what is being listened for, for engines that score phrases
	TArray<FString> Phrases;

	// 20 ms frames
	static constexpr int32 FrameSamples = 320;
	TArray<int16> FrameBuffer;

	TArray<int16> PreRoll;
	TArray<int16> Utterance;
	bool bInSpeech = false;
	int32 LoudFrames = 0;
	int32 QuietFrames = 0;
	float NoiseFloorDb = -60.f;

	TArray<FPending> Pending;
	TSharedPtr<FCriticalSection, ESPMode::ThreadSafe> TranscribeLock;
};
