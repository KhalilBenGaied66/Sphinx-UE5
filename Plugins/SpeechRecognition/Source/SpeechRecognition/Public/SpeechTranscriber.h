#pragma once

#include "CoreMinimal.h"

/** What a transcriber made of one utterance. */
struct FSpeechTranscription
{
	/** What was said, as the engine wrote it. Empty when nothing intelligible was heard. */
	FString Text;

	/** Set when the engine failed (no network, quota, broken model); Text is empty then. */
	FString Error;

	/** Wall-clock time the engine took, in seconds. */
	double Seconds = 0.0;

	/**
	 * Engines that can score phrases (FPhraseScorer): how well each phrase fits the
	 * audio, in nats per character, 0 best. Empty for engines that only write text.
	 */
	TMap<FString, float> PhraseScores;
};

/**
 * Turns one utterance into text, for the languages PocketSphinx has no model for
 * (Tunisian Darija, for one). The transcribing decoder detects the utterance and
 * hands it over; voice command components match the text against their phrases.
 *
 * Transcribe is called from a background thread, never twice at once on the same
 * object.
 */
class SPEECHRECOGNITION_API ISpeechTranscriber
{
public:
	virtual ~ISpeechTranscriber() = default;

	/**
	 * Audio is 16 kHz mono, -1 to 1. Phrases are those being listened for: an engine
	 * that can score them fills PhraseScores, the others ignore them.
	 */
	virtual FSpeechTranscription Transcribe(TConstArrayView<float> Audio, TConstArrayView<FString> Phrases) = 0;

	/** One line for logs: engine, model, where it runs. */
	virtual FString Describe() const = 0;
};

using FSpeechTranscriberPtr = TSharedPtr<ISpeechTranscriber, ESPMode::ThreadSafe>;
