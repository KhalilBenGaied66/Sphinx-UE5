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
	 * Picks the commands before the voice command component starts: -SpeechLanguage=aeb
	 * (Tunisian Darija), ar (Modern Standard Arabic) or any (invented spell words).
	 */
	virtual void PreInitializeComponents() override;

	/** The sample's commands in one language: en (PocketSphinx), aeb, ar or any (the second engine). */
	static TArray<FVoiceCommand> CommandsFor(const FString& LanguageCode);

	/** Changes language while the game runs. */
	void SwitchLanguage(const FString& LanguageCode);

	/** en, aeb, ar, any, en...: bound to the L key. */
	void CycleLanguage();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Sample")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Sample")
	TObjectPtr<UVoiceCommandComponent> VoiceCommands;

	/** Last command heard and when, for the HUD. */
	FName LastCommand;
	FRecognisedPhraseDetail LastPhrase;
	double LastCommandTime = 0.0;

	/** Transcribed languages: the last transcript, command or not, and when. */
	FString LastTranscript;
	float LastTranscriptSeconds = 0.f;
	double LastTranscriptTime = 0.0;

private:
	UFUNCTION()
	void HandleVoiceCommand(FName Command, FGameplayTag Tag, const FRecognisedPhraseDetail& Phrase);

	UFUNCTION()
	void HandlePhrasesHeard(const TArray<FRecognisedPhraseDetail>& Phrases);

	FVector Velocity = FVector::ZeroVector;
	float SpinSpeed = 0.f;
	float JumpVelocity = 0.f;
	float GroundHeight = 0.f;
};
