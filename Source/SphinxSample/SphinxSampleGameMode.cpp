#include "SphinxSampleGameMode.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "SpeechRecognitionSubsystem.h"
#include "VoiceCommandComponent.h"
#include "VoiceControlledCube.h"

ASphinxSampleGameMode::ASphinxSampleGameMode()
{
	DefaultPawnClass = ADefaultPawn::StaticClass();
	HUDClass = ASphinxSampleHUD::StaticClass();
}

void ASphinxSampleGameMode::BeginPlay()
{
	Super::BeginPlay();

	// in front of wherever the player starts
	FVector Location(400.f, 0.f, 50.f);
	FRotator Rotation = FRotator::ZeroRotator;
	if (const APlayerController* Player = GetWorld()->GetFirstPlayerController())
	{
		if (const APawn* Pawn = Player->GetPawn())
		{
			Rotation = FRotator(0.f, Pawn->GetActorRotation().Yaw, 0.f);
			Location = Pawn->GetActorLocation() + Rotation.Vector() * 500.f;
			Location.Z = 50.f;
		}
	}
	Cube = GetWorld()->SpawnActor<AVoiceControlledCube>(Location, Rotation);
}

void ASphinxSampleHUD::DrawHUD()
{
	Super::DrawHUD();

	const UWorld* World = GetWorld();
	const USpeechRecognitionSubsystem* Speech = World != nullptr ? World->GetSubsystem<USpeechRecognitionSubsystem>() : nullptr;
	if (Speech == nullptr || Canvas == nullptr)
	{
		return;
	}

	UFont* Font = GEngine->GetMediumFont();
	float Y = 40.f;
	const float X = 40.f;
	const float LineHeight = 22.f;
	auto Line = [&](const FString& Text, const FLinearColor& Colour)
	{
		DrawText(Text, Colour, X, Y, Font, 1.2f);
		Y += LineHeight;
	};

	const ESpeechRecognitionState State = Speech->GetState();
	const FString StateName = StaticEnum<ESpeechRecognitionState>()->GetNameStringByValue((int64)State);
	const FLinearColor StateColour = State == ESpeechRecognitionState::Listening ? FLinearColor::Green
		: State == ESpeechRecognitionState::Failed ? FLinearColor::Red
		: FLinearColor::Yellow;
	Line(FString::Printf(TEXT("Speech recognition: %s (language '%s')"), *StateName, *Speech->GetLanguageCode()), StateColour);
	if (Speech->IsTranscribing())
	{
		Line(FString::Printf(TEXT("Transcribed by: %s"), *Speech->GetTranscriberDescription()), FLinearColor(0.6f, 0.8f, 1.f));
	}

	if (State == ESpeechRecognitionState::Failed)
	{
		Line(Speech->GetLastErrorMessage(), FLinearColor::Red);
	}

	// microphone level
	const float Level = FMath::Clamp((float)Speech->GetCurrentVolume() / 8000.f, 0.f, 1.f);
	DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.6f), X, Y + 4.f, 300.f, 12.f);
	DrawRect(FLinearColor::Green, X, Y + 4.f, 300.f * Level, 12.f);
	Y += LineHeight;

	const ASphinxSampleGameMode* GameMode = World->GetAuthGameMode<ASphinxSampleGameMode>();
	const AVoiceControlledCube* Cube = GameMode != nullptr ? GameMode->Cube.Get() : nullptr;
	if (Cube == nullptr)
	{
		return;
	}

	Y += LineHeight * 0.5f;
	Line(TEXT("L: switch language (en, aeb Tunisian, ar MSA, any: spells)"), FLinearColor(0.6f, 0.8f, 1.f));
	Line(TEXT("Say:"), FLinearColor::White);
	for (const FVoiceCommand& Command : Cube->VoiceCommands->Commands)
	{
		Line(FString::Printf(TEXT("  %s"), *FString::Join(Command.Phrases, TEXT("  /  "))), FLinearColor(0.8f, 0.8f, 0.8f));
	}

	for (const FString& Rejected : Speech->GetRejectedPhrases())
	{
		Line(FString::Printf(TEXT("  not in the dictionary: %s"), *Rejected), FLinearColor(1.f, 0.5f, 0.f));
	}

	if (!Cube->LastCommand.IsNone())
	{
		Y += LineHeight * 0.5f;
		const double Age = World->GetRealTimeSeconds() - Cube->LastCommandTime;
		if (Cube->LastPhrase.bIsTranscript && Cube->LastPhrase.PhraseScores.Num() > 0)
		{
			Line(FString::Printf(TEXT("Heard \"%s\" -> %s   (phrase \"%s\" scored %.2f, in %.0f ms, %.1fs ago)"),
				*Cube->LastPhrase.Phrase, *Cube->LastCommand.ToString(), *Cube->LastPhrase.MatchedPhrase, Cube->LastPhrase.PhraseScore,
				Cube->LastPhrase.ProcessingSeconds * 1000.f, Age),
				Age < 1.0 ? FLinearColor::Green : FLinearColor::White);
		}
		else if (Cube->LastPhrase.bIsTranscript)
		{
			Line(FString::Printf(TEXT("Heard \"%s\" -> %s   (matched \"%s\", score %d, transcribed in %.0f ms, %.1fs ago)"),
				*Cube->LastPhrase.Phrase, *Cube->LastCommand.ToString(), *Cube->LastPhrase.MatchedPhrase, Cube->LastPhrase.Score,
				Cube->LastPhrase.ProcessingSeconds * 1000.f, Age),
				Age < 1.0 ? FLinearColor::Green : FLinearColor::White);
		}
		else
		{
			Line(FString::Printf(TEXT("Heard \"%s\" -> %s   (%.2fs to %.2fs in the utterance, score %d, %.1fs ago)"),
				*Cube->LastPhrase.Phrase, *Cube->LastCommand.ToString(), Cube->LastPhrase.StartTime, Cube->LastPhrase.EndTime, Cube->LastPhrase.Score, Age),
				Age < 1.0 ? FLinearColor::Green : FLinearColor::White);
		}
	}

	if (Speech->IsTranscribing() && !Cube->LastTranscript.IsEmpty())
	{
		// what the engine wrote, whether it matched a command or not
		const double Age = World->GetRealTimeSeconds() - Cube->LastTranscriptTime;
		Line(FString::Printf(TEXT("Last transcript: \"%s\"   (%.0f ms, %.1fs ago)"), *Cube->LastTranscript, Cube->LastTranscriptSeconds * 1000.f, Age), FLinearColor(0.8f, 0.8f, 0.8f));
		if (!Cube->LastBestPhrase.IsEmpty())
		{
			// close but not accepted: say why
			Line(Cube->bLastFired
				? FString::Printf(TEXT("Best phrase \"%s\": %.2f, %.2f ahead of the next command (fired)"), *Cube->LastBestPhrase, Cube->LastBestScore, Cube->LastLead)
				: FString::Printf(TEXT("Best phrase \"%s\": %.2f, %.2f ahead (not fired: needs %.1f or better and %.1f ahead; add the wording you said)"),
					*Cube->LastBestPhrase, Cube->LastBestScore, Cube->LastLead, Cube->VoiceCommands->PhraseScoreThreshold, Cube->VoiceCommands->PhraseScoreMargin),
				Cube->bLastFired ? FLinearColor::Green : FLinearColor(1.f, 0.7f, 0.3f));
		}
	}
}
