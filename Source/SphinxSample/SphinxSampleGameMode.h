#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "SphinxSampleGameMode.generated.h"

class AVoiceControlledCube;

/** Spawns the voice-controlled cube in front of the player and puts the status text on screen. */
UCLASS()
class ASphinxSampleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASphinxSampleGameMode();

	virtual void BeginPlay() override;

	UPROPERTY(Transient)
	TObjectPtr<AVoiceControlledCube> Cube;
};

/** Draws what the recogniser is doing: its state, the microphone level, the commands, the last one heard. */
UCLASS()
class ASphinxSampleHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
