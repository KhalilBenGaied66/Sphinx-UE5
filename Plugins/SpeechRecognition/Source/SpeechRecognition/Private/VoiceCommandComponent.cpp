#include "VoiceCommandComponent.h"
#include "SpeechRecognitionSubsystem.h"
#include "SphinxDecoder.h"
#include "TranscriptMatcher.h"
#include "VoiceCommandSet.h"

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
	RefreshCommands();
}

void UVoiceCommandComponent::AddCommand(const FVoiceCommand& InCommand)
{
	if (FVoiceCommand* Existing = Commands.FindByPredicate([&InCommand](const FVoiceCommand& Command) { return Command.Command == InCommand.Command; }))
	{
		*Existing = InCommand;
	}
	else
	{
		Commands.Add(InCommand);
	}
	RefreshCommands();
}

bool UVoiceCommandComponent::RemoveCommand(FName Command)
{
	const int32 Removed = Commands.RemoveAll([Command](const FVoiceCommand& Candidate) { return Candidate.Command == Command; });
	if (Removed > 0)
	{
		RefreshCommands();
	}
	return Removed > 0;
}

void UVoiceCommandComponent::SetCommandSets(const TArray<UVoiceCommandSet*>& InCommandSets)
{
	CommandSets.Reset();
	for (UVoiceCommandSet* CommandSet : InCommandSets)
	{
		if (CommandSet != nullptr)
		{
			CommandSets.AddUnique(CommandSet);
		}
	}
	RefreshCommands();
}

void UVoiceCommandComponent::AddCommandSet(UVoiceCommandSet* CommandSet)
{
	if (CommandSet != nullptr && !CommandSets.Contains(CommandSet))
	{
		CommandSets.Add(CommandSet);
		RefreshCommands();
	}
}

bool UVoiceCommandComponent::RemoveCommandSet(UVoiceCommandSet* CommandSet)
{
	const int32 Removed = CommandSets.Remove(CommandSet);
	if (Removed > 0)
	{
		RefreshCommands();
	}
	return Removed > 0;
}

void UVoiceCommandComponent::SetLanguage(const FString& InLanguageCode)
{
	const FString Code = InLanguageCode.TrimStartAndEnd();
	if (Code.IsEmpty())
	{
		return;
	}
	LanguageCode = Code;

	// a recogniser already running in another language takes the new one; the
	// phrases registered below restart it in keyword mode
	USpeechRecognitionSubsystem* Subsystem = GetSubsystem();
	if (Subsystem != nullptr && Subsystem->GetState() != ESpeechRecognitionState::Stopped && !Subsystem->GetLanguageCode().Equals(Code, ESearchCase::IgnoreCase))
	{
		Subsystem->InitWithLanguageCode(Code);
	}
	RefreshCommands();
}

TArray<FVoiceCommand> UVoiceCommandComponent::GetActiveCommands() const
{
	TArray<FVoiceCommand> Result;
	auto Merge = [&Result](const FVoiceCommand& Command)
	{
		// one entry per command name, so that two lists naming the same command do
		// not compete with each other for it
		if (FVoiceCommand* Existing = Result.FindByPredicate([&Command](const FVoiceCommand& Candidate) { return Candidate.Command == Command.Command; }))
		{
			for (const FString& Phrase : Command.Phrases)
			{
				Existing->Phrases.AddUnique(Phrase);
			}
		}
		else
		{
			Result.Add(Command);
		}
	};

	for (const FVoiceCommand& Command : Commands)
	{
		Merge(Command);
	}
	for (const UVoiceCommandSet* CommandSet : CommandSets)
	{
		if (CommandSet != nullptr && CommandSet->IsForLanguage(LanguageCode))
		{
			for (const FVoiceCommand& Command : CommandSet->Commands)
			{
				Merge(Command);
			}
		}
	}
	return Result;
}

void UVoiceCommandComponent::RefreshCommands()
{
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
	for (const FVoiceCommand& Command : GetActiveCommands())
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
	ActiveCommands = GetActiveCommands();
	for (const FVoiceCommand& Command : ActiveCommands)
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
	ActiveCommands = GetActiveCommands();
	const TArray<FVoiceCommand>& Active = ActiveCommands;
	TArray<TArray<FString>> Groups;
	Groups.Reserve(Active.Num());
	for (const FVoiceCommand& Command : Active)
	{
		Groups.Add(Command.Phrases);
	}
	const FTranscriptMatcher::FResult Match = FTranscriptMatcher::Match(Transcript, Groups, TranscriptMatchThreshold, TranscriptMatchMargin);
	OutScore = Match.Score;
	OutPhrase = Match.Phrase;
	UE_LOG(SpeechRecognitionPlugin, Verbose, TEXT("\"%s\": best %s (%.0f), runner-up %s (%.0f)"), *Transcript,
		Active.IsValidIndex(Match.Group) ? *Active[Match.Group].Command.ToString() : TEXT("none"), Match.Score,
		Active.IsValidIndex(Match.RunnerUp) ? *Active[Match.RunnerUp].Command.ToString() : TEXT("none"), Match.RunnerUpScore);
	return Active.IsValidIndex(Match.Group) ? &Active[Match.Group] : nullptr;
}

const FVoiceCommand* UVoiceCommandComponent::FindCommandForPhraseScores(const TMap<FString, float>& Scores, FString& OutPhrase, float& OutScore) const
{
	ActiveCommands = GetActiveCommands();
	const TArray<FVoiceCommand>& Active = ActiveCommands;
	const float Unscored = -std::numeric_limits<float>::infinity();
	int32 Top = INDEX_NONE, Second = INDEX_NONE;
	float TopScore = Unscored, SecondScore = Unscored;
	FString TopPhrase;
	for (int32 i = 0; i < Active.Num(); ++i)
	{
		float Best = Unscored;
		FString BestPhrase;
		for (const FString& Phrase : Active[i].Phrases)
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
		Active.IsValidIndex(Top) ? *Active[Top].Command.ToString() : TEXT("none"), TopScore, *TopPhrase,
		Active.IsValidIndex(Second) ? *Active[Second].Command.ToString() : TEXT("none"), SecondScore);
	if (!Active.IsValidIndex(Top) || TopScore < PhraseScoreThreshold || TopScore - SecondScore < PhraseScoreMargin)
	{
		return nullptr;
	}
	return &Active[Top];
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
