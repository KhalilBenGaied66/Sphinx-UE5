
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SpeechRecognition.h"
#include "SphinxDecoder.h"
#include "SpeechRecognitionSubsystem.generated.h"

class FSpeechRecognitionWorker;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FStartedSpeakingSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FStoppedSpeakingSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWordsSpokenSignature, FRecognisedPhrases, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUnknownPhraseSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPhrasesRecognisedSignature, const TArray<FRecognisedPhraseDetail>&, Phrases);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FListeningStartedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRecognitionFailedSignature, ESpeechRecognitionError, Error, const FString&, Message);

/**
 * Speech recognition for one world.
 *
 * Call Init (or InitWithLanguageCode), then enable one of the three modes. The
 * model is loaded and the microphone is read on a worker thread; every event
 * below is raised on the game thread, from this subsystem's Tick.
 */
UCLASS(BlueprintType)
class SPEECHRECOGNITION_API USpeechRecognitionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

	FSpeechRecognitionWorker* listenerThread = nullptr;

	// Everything below is game-thread state. The worker only ever receives a
	// complete copy of it, as a decoder configuration.
	ESpeechRecognitionState State = ESpeechRecognitionState::Stopped;
	ESpeechRecognitionError LastError = ESpeechRecognitionError::None;
	FString LastErrorMessage;
	TArray<FString> RejectedPhrases;

	FString LanguageCode;
	ESpeechRecognitionMode Mode = ESpeechRecognitionMode::VE_KEYWORD;
	bool bModeEnabled = false;
	TArray<FRecognitionPhrase> Keywords;
	FString GrammarName;
	FString LanguageModelName;
	TArray<FSphinxParam> ConfigParams;
	bool bReportKeyPhrasesEarly = false;
	bool bKeepBestOfOverlappingKeyPhrases = true;

	// Key phrases contributed by other objects (voice command components), on top of Keywords
	TMap<TWeakObjectPtr<const UObject>, TArray<FRecognitionPhrase>> KeywordSources;
	bool bKeywordSourcesDirty = false;

	// Identifies the current mode request; events of older requests are dropped
	uint32 RequestGeneration = 0;

	// Languages without a PocketSphinx model: the engine that transcribes them.
	// Kept across mode changes, since creating one can mean loading a large model.
	FSpeechTranscriberPtr Transcriber;
	FString TranscriberKey;

	bool ApplyMode();
	bool EnsureTranscriber(FString& OutError);
	void StopListening();
	void ReportFailure(ESpeechRecognitionError Error, const FString& Message);
	void WarnIfModelsAreNotStaged() const;

public:
	//Methods to switch recognition modes
	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Enable Keyword Mode", Keywords = "Speech Recognition Mode"))
	bool EnableKeywordMode(const TArray<FRecognitionPhrase>& wordList);

	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Enable Grammar Mode", Keywords = "Speech Recognition Mode"))
	bool EnableGrammarMode(FString grammarName);

	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Enable Language Model Mode", Keywords = "Speech Recognition Mode"))
	bool EnableLanguageModel(FString languageModel);

	// Basic functions
	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "GetCurrentVolume", Keywords = "Speech Recognition Volume"))
	int32 GetCurrentVolume() const;

	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Init", Keywords = "Speech Recognition Init"))
	bool Init(ESpeechRecognitionLanguage Language, bool bInitDefaultConfigParams = true);

	/**
	 * Init for any language: the code is the name of the language's folder under the
	 * model folder ("en", "de", "fr"...). See Get Installed Languages.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Init With Language Code", Keywords = "Speech Recognition Init Language"))
	bool InitWithLanguageCode(const FString& InLanguageCode, bool bInitDefaultConfigParams = true);

	/**
	 * Sets a PocketSphinx parameter, such as "-vad_postspeech". It takes effect the next
	 * time a mode is enabled and stays set until Shutdown or Clear Config Params.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "SetConfigParam", Keywords = "Speech Recognition Set Config Param"))
	bool SetConfigParam(FString param, ESpeechRecognitionParamType type, FString value);

	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Clear Config Params", Keywords = "Speech Recognition Config Param"))
	void ClearConfigParams();

	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Shutdown", Keywords = "Speech Recognition Shutdown"))
	bool Shutdown();

	/**
	 * Keyword mode: report a phrase the moment it is spotted, instead of when the speaker
	 * pauses. Commands arrive a few hundred milliseconds sooner; the price is that a
	 * phrase is reported before the recogniser has heard what follows it, so two phrases
	 * that start alike ("lights on", "lights off") can both be reported. Off by default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Set Report Key Phrases Early"))
	void SetReportKeyPhrasesEarly(bool bEarly);

	/**
	 * Keyword mode: when two different phrases are spotted over the same stretch of speech,
	 * keep only the one the recogniser scored higher. On by default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Set Keep Best Of Overlapping Key Phrases"))
	void SetKeepBestOfOverlappingKeyPhrases(bool bKeepBest);

	// State
	UFUNCTION(BlueprintPure, Category = "Audio|SpeechRecognition", meta = (DisplayName = "Get Speech Recognition State"))
	ESpeechRecognitionState GetState() const;

	/** The reason for the last failure, or None. Cleared when a mode starts listening. */
	UFUNCTION(BlueprintPure, Category = "Audio|SpeechRecognition")
	ESpeechRecognitionError GetLastError() const;

	UFUNCTION(BlueprintPure, Category = "Audio|SpeechRecognition")
	FString GetLastErrorMessage() const;

	/** Key phrases the current keyword list lost because one of their words is not in the dictionary. */
	UFUNCTION(BlueprintPure, Category = "Audio|SpeechRecognition")
	TArray<FString> GetRejectedPhrases() const;

	/** The language code given to Init. */
	UFUNCTION(BlueprintPure, Category = "Audio|SpeechRecognition")
	FString GetLanguageCode() const;

	/**
	 * Whether the language is transcribed rather than decoded by PocketSphinx: it has
	 * no PocketSphinx model. Recognised phrases are then transcripts, which voice
	 * command components match loosely against their phrases. Only keyword mode
	 * (and voice command components) work for such a language.
	 */
	UFUNCTION(BlueprintPure, Category = "Audio|SpeechRecognition")
	bool IsTranscribing() const;

	/** Engine, model and where it runs, for a language that is transcribed; empty otherwise. */
	UFUNCTION(BlueprintPure, Category = "Audio|SpeechRecognition")
	FString GetTranscriberDescription() const;

	/** Codes of the languages that have a model installed under the model folder. */
	UFUNCTION(BlueprintPure, Category = "Audio|SpeechRecognition")
	static TArray<FString> GetInstalledLanguages();

	/**
	 * Adds key phrases on behalf of Source, on top of those given to Enable Keyword Mode,
	 * and switches to keyword mode. Calling it again for the same Source replaces its
	 * phrases. Several sources may be added in one frame: the recogniser restarts once.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition")
	void AddKeywordSource(const UObject* Source, const TArray<FRecognitionPhrase>& Phrases);

	UFUNCTION(BlueprintCallable, Category = "Audio|SpeechRecognition")
	void RemoveKeywordSource(const UObject* Source);

	// Callback events
	UPROPERTY(BlueprintAssignable, Category = "Audio|SpeechRecognition")
	FWordsSpokenSignature OnWordsSpoken;

	/** Same moment as On Words Spoken, with the position and score of each phrase. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|SpeechRecognition")
	FPhrasesRecognisedSignature OnPhrasesRecognised;

	UPROPERTY(BlueprintAssignable, Category = "Audio|SpeechRecognition")
	FUnknownPhraseSignature OnUnknownPhrase;

	UPROPERTY(BlueprintAssignable, Category = "Audio|SpeechRecognition")
	FStartedSpeakingSignature OnStartedSpeaking;

	UPROPERTY(BlueprintAssignable, Category = "Audio|SpeechRecognition")
	FStoppedSpeakingSignature OnStoppedSpeaking;

	/** The model is loaded and the microphone is open. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|SpeechRecognition")
	FListeningStartedSignature OnListeningStarted;

	/** A mode could not start, or the microphone failed. Enabling a mode again retries. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|SpeechRecognition")
	FRecognitionFailedSignature OnRecognitionFailed;

	//~ Begin UWorldSubsystem Subsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	//~ End UWorldSubsystem Subsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	//~ End FTickableGameObject

	USpeechRecognitionSubsystem();

	/** The PocketSphinx parameters Init sets when asked for the defaults. */
	static TArray<FSphinxParam> GetDefaultConfigParams();
};
