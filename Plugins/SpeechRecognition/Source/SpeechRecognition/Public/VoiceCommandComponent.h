#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SpeechRecognition.h"
#include "VoiceCommandComponent.generated.h"

class USpeechRecognitionSubsystem;

/** One thing the player can say, and the ways of saying it. */
USTRUCT(BlueprintType)
struct FVoiceCommand
{
	GENERATED_USTRUCT_BODY()

	/** Handed to On Voice Command when one of the phrases is heard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command")
	FName Command;

	/** Optional gameplay tag handed over with the command, for projects that route input by tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command")
	FGameplayTag Tag;

	/**
	 * Every wording that triggers the command, e.g. "open the door" and "open door".
	 * For a transcribed language (Tunisian Darija), give each wording in Arabic script
	 * and in the Latin spellings people use ("حلّ الباب", "7ell el beb"): the
	 * transcript is matched against all of them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command")
	TArray<FString> Phrases;

	/** V1 triggers most easily, V10 is the strictest. Short phrases need a stricter setting than long ones. Not used by transcribed languages. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command")
	EPhraseRecognitionTolerance Tolerance = EPhraseRecognitionTolerance::VE_5;
};

/** Transcribed languages: how a heard utterance is matched to a command. */
UENUM(BlueprintType)
enum class ETranscriptMatching : uint8
{
	/**
	 * Each phrase is scored against the speech model's output (FPhraseScorer), as
	 * PocketSphinx spots key phrases. Needs an engine that scores phrases (the local
	 * model); with one that only writes text (Gemini) the text is matched instead.
	 */
	PhraseScoring,
	/** The text the engine wrote is compared with the phrases (FTranscriptMatcher). */
	Text
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FVoiceCommandSignature, FName, Command, FGameplayTag, Tag, const FRecognisedPhraseDetail&, Phrase);

/**
 * Voice commands for the actor it is on.
 *
 * List the commands, bind On Voice Command, and the component does the rest: it
 * starts the recogniser if nothing has yet, adds its phrases to the ones being
 * listened for, and removes them when it is deactivated or destroyed. Several
 * components can be active at once; each one only hears its own commands.
 *
 * Activating a component puts the recogniser in keyword mode.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class SPEECHRECOGNITION_API UVoiceCommandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoiceCommandComponent();

	/** Language to start the recogniser with when this component is the first to need it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command")
	FString LanguageCode = TEXT("en");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command")
	TArray<FVoiceCommand> Commands;

	/** Transcribed languages: how what was heard is matched to a command. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command|Transcribed languages")
	ETranscriptMatching TranscriptMatching = ETranscriptMatching::PhraseScoring;

	/**
	 * Phrase scoring: the score a command's best phrase must reach, in nats per
	 * character. 0 means the model wrote exactly that phrase; lower is looser.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command|Transcribed languages", meta = (ClampMax = "0.0"))
	float PhraseScoreThreshold = -2.5f;

	/** Phrase scoring: how far the best command must lead the next one, in nats per character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command|Transcribed languages", meta = (ClampMin = "0.0"))
	float PhraseScoreMargin = 0.5f;

	/**
	 * Text matching: the score (0-100) a transcript must reach against one of
	 * a command's phrases for the command to fire.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command|Transcribed languages", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float TranscriptMatchThreshold = 80.f;

	/** Text matching: how far the best command must lead every other one. Ties fire nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command|Transcribed languages", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float TranscriptMatchMargin = 5.f;

	/** Seconds during which a command that just fired is ignored. Stops one utterance from firing it twice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Command", meta = (ClampMin = "0.0"))
	float CommandCooldown = 0.f;

	UPROPERTY(BlueprintAssignable, Category = "Voice Command")
	FVoiceCommandSignature OnVoiceCommand;

	/** Replaces the commands. Takes effect at once if the component is active. */
	UFUNCTION(BlueprintCallable, Category = "Voice Command")
	void SetCommands(const TArray<FVoiceCommand>& InCommands);

	/** The command a phrase belongs to, or null. Ignores case and pronunciation suffixes such as "(2)". */
	const FVoiceCommand* FindCommandForPhrase(const FString& Phrase) const;

	/**
	 * The command a free transcript stands for, or null: fuzzy matching across every
	 * phrase of every command (see FTranscriptMatcher). OutPhrase is the phrase that
	 * matched, OutScore its score.
	 */
	const FVoiceCommand* FindCommandForTranscript(const FString& Transcript, FString& OutPhrase, float& OutScore) const;

	/**
	 * The command whose best phrase scored highest, if it clears Phrase Score
	 * Threshold and leads the next command by Phrase Score Margin; null otherwise.
	 */
	const FVoiceCommand* FindCommandForPhraseScores(const TMap<FString, float>& Scores, FString& OutPhrase, float& OutScore) const;

	//~ Begin UActorComponent
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** Bound to the subsystem while the component is active. Public so that it can be driven directly in tests. */
	UFUNCTION()
	void HandlePhrasesRecognised(const TArray<FRecognisedPhraseDetail>& Phrases);

private:
	void RegisterWithSubsystem();
	void UnregisterFromSubsystem();
	USpeechRecognitionSubsystem* GetSubsystem() const;

	bool bRegistered = false;

	// world time at which each command last fired
	TMap<FName, double> LastFiredTime;
};
