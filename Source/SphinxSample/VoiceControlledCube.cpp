#include "VoiceControlledCube.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "VoiceCommandComponent.h"

namespace
{
	const float MoveSpeed = 250.f;
	const float TurnSpeed = 120.f;
	const float JumpSpeed = 600.f;
	const float Gravity = 1600.f;

	FVoiceCommand MakeCommand(FName Command, std::initializer_list<const TCHAR*> Phrases, EPhraseRecognitionTolerance Tolerance)
	{
		FVoiceCommand Result;
		Result.Command = Command;
		Result.Tolerance = Tolerance;
		for (const TCHAR* Phrase : Phrases)
		{
			Result.Phrases.Add(Phrase);
		}
		return Result;
	}
}

AVoiceControlledCube::AVoiceControlledCube()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetMobility(EComponentMobility::Movable);

	VoiceCommands = CreateDefaultSubobject<UVoiceCommandComponent>(TEXT("VoiceCommands"));

	// Longer phrases are easier to tell apart, so they can be given a looser
	// tolerance; a single short word needs a strict one or it fires on anything.
	VoiceCommands->Commands = {
		MakeCommand(TEXT("Forward"), { TEXT("go forward"), TEXT("move forward") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Back"),    { TEXT("go back"), TEXT("move back") },       EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Left"),    { TEXT("turn left") },                         EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Right"),   { TEXT("turn right") },                        EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Jump"),    { TEXT("jump") },                              EPhraseRecognitionTolerance::VE_8),
		MakeCommand(TEXT("Stop"),    { TEXT("stop") },                              EPhraseRecognitionTolerance::VE_8),
	};
	VoiceCommands->CommandCooldown = 0.3f;
}

void AVoiceControlledCube::BeginPlay()
{
	Super::BeginPlay();
	GroundHeight = GetActorLocation().Z;
	VoiceCommands->OnVoiceCommand.AddDynamic(this, &AVoiceControlledCube::HandleVoiceCommand);
}

void AVoiceControlledCube::HandleVoiceCommand(FName Command, FGameplayTag /*Tag*/, const FRecognisedPhraseDetail& Phrase)
{
	LastCommand = Command;
	LastPhrase = Phrase;
	LastCommandTime = GetWorld()->GetRealTimeSeconds();

	if (Command == TEXT("Forward"))
	{
		Velocity = GetActorForwardVector() * MoveSpeed;
		SpinSpeed = 0.f;
	}
	else if (Command == TEXT("Back"))
	{
		Velocity = GetActorForwardVector() * -MoveSpeed;
		SpinSpeed = 0.f;
	}
	else if (Command == TEXT("Left"))
	{
		SpinSpeed = -TurnSpeed;
	}
	else if (Command == TEXT("Right"))
	{
		SpinSpeed = TurnSpeed;
	}
	else if (Command == TEXT("Jump"))
	{
		if (FMath::IsNearlyEqual(GetActorLocation().Z, GroundHeight, 1.f))
		{
			JumpVelocity = JumpSpeed;
		}
	}
	else if (Command == TEXT("Stop"))
	{
		Velocity = FVector::ZeroVector;
		SpinSpeed = 0.f;
	}
}

void AVoiceControlledCube::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (SpinSpeed != 0.f)
	{
		AddActorWorldRotation(FRotator(0.f, SpinSpeed * DeltaSeconds, 0.f));
		// keep moving the way the cube now faces
		if (!Velocity.IsNearlyZero())
		{
			Velocity = GetActorForwardVector() * (FVector::DotProduct(Velocity, GetActorForwardVector()) >= 0.f ? MoveSpeed : -MoveSpeed);
		}
	}

	FVector Location = GetActorLocation() + Velocity * DeltaSeconds;

	if (JumpVelocity != 0.f || Location.Z > GroundHeight)
	{
		Location.Z += JumpVelocity * DeltaSeconds;
		JumpVelocity -= Gravity * DeltaSeconds;
		if (Location.Z <= GroundHeight)
		{
			Location.Z = GroundHeight;
			JumpVelocity = 0.f;
		}
	}

	SetActorLocation(Location);
}
