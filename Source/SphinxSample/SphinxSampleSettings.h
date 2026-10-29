#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VoiceCommandComponent.h"
#include "SphinxSampleSettings.generated.h"

/**
 * The sample's commands, one list per language, in Edit > Project Settings >
 * Game > Sphinx Sample. Type a phrase, press Play: no C++, no rebuild. Changes
 * are saved to Config/DefaultGame.ini, and a running game picks them up the next
 * time L switches to that language.
 *
 * The cube knows six commands: Forward, Back, Left, Right, Jump, Stop. A command
 * with any other name ("Patronus") makes it spin once, and the screen shows the
 * name, so a new command can be tried before writing what it does.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Sphinx Sample"))
class USphinxSampleSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USphinxSampleSettings();

	/** en: PocketSphinx. Every word must be in the English dictionary; the screen lists those that are not. */
	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (DisplayName = "English (en)"))
	TArray<FVoiceCommand> English;

	/** aeb: the second engine. Arabic script or Latin letters, written as it sounds. */
	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (DisplayName = "Tunisian Darija (aeb)"))
	TArray<FVoiceCommand> Tunisian;

	/** ar: the second engine. */
	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (DisplayName = "Modern Standard Arabic (ar)"))
	TArray<FVoiceCommand> Arabic;

	/**
	 * any: the second engine, with no language. Invented words, names, mixed
	 * languages, written as they sound ("expecto patronum"). Phrases of two or
	 * three words are told apart more reliably than single short ones.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (DisplayName = "Spells (any)"))
	TArray<FVoiceCommand> Spells;

	/** The list for a language code: aeb, ar, any; anything else gets English. */
	const TArray<FVoiceCommand>& CommandsFor(const FString& LanguageCode) const;
};
