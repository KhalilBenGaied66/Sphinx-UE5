#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SpeechRecognition.h"
#include "SpeechRecognitionSettings.generated.h"

/** What transcribes the languages PocketSphinx has no model for. */
UENUM(BlueprintType)
enum class ESpeechTranscriptionEngine : uint8
{
	/** A model in the language's folder, run on the CPU by Unreal's Neural Network Engine. Offline; ships with the game. */
	LocalModel,
	/** Google's Gemini API. Online, needs GEMINI_API_KEY in the environment: for development and for comparing, never for a shipped game. */
	Gemini
};

/**
 * Project Settings > Plugins > Speech Recognition.
 *
 * A language is a folder. For the code "en" the plugin expects, under the model folder:
 *
 *   en/en/                       the acoustic model (mdef, means, variances...)
 *   en/en.dict                   the pronunciation dictionary
 *   en/grammars/<name>.gram      JSGF grammars, for grammar mode
 *   en/language_models/<name>.lm n-gram models, for language model mode
 *
 * The second engine, for what PocketSphinx has no model for, is one shared folder:
 *
 *   omnilingual/model.int8.onnx  Omnilingual ASR, exported to ONNX (or model.onnx)
 *   omnilingual/tokens.txt       its vocabulary
 *
 * It serves the language code "any" (any phrase in any alphabet, invented words
 * included) and every Arabic code: "ar" (Modern Standard Arabic), "aeb"
 * (Tunisian), "ary" (Moroccan), "arz" (Egyptian), "apc" (Levantine)... A code's
 * own folder (aeb/model.int8.onnx) takes precedence. Voice command components
 * score their phrases against what the model heard. With the Gemini engine no
 * folder is needed.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Speech Recognition"))
class SPEECHRECOGNITION_API USpeechRecognitionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Folder holding one sub-folder per language: relative to the project's Content folder, or absolute. */
	UPROPERTY(config, EditAnywhere, Category = "Models")
	FString ModelDirectory = TEXT("model");

	/**
	 * For languages without a PocketSphinx model. The command line overrides it:
	 * -SpeechTranscriber=gemini or -SpeechTranscriber=local.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Transcription")
	ESpeechTranscriptionEngine TranscriptionEngine = ESpeechTranscriptionEngine::LocalModel;

	/** Gemini model used by the Gemini engine. The free tier's daily quota is per model. */
	UPROPERTY(config, EditAnywhere, Category = "Transcription")
	FString GeminiModel = TEXT("gemini-3.6-flash");

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override;
	//~ End UDeveloperSettings

	/** The model folder as an absolute path. */
	static FString GetModelRoot();

	/** Folder name used for one of the languages in ESpeechRecognitionLanguage. */
	static FString GetLanguageCode(ESpeechRecognitionLanguage Language);

	/** Codes of the languages that have a PocketSphinx model (acoustic model and dictionary) or a transcription model under the model folder. */
	static TArray<FString> FindInstalledLanguages();

	/** The language's folder under the model folder. */
	static FString GetLanguageDir(const FString& LanguageCode);

	/** Whether the language has a PocketSphinx acoustic model and dictionary. */
	static bool HasSphinxModel(const FString& LanguageCode);

	/** Whether a transcription model is installed for the language (its own folder, or the shared one). */
	static bool HasTranscriptionModel(const FString& LanguageCode);

	/** The folder of the language's transcription model: its own if it has one, else the shared "omnilingual" folder for "any" and the Arabic codes. */
	static FString GetTranscriptionModelDir(const FString& LanguageCode);

	/** "ar", "ar-TN", "arb", "aeb", "ary", "arz", "apc"...: the codes of Arabic and its varieties. */
	static bool IsArabicCode(const FString& LanguageCode);

	/** "any": no language at all. Phrases are recognised as they are written, in any alphabet, invented words included. */
	static bool IsAnyLanguageCode(const FString& LanguageCode);

	/** The codes the shared Omnilingual model serves: "any" and the Arabic codes. */
	static bool UsesSharedModel(const FString& LanguageCode);

	/** The project's setting, unless the command line names another engine. */
	static ESpeechTranscriptionEngine GetTranscriptionEngine();

	/**
	 * Whether the language is transcribed rather than decoded by PocketSphinx: it has
	 * no PocketSphinx model, and it has a transcription model, or the Gemini engine is
	 * selected, or it is "any" or an Arabic code. A code with none of these is a
	 * PocketSphinx language whose model is missing.
	 */
	static bool IsTranscribedLanguage(const FString& LanguageCode);

	static FString GetAcousticModelDir(const FString& LanguageCode);
	static FString GetDictionaryPath(const FString& LanguageCode);
	static FString GetGrammarPath(const FString& LanguageCode, const FString& GrammarName);

	/** Looks for <name>.lm, then <name>.lm.bin. Returns the .lm path when neither exists. */
	static FString GetLanguageModelPath(const FString& LanguageCode, const FString& ModelName);
};
