#include "VoiceCommandComponent.h"
#include "SpeechRecognitionSubsystem.h"
#include "SphinxDecoder.h"

#include "Engine/World.h"

UVoiceCommandComponent::UVoiceCommandComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

USpeechRecognitionSubsystem* UVoiceCommandComponent::GetSubsystem() const
{
	// Components are also activated in the level editor's own world, where nobody
	// wants the microphone opened: only game worlds (standalone, PIE) count.
	const UWorld* World = GetWorld();
	return (World != nullptr && World->IsGameWorld()) ? World->GetSubsystem<USpeechRecognitionSubsystem>() : nullptr;
}

void UVoiceCommandComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	if (IsActive())
	{
		RegisterWithSubsystem();
	}
}

void UVoiceCommandComponent::Deactivate()
{
	Super::Deactivate();
	if (!IsActive())
	{
		UnregisterFromSubsystem();
	}
}

void UVoiceCommandComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSubsystem();
	Super::EndPlay(EndPlayReason);
}

void UVoiceCommandComponent::SetCommands(const TArray<FVoiceCommand>& InCommands)
{
	Commands = InCommands;
	if (bRegistered)
	{
		RegisterWithSubsystem();
	}
}

void UVoiceCommandComponent::RegisterWithSubsystem()
{
	USpeechRecognitionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem == nullptr)
	{
		// no subsystem on a dedicated server, or outside a game world
		return;
	}

	if (Subsystem->GetState() == ESpeechRecognitionState::Stopped && !Subsystem->InitWithLanguageCode(LanguageCode))
	{
		return;
	}

	TArray<FRecognitionPhrase> Phrases;
	for (const FVoiceCommand& Command : Commands)
	{
		for (const FString& Phrase : Command.Phrases)
		{
			if (!Phrase.TrimStartAndEnd().IsEmpty())
			{
				Phrases.Emplace(Phrase, Command.Tolerance);
			}
		}
	}

	Subsystem->AddKeywordSource(this, Phrases);
	if (!bRegistered)
	{
		Subsystem->OnPhrasesRecognised.AddDynamic(this, &UVoiceCommandComponent::HandlePhrasesRecognised);
		bRegistered = true;
	}
}

void UVoiceCommandComponent::UnregisterFromSubsystem()
{
	if (!bRegistered)
	{
		return;
	}
	bRegistered = false;

	if (USpeechRecognitionSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->OnPhrasesRecognised.RemoveDynamic(this, &UVoiceCommandComponent::HandlePhrasesRecognised);
		Subsystem->RemoveKeywordSource(this);
	}
}

const FVoiceCommand* UVoiceCommandComponent::FindCommandForPhrase(const FString& Phrase) const
{
	// compare in the form the recogniser reports: lower case, single spaces, no "(2)"
	const FString Heard = FSphinxDecoder::NormalisePhrase(FSphinxDecoder::StripPronunciationSuffixes(Phrase));
	for (const FVoiceCommand& Command : Commands)
	{
		for (const FString& Candidate : Command.Phrases)
		{
			if (FSphinxDecoder::NormalisePhrase(FSphinxDecoder::StripPronunciationSuffixes(Candidate)) == Heard)
			{
				return &Command;
			}
		}
	}
	return nullptr;
}

void UVoiceCommandComponent::HandlePhrasesRecognised(const TArray<FRecognisedPhraseDetail>& Phrases)
{
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetRealTimeSeconds() : 0.0;

	for (const FRecognisedPhraseDetail& Phrase : Phrases)
	{
		const FVoiceCommand* Command = FindCommandForPhrase(Phrase.Phrase);
		if (Command == nullptr)
		{
			// someone else's phrase
			continue;
		}

		if (CommandCooldown > 0.f)
		{
			// real time, so that a cooldown still runs out while the game is paused
			if (const double* LastTime = LastFiredTime.Find(Command->Command))
			{
				if (Now - *LastTime < (double)CommandCooldown)
				{
					continue;
				}
			}
			LastFiredTime.Add(Command->Command, Now);
		}

		OnVoiceCommand.Broadcast(Command->Command, Command->Tag, Phrase);
	}
}
