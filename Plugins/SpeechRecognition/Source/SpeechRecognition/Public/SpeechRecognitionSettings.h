#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SpeechRecognition.h"
#include "SpeechRecognitionSettings.generated.h"

/**
 * Project Settings > Plugins > Speech Recognition.
 *
 * A language is a folder. For the code "en" the plugin expects, under the model folder:
 *
 *   en/en/                       the acoustic model (mdef, means, variances...)
 *   en/en.dict                   the pronunciation dictionary
 *   en/grammars/<name>.gram      JSGF grammars, for grammar mode
 *   en/language_models/<name>.lm n-gram models, for language model mode
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Speech Recognition"))
class SPEECHRECOGNITION_API USpeechRecognitionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Folder holding one sub-folder per language: relative to the project's Content folder, or absolute. */
	UPROPERTY(config, EditAnywhere, Category = "Models")
	FString ModelDirectory = TEXT("model");

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override;
	//~ End UDeveloperSettings

	/** The model folder as an absolute path. */
	static FString GetModelRoot();

	/** Folder name used for one of the languages in ESpeechRecognitionLanguage. */
	static FString GetLanguageCode(ESpeechRecognitionLanguage Language);

	/** Codes of the languages that have both an acoustic model and a dictionary under the model folder. */
	static TArray<FString> FindInstalledLanguages();

	static FString GetAcousticModelDir(const FString& LanguageCode);
	static FString GetDictionaryPath(const FString& LanguageCode);
	static FString GetGrammarPath(const FString& LanguageCode, const FString& GrammarName);

	/** Looks for <name>.lm, then <name>.lm.bin. Returns the .lm path when neither exists. */
	static FString GetLanguageModelPath(const FString& LanguageCode, const FString& ModelName);
};
