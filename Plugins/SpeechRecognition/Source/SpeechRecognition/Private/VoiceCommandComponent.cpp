#include "VoiceCommandComponent.h"
#include "SpeechRecognitionSubsystem.h"
#include "SphinxDecoder.h"
#include "TranscriptMatcher.h"

#include <limits>

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

const FVoiceCommand* UVoiceCommandComponent::FindCommandForTranscript(const FString& Transcript, FString& OutPhrase, float& OutScore) const
{
	TArray<TArray<FString>> Groups;
	Groups.Reserve(Commands.Num());
	for (const FVoiceCommand& Command : Commands)
	{
		Groups.Add(Command.Phrases);
	}
	const FTranscriptMatcher::FResult Match = FTranscriptMatcher::Match(Transcript, Groups, TranscriptMatchThreshold, TranscriptMatchMargin);
	OutScore = Match.Score;
	OutPhrase = Match.Phrase;
	UE_LOG(SpeechRecognitionPlugin, Verbose, TEXT("\"%s\": best %s (%.0f), runner-up %s (%.0f)"), *Transcript,
		Commands.IsValidIndex(Match.Group) ? *Commands[Match.Group].Command.ToString() : TEXT("none"), Match.Score,
		Commands.IsValidIndex(Match.RunnerUp) ? *Commands[Match.RunnerUp].Command.ToString() : TEXT("none"), Match.RunnerUpScore);
	return Commands.IsValidIndex(Match.Group) ? &Commands[Match.Group] : nullptr;
}

const FVoiceCommand* UVoiceCommandComponent::FindCommandForPhraseScores(const TMap<FString, float>& Scores, FString& OutPhrase, float& OutScore) const
{
	const float Unscored = -std::numeric_limits<float>::infinity();
	int32 Top = INDEX_NONE, Second = INDEX_NONE;
	float TopScore = Unscored, SecondScore = Unscored;
	FString TopPhrase;
	for (int32 i = 0; i < Commands.Num(); ++i)
	{
		float Best = Unscored;
		FString BestPhrase;
		for (const FString& Phrase : Commands[i].Phrases)
		{
			if (const float* Score = Scores.Find(Phrase.TrimStartAndEnd()))
			{
				if (*Score > Best)
				{
					Best = *Score;
					BestPhrase = Phrase;
				}
			}
		}
		if (Best > TopScore)
		{
			Second = Top;
			SecondScore = TopScore;
			Top = i;
			TopScore = Best;
			TopPhrase = BestPhrase;
		}
		else if (Best > SecondScore)
		{
			Second = i;
			SecondScore = Best;
		}
	}
	OutScore = TopScore;
	OutPhrase = TopPhrase;
	UE_LOG(SpeechRecognitionPlugin, Log, TEXT("Phrase scores: best %s (%.2f, \"%s\"), next %s (%.2f)"),
		Commands.IsValidIndex(Top) ? *Commands[Top].Command.ToString() : TEXT("none"), TopScore, *TopPhrase,
		Commands.IsValidIndex(Second) ? *Commands[Second].Command.ToString() : TEXT("none"), SecondScore);
	if (!Commands.IsValidIndex(Top) || TopScore < PhraseScoreThreshold || TopScore - SecondScore < PhraseScoreMargin)
	{
		return nullptr;
	}
	return &Commands[Top];
}

void UVoiceCommandComponent::HandlePhrasesRecognised(const TArray<FRecognisedPhraseDetail>& Phrases)
{
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetRealTimeSeconds() : 0.0;

	for (const FRecognisedPhraseDetail& Heard : Phrases)
	{
		FRecognisedPhraseDetail Phrase = Heard;
		const FVoiceCommand* Command = nullptr;
		if (Heard.bIsTranscript && TranscriptMatching == ETranscriptMatching::PhraseScoring && Heard.PhraseScores.Num() > 0)
		{
			// a transcribed language, scored phrase by phrase against the model's output
			float Score = 0.f;
			Command = FindCommandForPhraseScores(Heard.PhraseScores, Phrase.MatchedPhrase, Score);
			Phrase.PhraseScore = Score;
			// 100 when the model wrote exactly the phrase
			Phrase.Score = FMath::RoundToInt(100.f * FMath::Exp(FMath::Max(Score, -20.f)));
		}
		else if (Heard.bIsTranscript)
		{
			// a transcribed language: what the engine wrote, matched loosely
			float Score = 0.f;
			Command = FindCommandForTranscript(Heard.Phrase, Phrase.MatchedPhrase, Score);
			Phrase.Score = FMath::RoundToInt(Score);
		}
		else
		{
			Command = FindCommandForPhrase(Heard.Phrase);
		}
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
