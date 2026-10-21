#pragma once

#include "CoreMinimal.h"
#include "SpeechRecognition.h"
#include "SpeechTranscriber.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

/** One PocketSphinx configuration value, for example "-vad_postspeech" = "10". */
struct FSphinxParam
{
	FString Name;
	ESpeechRecognitionParamType Type = ESpeechRecognitionParamType::VE_STRING;
	FString Value;
};

/** Everything the decoder needs to start: a model, a mode, and what that mode searches for. */
struct FSphinxDecoderConfig
{
	/** Folder holding the acoustic model (mdef, means, variances...). */
	FString AcousticModelDir;

	/** Pronunciation dictionary (.dict). */
	FString DictionaryPath;

	ESpeechRecognitionMode Mode = ESpeechRecognitionMode::VE_KEYWORD;

	/** Keyword mode: the phrases to spot. */
	TArray<FRecognitionPhrase> Keywords;

	/**
	 * Keyword mode: report a phrase as soon as it is spotted, instead of waiting for
	 * the speaker to pause. Faster, and blind to what is said next: "lights on" is
	 * reported before the decoder can hear that the sentence was "lights only".
	 */
	bool bReportKeyPhrasesEarly = false;

	/**
	 * Keyword mode: when two different phrases are spotted over the same stretch of
	 * speech, keep the one the decoder scored higher. Has no say over phrases that
	 * were already reported early.
	 */
	bool bKeepBestOfOverlappingKeyPhrases = true;

	/** Grammar mode: a JSGF grammar file. */
	FString GrammarPath;

	/** Language model mode: an n-gram model file. */
	FString LanguageModelPath;

	/** Extra PocketSphinx parameters. */
	TArray<FSphinxParam> Params;

	/**
	 * Take care of the first moments after start-up, when the decoder has not yet
	 * adapted to the room, the voice and the microphone: do not report the voice
	 * detector's false start at the head of the stream, and decode each utterance a
	 * second time until one is recognised. Without it the first phrase after
	 * start-up is the one most often missed.
	 */
	bool bSettleAtStartUp = true;

	/**
	 * Set for languages PocketSphinx has no model for: the worker then runs a
	 * transcribing decoder with this engine instead, and every field above but the
	 * mode and the key phrases is ignored.
	 */
	FSpeechTranscriberPtr Transcriber;
};

enum class ESphinxDecoderEvent : uint8
{
	/** The voice activity detector heard the start of speech. */
	SpeechStarted,
	/** The utterance ended and at least one phrase was recognised. */
	Recognised,
	/** The utterance ended and nothing was recognised. */
	Unrecognised,
	/** Always follows Recognised or Unrecognised. */
	SpeechEnded
};

/**
 * A PocketSphinx decoder without a thread or a microphone: give it 16-bit mono PCM,
 * get utterances back. The speech recognition worker drives one from the recording
 * device; tests and tools drive one from audio files.
 *
 * Not thread-safe. Create, use and destroy an instance on one thread.
 */
class SPEECHRECOGNITION_API FSphinxDecoder
{
public:
	using FEventCallback = TFunctionRef<void(ESphinxDecoderEvent Event, const TArray<FRecognisedPhraseDetail>& Phrases)>;

	FSphinxDecoder();
	~FSphinxDecoder();

	FSphinxDecoder(const FSphinxDecoder&) = delete;
	FSphinxDecoder& operator=(const FSphinxDecoder&) = delete;

	/**
	 * Loads the model and prepares the search.
	 *
	 * A decoder that is already running the same model, dictionary and parameters is
	 * kept: only what it listens for changes, in a few milliseconds, and what it has
	 * learnt about the room and the voice is not lost. Anything else is released
	 * and loaded afresh.
	 *
	 * @return false with an error code and a message a user can act on; nothing is
	 * left initialised then.
	 */
	bool Initialize(const FSphinxDecoderConfig& Config, ESpeechRecognitionError& OutError, FString& OutMessage);

	/** Whether Initialize with this configuration would keep the running decoder. */
	bool CanReuse(const FSphinxDecoderConfig& Config) const;

	/**
	 * Checks that the files the configuration names exist, without loading any of them.
	 * Cheap enough to call on the game thread before handing the configuration to a worker.
	 */
	static bool ValidateFiles(const FSphinxDecoderConfig& Config, ESpeechRecognitionError& OutError, FString& OutMessage);

	/** Releases the decoder. Safe to call twice. */
	void Shutdown();

	bool IsInitialized() const;

	/** Sample rate the model expects, in Hz (16000 for the stock models). */
	int32 GetSampleRate() const;

	/** Key phrases dropped by the last Initialize because a word is not in the dictionary. */
	const TArray<FString>& GetRejectedPhrases() const;

	/** Feeds audio. OnEvent is called zero or more times before this returns. */
	void ProcessAudio(const int16* Samples, int32 NumSamples, FEventCallback OnEvent);

	/** Closes an utterance that is still open, as when an audio file ends mid-speech. */
	void EndStream(FEventCallback OnEvent);

	/** Whether the pronunciation dictionary knows this word. */
	bool IsWordInDictionary(const FString& Word) const;

	/** Removes pronunciation suffixes: "read(2) the book" becomes "read the book". */
	static FString StripPronunciationSuffixes(const FString& Phrase);

	/** True for the decoder's own tokens, such as <sil>, </s> or ++noise++. */
	static bool IsFillerWord(const FString& Word);

	/** Lower-cases with Unicode rules, so that accented and non-Latin capitals match the dictionary's lower-case entries. */
	static FString NormalisePhrase(const FString& Phrase);

	/** Mean of the positive half-wave peaks in a buffer: a cheap loudness figure, 0 to 32767. */
	static int32 ComputeAveragePeak(const int16* Samples, int32 NumSamples);

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
