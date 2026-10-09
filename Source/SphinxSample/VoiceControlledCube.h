#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "SpeechRecognition.h"
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

private:
	UFUNCTION()
	void HandleVoiceCommand(FName Command, FGameplayTag Tag, const FRecognisedPhraseDetail& Phrase);

	FVector Velocity = FVector::ZeroVector;
	float SpinSpeed = 0.f;
	float JumpVelocity = 0.f;
	float GroundHeight = 0.f;
};
