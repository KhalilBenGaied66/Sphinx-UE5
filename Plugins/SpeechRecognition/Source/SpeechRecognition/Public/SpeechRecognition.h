#pragma once

#include "CoreMinimal.h"
#include "SpeechRecognition.generated.h"

//General Log (defined in SpeechRecognitionModule.cpp)
SPEECHRECOGNITION_API DECLARE_LOG_CATEGORY_EXTERN(SpeechRecognitionPlugin, Log, All);

//Common structures and enumerations
USTRUCT(BlueprintType)
struct FRecognisedPhrases
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Audio|SpeechRecognition")
	TArray<FString> phrases;

	// default constructor
	FRecognisedPhrases() {
	}
};

/** One recognised phrase, with where it sat in the utterance and how the decoder scored it. */
USTRUCT(BlueprintType)
struct FRecognisedPhraseDetail
{
	GENERATED_USTRUCT_BODY()

	/** The phrase, lower case, without pronunciation suffixes such as "(2)". */
	UPROPERTY(BlueprintReadOnly, Category = "Audio|SpeechRecognition")
	FString Phrase;

	/** Seconds from the start of the utterance to the start of the phrase. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio|SpeechRecognition")
	float StartTime = 0.f;

	/** Seconds from the start of the utterance to the end of the phrase. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio|SpeechRecognition")
	float EndTime = 0.f;

	/**
	 * The decoder's raw log score for this phrase. Higher is better, and it is only
	 * comparable between phrases recognised in the same mode: it is not a probability.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Audio|SpeechRecognition")
	int32 Score = 0;

	/**
	 * True when Phrase is what a transcribing engine wrote (languages without a
	 * PocketSphinx model, such as Tunisian Darija), not one of the listed phrases.
	 * Voice command components then match it against their phrases, put the phrase
	 * that matched in Matched Phrase and the match score (0-100) in Score.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Audio|SpeechRecognition")
	bool bIsTranscript = false;

	/** Transcripts: the listed phrase the transcript was matched to. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio|SpeechRecognition")
	FString MatchedPhrase;

	/** Transcripts: seconds the engine took, from the end of the utterance. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio|SpeechRecognition")
	float ProcessingSeconds = 0.f;

	/**
	 * Transcripts from an engine that scores phrases: how well each phrase being
	 * listened for fits the audio, in nats per character (0 best, below -3 poor).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Audio|SpeechRecognition")
	TMap<FString, float> PhraseScores;

	/** Phrase scoring: the score of Matched Phrase, in nats per character. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio|SpeechRecognition")
	float PhraseScore = 0.f;
};

UENUM(BlueprintType)
enum class ESpeechRecognitionMode : uint8
{
	VE_KEYWORD 	UMETA(DisplayName = "Keyword Spotting"),
	VE_GRAMMAR  UMETA(DisplayName = "Grammar"),
	VE_LANGUAGE_MODEL UMETA(DisplayName = "Language Model")
};

UENUM(BlueprintType)
enum class ESpeechRecognitionParamType : uint8
{
	VE_FLOAT 	UMETA(DisplayName = "Float"),
	VE_BOOLEAN  UMETA(DisplayName = "Boolean"),
	VE_STRING   UMETA(DisplayName = "String"),
	VE_INTEGER  UMETA(DisplayName = "Integer")
};

/**
 * The languages the plugin has always listed. Any other language works too: put its
 * model under the model folder and call InitWithLanguageCode with the folder's name.
 */
UENUM(BlueprintType)
enum class ESpeechRecognitionLanguage : uint8
{
	VE_English 	UMETA(DisplayName = "English"),
	VE_Chinese  UMETA(DisplayName = "Chinese"),
	VE_French	UMETA(DisplayName = "French"),
	VE_Spanish  UMETA(DisplayName = "Spanish"),
	VE_Russian  UMETA(DisplayName = "Russian"),
	VE_German   UMETA(DisplayName = "German")
};

/** How easily a key phrase triggers. V1 triggers most easily (more false alarms), V10 is the strictest. */
UENUM(BlueprintType)
enum class EPhraseRecognitionTolerance : uint8
{
	VE_1 	UMETA(DisplayName = "V1"),
	VE_2 	UMETA(DisplayName = "V2"),
	VE_3 	UMETA(DisplayName = "V3"),
	VE_4 	UMETA(DisplayName = "V4"),
	VE_5 	UMETA(DisplayName = "V5"),
	VE_6 	UMETA(DisplayName = "V6"),
	VE_7 	UMETA(DisplayName = "V7"),
	VE_8 	UMETA(DisplayName = "V8"),
	VE_9 	UMETA(DisplayName = "V9"),
	VE_10 	UMETA(DisplayName = "V10")
};

/** Where the recogniser is in its life. */
UENUM(BlueprintType)
enum class ESpeechRecognitionState : uint8
{
	/** Init has not been called, or Shutdown has. */
	Stopped,
	/** Init succeeded; no recognition mode has been enabled yet. */
	Initialised,
	/** A mode was requested; the model is loading and the microphone is being opened. */
	Starting,
	/** The microphone is open and speech is being decoded. */
	Listening,
	/** The last mode request failed. See the error; enabling a mode again retries. */
	Failed
};

/** Why recognition could not start, or stopped. */
UENUM(BlueprintType)
enum class ESpeechRecognitionError : uint8
{
	None,
	/** SphinxBase.dll or PocketSphinx.dll could not be loaded. */
	LibrariesMissing,
	/** Init has not been called. */
	NotInitialised,
	/** No acoustic model folder for the language. */
	ModelNotFound,
	/** No pronunciation dictionary for the language. */
	DictionaryNotFound,
	/** The grammar file does not exist. */
	GrammarNotFound,
	/** The language model file does not exist. */
	LanguageModelNotFound,
	/** A model path contains characters the decoder cannot open. */
	UnsupportedPath,
	/** No key phrase is left once those with words missing from the dictionary are removed. */
	NoValidKeywords,
	/** The decoder refused the model, the grammar or the configuration. */
	DecoderInitFailed,
	/** The recording device could not be opened or read. */
	AudioDeviceFailed,
	/** A language without a PocketSphinx model has no transcription model or service either. */
	TranscriberUnavailable
};

USTRUCT(BlueprintType)
struct FRecognitionPhrase
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|SpeechRecognition")
	FString phrase;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|SpeechRecognition")
	EPhraseRecognitionTolerance tolerance;

	// default constructor
	FRecognitionPhrase() {
		phrase = FString();
		tolerance = EPhraseRecognitionTolerance::VE_1;
	}

	// if you wish to only provide a phrase
	FRecognitionPhrase(const FString& keyword) {
		phrase = keyword;
		tolerance = EPhraseRecognitionTolerance::VE_5;
	}

	// if you wish to specify both a phrase, and a tolerance setting
	FRecognitionPhrase(const FString& InPhrase, EPhraseRecognitionTolerance InTolerance) {
		phrase = InPhrase;
		tolerance = InTolerance;
	}
};
