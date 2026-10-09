#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SpeechRecognition.h"
#include "UObject/Object.h"
#include "SpeechRecognitionTestListener.generated.h"

/** Records the events it is bound to, so that the automated tests can check what was broadcast. */
UCLASS()
class USpeechRecognitionTestListener : public UObject
{
	GENERATED_BODY()

public:
	TArray<FName> Commands;
	TArray<ESpeechRecognitionError> Failures;
	int32 ListeningStartedCount = 0;

	UFUNCTION()
	void HandleVoiceCommand(FName Command, FGameplayTag Tag, const FRecognisedPhraseDetail& Phrase)
	{
		Commands.Add(Command);
	}

	UFUNCTION()
	void HandleRecognitionFailed(ESpeechRecognitionError Error, const FString& Message)
	{
		Failures.Add(Error);
	}

	UFUNCTION()
	void HandleListeningStarted()
	{
		++ListeningStartedCount;
	}
};
