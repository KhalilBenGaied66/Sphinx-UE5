#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "SpeechRecognition.h"
#include "VoiceCommandComponent.h"
#include "VoiceControlledCube.generated.h"

class UStaticMeshComponent;
class UVoiceCommandComponent;

/**
 * A cube that moves when told to. Everything is set up in C++ so that the sample
 * needs no asset of its own: in a real project the same component and the same
 * event are one click each in a Blueprint.
 */
UCLASS()
class AVoiceControlledCube : public AActor
{
	GENERATED_BODY()

public:
	AVoiceControlledCube();

	/**
	 * Picks the commands before the voice command component starts: the first list
	 * of Project Settings > Game > Sphinx Sample, or the one -SpeechLanguage= names
	 * (aeb, Spells...).
	 */
	virtual void PreInitializeComponents() override;

	/** Switches to one of the lists of Project Settings > Game > Sphinx Sample while the game runs. */
	void SwitchLanguage(int32 Index);

	/** The next list, after the last the first: bound to the L key. */
	void CycleLanguage();

	/** The name of the list in use, for the HUD. */
	FString GetLanguageName() const;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Sample")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Sample")
	TObjectPtr<UVoiceCommandComponent> VoiceCommands;

	/** The list in use, in Project Settings > Game > Sphinx Sample. */
	int32 LanguageIndex = 0;

	/** Last command heard and when, for the HUD. */
	FName LastCommand;
	FRecognisedPhraseDetail LastPhrase;
	double LastCommandTime = 0.0;

	/** Transcribed languages: the last transcript, command or not, and when. */
	FString LastTranscript;
	float LastTranscriptSeconds = 0.f;
	double LastTranscriptTime = 0.0;

	/** Phrase scoring: the best phrase of the last utterance, fired or not, and by how much it led. */
	FString LastBestPhrase;
	float LastBestScore = 0.f;
	float LastLead = 0.f;
	bool bLastFired = false;

private:
	UFUNCTION()
	void HandleVoiceCommand(FName Command, FGameplayTag Tag, const FRecognisedPhraseDetail& Phrase);

	UFUNCTION()
	void HandlePhrasesHeard(const TArray<FRecognisedPhraseDetail>& Phrases);

	FVector Velocity = FVector::ZeroVector;
	float SpinSpeed = 0.f;
	float JumpVelocity = 0.f;
	float GroundHeight = 0.f;

	/** Degrees left of the one-turn spin for a command the cube has no action for. */
	float SpinTurnLeft = 0.f;
};
