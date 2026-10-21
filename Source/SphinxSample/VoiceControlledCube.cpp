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

TArray<FVoiceCommand> AVoiceControlledCube::CommandsFor(const FString& LanguageCode)
{
	const FString Code = LanguageCode.ToLower();

	// The languages below have no PocketSphinx model: the plugin scores each phrase
	// against what the second engine's speech model heard. A phrase may be written
	// in Arabic script or in Latin letters, as it sounds; one spelling is enough,
	// more spellings help. The tolerance is only used by PocketSphinx.
	if (Code == TEXT("aeb"))
	{
		// Tunisian Darija, as in Tools/DarijaEval/commands.toml
		return {
			MakeCommand(TEXT("Forward"), { TEXT("قدّام"), TEXT("إمشي لقدّام"), TEXT("9oddem"), TEXT("emchi l9oddem") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Back"),    { TEXT("لتالي"), TEXT("ارجع"), TEXT("وخّر"), TEXT("ltali"), TEXT("arja3"), TEXT("wakhar") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Left"),    { TEXT("على ليسار"), TEXT("دور ليسار"), TEXT("3al lisar"), TEXT("a gauche") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Right"),   { TEXT("على ليمين"), TEXT("دور ليمين"), TEXT("3al limin"), TEXT("a droite") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Jump"),    { TEXT("اُنڨز"), TEXT("onguez") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Stop"),    { TEXT("أُقف"), TEXT("حبس"), TEXT("بركا"), TEXT("o9ef"), TEXT("barka"), TEXT("stop") }, EPhraseRecognitionTolerance::VE_5),
		};
	}
	if (Code == TEXT("ar"))
	{
		// Modern Standard Arabic, written once each, in Arabic script only
		return {
			MakeCommand(TEXT("Forward"), { TEXT("تقدّم"), TEXT("إلى الأمام") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Back"),    { TEXT("ارجع"), TEXT("إلى الخلف") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Left"),    { TEXT("يسارا"), TEXT("انعطف يسارا") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Right"),   { TEXT("يمينا"), TEXT("انعطف يمينا") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Jump"),    { TEXT("اقفز") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Stop"),    { TEXT("توقف"), TEXT("قف") }, EPhraseRecognitionTolerance::VE_5),
		};
	}
	if (Code == TEXT("any"))
	{
		// No language: invented words, written as they sound. A command may also
		// mix languages, as Stop does.
		return {
			MakeCommand(TEXT("Forward"), { TEXT("velaris") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Back"),    { TEXT("zelthar morun") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Left"),    { TEXT("sanarel vitae") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Right"),   { TEXT("kraveth ulnar") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Jump"),    { TEXT("amorix valor corva aeterna") }, EPhraseRecognitionTolerance::VE_5),
			MakeCommand(TEXT("Stop"),    { TEXT("silentium"), TEXT("stop"), TEXT("o9ef"), TEXT("قف") }, EPhraseRecognitionTolerance::VE_5),
		};
	}

	// English, PocketSphinx. Longer phrases are easier to tell apart, so they can
	// be given a looser tolerance; a single short word needs a strict one or it
	// fires on anything.
	return {
		MakeCommand(TEXT("Forward"), { TEXT("go forward"), TEXT("move forward") }, EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Back"),    { TEXT("go back"), TEXT("move back") },       EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Left"),    { TEXT("turn left") },                         EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Right"),   { TEXT("turn right") },                        EPhraseRecognitionTolerance::VE_5),
		MakeCommand(TEXT("Jump"),    { TEXT("jump") },                              EPhraseRecognitionTolerance::VE_8),
		MakeCommand(TEXT("Stop"),    { TEXT("stop") },                              EPhraseRecognitionTolerance::VE_8),
	};
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
	VoiceCommands->Commands = CommandsFor(TEXT("en"));
	VoiceCommands->CommandCooldown = 0.3f;
}

void AVoiceControlledCube::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	FString Language;
	if (FParse::Value(FCommandLine::Get(), TEXT("SpeechLanguage="), Language))
	{
		VoiceCommands->LanguageCode = Language.ToLower();
		VoiceCommands->Commands = CommandsFor(Language);
	}
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
