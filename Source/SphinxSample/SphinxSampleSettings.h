#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VoiceCommandComponent.h"
#include "SphinxSampleSettings.generated.h"

/** One list of the sample's commands: a name for the screen, a language, the commands. */
USTRUCT()
struct FSphinxSampleLanguage
{
	GENERATED_BODY()

	/** Shown on screen when L switches to this list. */
	UPROPERTY(Config, EditAnywhere, Category = "Commands")
	FString Name;

	/** The recogniser's language for these phrases: an installed language, or Custom phrases (any). */
	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (GetOptions = "SpeechRecognition.SpeechRecognitionSettings.GetLanguageOptions"))
	FString Language = TEXT("any");

	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (TitleProperty = "Command"))
	TArray<FVoiceCommand> Commands;
};

/**
 * The sample's commands, in Edit > Project Settings > Game > Sphinx Sample. Type
 * a phrase, press Play: no C++, no rebuild. Changes are saved to
 * Config/DefaultGame.ini, and a running game picks them up the next time L
 * switches to that list.
 *
 * L goes through the lists in order; add one (a name, a language, commands) and
 * L reaches it. The cube knows six commands: Forward, Back, Left, Right, Jump,
 * Stop. A command with any other name ("Patronus") makes it spin once, and the
 * screen shows the name, so a new command can be tried before writing what it
 * does.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Sphinx Sample"))
class USphinxSampleSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USphinxSampleSettings();

	/** The lists L goes through. The first one is used at start, unless -SpeechLanguage= names another (by language code or name). */
	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (TitleProperty = "Name"))
	TArray<FSphinxSampleLanguage> Languages;

	/** The list -SpeechLanguage= names: its language code or its name, case ignored. INDEX_NONE if there is none. */
	int32 FindLanguage(const FString& CodeOrName) const;
};
