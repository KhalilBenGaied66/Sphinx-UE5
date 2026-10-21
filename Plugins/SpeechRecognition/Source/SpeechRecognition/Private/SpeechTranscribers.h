#pragma once

#include "CoreMinimal.h"
#include "SpeechTranscriber.h"

/** The engines behind the transcribing decoder. */
namespace SpeechTranscribers
{
	/**
	 * Omnilingual ASR (Meta, Apache-2.0), a CTC model exported to ONNX, run on the
	 * CPU through Unreal's Neural Network Engine and its ONNX Runtime back end.
	 * Offline; the model ships with the game. Folder holds model.int8.onnx (or
	 * model.onnx) and tokens.txt.
	 *
	 * Call on the game thread: it loads a few hundred megabytes and creates the
	 * model, which takes a second or two.
	 */
	FSpeechTranscriberPtr CreateOmnilingual(const FString& Folder, FString& OutError);

	/**
	 * Gemini, through Google's API. Online, and for development only: the key is
	 * read from the GEMINI_API_KEY environment variable and must never ship inside
	 * a game. The free tier allows a small number of requests a day per model.
	 */
	FSpeechTranscriberPtr CreateGemini(const FString& LanguageCode, const FString& Model, FString& OutError);

	/** model.int8.onnx or model.onnx in Folder, whichever exists, int8 first; empty when neither does. */
	FString FindOnnxModel(const FString& Folder);
}
