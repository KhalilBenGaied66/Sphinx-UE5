#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VoiceCommandComponent.h"
#include "VoiceCommandSet.generated.h"

/**
 * Voice commands saved as an asset, in one language: Content Browser > Add >
 * Miscellaneous > Data Asset > Voice Command Set.
 *
 * Give sets to a Voice Command component (Command Sets). It listens for the sets
 * of its current language, so one actor can hold a set per language and switch
 * with Set Language. A set can be shared by any number of actors, and edited
 * without touching them.
 */
UCLASS(BlueprintType)
class SPEECHRECOGNITION_API UVoiceCommandSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The language the phrases are written for. Custom phrases (any) takes any phrase in any alphabet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice Command", meta = (GetOptions = "SpeechRecognition.SpeechRecognitionSettings.GetLanguageOptions"))
	FString Language = TEXT("en");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice Command", meta = (TitleProperty = "Command"))
	TArray<FVoiceCommand> Commands;

	/** Whether the set is for this language code. Case is ignored. */
	UFUNCTION(BlueprintPure, Category = "Voice Command")
	bool IsForLanguage(const FString& LanguageCode) const;
};
