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

	// read here rather than in the constructor, so that edits made in Project
	// Settings since the editor started are used
	const USphinxSampleSettings* Settings = GetDefault<USphinxSampleSettings>();
	FString Wanted;
	if (FParse::Value(FCommandLine::Get(), TEXT("SpeechLanguage="), Wanted))
	{
		LanguageIndex = FMath::Max(Settings->FindLanguage(Wanted), 0);
	}
	if (Settings->Languages.IsValidIndex(LanguageIndex))
	{
		const FSphinxSampleLanguage& Entry = Settings->Languages[LanguageIndex];
		VoiceCommands->LanguageCode = Entry.Language;
		VoiceCommands->Commands = Entry.Commands;
	}
}

FString AVoiceControlledCube::GetLanguageName() const
{
	const TArray<FSphinxSampleLanguage>& Languages = GetDefault<USphinxSampleSettings>()->Languages;
	return Languages.IsValidIndex(LanguageIndex) ? Languages[LanguageIndex].Name : FString();
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
	const int32 Count = GetDefault<USphinxSampleSettings>()->Languages.Num();
	if (Count > 0)
	{
		SwitchLanguage((LanguageIndex + 1) % Count);
	}
}

void AVoiceControlledCube::SwitchLanguage(int32 Index)
{
	const TArray<FSphinxSampleLanguage>& Languages = GetDefault<USphinxSampleSettings>()->Languages;
	if (!Languages.IsValidIndex(Index))
	{
		return;
	}
	LanguageIndex = Index;
	// the commands first, so that the component registers them once, in the new language
	VoiceCommands->Commands = Languages[Index].Commands;
	VoiceCommands->SetLanguage(Languages[Index].Language);
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
			for (const FVoiceCommand& Command : VoiceCommands->GetActiveCommands())
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
