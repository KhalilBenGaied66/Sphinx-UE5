#include "VoiceControlledCube.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"
#include "SpeechRecognitionSubsystem.h"
#include "SphinxSampleSettings.h"
#include "VoiceCommandComponent.h"

namespace
{
	const float MoveSpeed = 250.f;
	const float TurnSpeed = 120.f;
	const float JumpSpeed = 600.f;
	const float Gravity = 1600.f;
	const float SpinTurnSpeed = 720.f;
}

TArray<FVoiceCommand> AVoiceControlledCube::CommandsFor(const FString& LanguageCode)
{
	return GetDefault<USphinxSampleSettings>()->CommandsFor(LanguageCode);
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
	VoiceCommands->CommandCooldown = 0.3f;
}

void AVoiceControlledCube::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	FString Language;
	if (FParse::Value(FCommandLine::Get(), TEXT("SpeechLanguage="), Language))
	{
		VoiceCommands->LanguageCode = Language.ToLower();
	}
	// read here rather than in the constructor, so that edits made in Project
	// Settings since the editor started are used
	VoiceCommands->Commands = CommandsFor(VoiceCommands->LanguageCode);
}

void AVoiceControlledCube::BeginPlay()
{
	Super::BeginPlay();
	GroundHeight = GetActorLocation().Z;
	VoiceCommands->OnVoiceCommand.AddDynamic(this, &AVoiceControlledCube::HandleVoiceCommand);
	if (USpeechRecognitionSubsystem* Speech = GetWorld()->GetSubsystem<USpeechRecognitionSubsystem>())
	{
		Speech->OnPhrasesRecognised.AddDynamic(this, &AVoiceControlledCube::HandlePhrasesHeard);
	}

	// L switches language while the game runs, for a demonstration in one take
	if (APlayerController* Player = GetWorld()->GetFirstPlayerController())
	{
		EnableInput(Player);
		if (InputComponent != nullptr)
		{
			InputComponent->BindKey(EKeys::L, IE_Pressed, this, &AVoiceControlledCube::CycleLanguage);
		}
	}
}

void AVoiceControlledCube::CycleLanguage()
{
	static const TCHAR* const Languages[] = { TEXT("en"), TEXT("aeb"), TEXT("ar"), TEXT("any") };
	int32 Next = 0;
	for (int32 i = 0; i < UE_ARRAY_COUNT(Languages); ++i)
	{
		if (VoiceCommands->LanguageCode.Equals(Languages[i], ESearchCase::IgnoreCase))
		{
			Next = (i + 1) % UE_ARRAY_COUNT(Languages);
			break;
		}
	}
	SwitchLanguage(Languages[Next]);
}

void AVoiceControlledCube::SwitchLanguage(const FString& LanguageCode)
{
	USpeechRecognitionSubsystem* Speech = GetWorld()->GetSubsystem<USpeechRecognitionSubsystem>();
	if (Speech == nullptr)
	{
		return;
	}
	// the subsystem takes the new language; the component's new phrases restart it
	Speech->InitWithLanguageCode(LanguageCode);
	VoiceCommands->LanguageCode = LanguageCode;
	VoiceCommands->SetCommands(CommandsFor(LanguageCode));
	LastCommand = NAME_None;
	LastTranscript.Reset();
}

void AVoiceControlledCube::HandlePhrasesHeard(const TArray<FRecognisedPhraseDetail>& Phrases)
{
	for (const FRecognisedPhraseDetail& Phrase : Phrases)
	{
		if (Phrase.bIsTranscript)
		{
			LastTranscript = Phrase.Phrase;
			LastTranscriptSeconds = Phrase.ProcessingSeconds;
			LastTranscriptTime = GetWorld()->GetRealTimeSeconds();

			// the best and second best command, as the component sees them, so that a
			// phrase that was close but not accepted shows on screen
			float Best = -1e9f, Second = -1e9f;
			FString BestPhrase;
			for (const FVoiceCommand& Command : VoiceCommands->Commands)
			{
				float CommandBest = -1e9f;
				FString CommandPhrase;
				for (const FString& Candidate : Command.Phrases)
				{
					if (const float* Score = Phrase.PhraseScores.Find(Candidate.TrimStartAndEnd()))
					{
						if (*Score > CommandBest)
						{
							CommandBest = *Score;
							CommandPhrase = Candidate;
						}
					}
				}
				if (CommandBest > Best)
				{
					Second = Best;
					Best = CommandBest;
					BestPhrase = CommandPhrase;
				}
				else if (CommandBest > Second)
				{
					Second = CommandBest;
				}
			}
			LastBestPhrase = BestPhrase;
			LastBestScore = Best;
			LastLead = Best - Second;
			bLastFired = Best >= VoiceCommands->PhraseScoreThreshold && LastLead >= VoiceCommands->PhraseScoreMargin;
		}
	}
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
	else
	{
		// a command added in Project Settings that the cube has no action for: one
		// full turn, so it shows it was heard
		SpinTurnLeft = 360.f;
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

	if (SpinTurnLeft > 0.f)
	{
		const float Step = FMath::Min(SpinTurnLeft, SpinTurnSpeed * DeltaSeconds);
		Mesh->AddLocalRotation(FRotator(0.f, Step, 0.f));
		SpinTurnLeft -= Step;
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
