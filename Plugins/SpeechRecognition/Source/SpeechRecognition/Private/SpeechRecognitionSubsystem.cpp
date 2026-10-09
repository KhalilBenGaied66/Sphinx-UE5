
#include "SpeechRecognitionSubsystem.h"
#include "ISpeechRecognition.h"
#include "SpeechRecognitionSettings.h"
#include "SpeechRecognitionWorker.h"

#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#include <cstdlib>

#define SPEECHRECOGNITIONPLUGIN ISpeechRecognition::Get()

USpeechRecognitionSubsystem::USpeechRecognitionSubsystem()
{

}

TArray<FSphinxParam> USpeechRecognitionSubsystem::GetDefaultConfigParams()
{
	return {
		{ TEXT("-vad_prespeech"), ESpeechRecognitionParamType::VE_INTEGER, TEXT("10") },
		{ TEXT("-vad_postspeech"), ESpeechRecognitionParamType::VE_INTEGER, TEXT("10") },
		{ TEXT("-agc"), ESpeechRecognitionParamType::VE_STRING, TEXT("noise") },
		{ TEXT("-beam"), ESpeechRecognitionParamType::VE_FLOAT, TEXT("1e-40") },
	};
}

bool USpeechRecognitionSubsystem::Init(ESpeechRecognitionLanguage Language, bool bInitDefaultConfigParams)
{
	return InitWithLanguageCode(USpeechRecognitionSettings::GetLanguageCode(Language), bInitDefaultConfigParams);
}

bool USpeechRecognitionSubsystem::InitWithLanguageCode(const FString& InLanguageCode, bool bInitDefaultConfigParams)
{
	// If the third-party DLLs failed to load, fail gracefully instead of
	// crashing on the first delay-loaded Sphinx call.
	if (!SPEECHRECOGNITIONPLUGIN.AreLibrariesLoaded())
	{
		ReportFailure(ESpeechRecognitionError::LibrariesMissing,
			TEXT("Speech Recognition is unavailable: the SphinxBase/PocketSphinx DLLs were not loaded. See the startup log for details."));
		return false;
	}

	const FString Code = InLanguageCode.TrimStartAndEnd();
	if (Code.IsEmpty())
	{
		ReportFailure(ESpeechRecognitionError::ModelNotFound, TEXT("Init needs a language code, such as \"en\"."));
		return false;
	}

	// an existing thread is reused: the new language applies to the next mode that is enabled
	LanguageCode = Code;

	if (listenerThread == nullptr)
	{
		// start listener thread
		listenerThread = new FSpeechRecognitionWorker();
		if (!listenerThread->StartThread())
		{
			delete listenerThread;
			listenerThread = nullptr;
			ReportFailure(ESpeechRecognitionError::DecoderInitFailed, TEXT("The speech recognition thread could not be created."));
			return false;
		}
		State = ESpeechRecognitionState::Initialised;
		LastError = ESpeechRecognitionError::None;
		LastErrorMessage.Reset();
		bModeEnabled = false;

		// phrases registered before Init are picked up on the next tick
		bKeywordSourcesDirty = KeywordSources.Num() > 0;
	}

	if (bInitDefaultConfigParams)
	{
		for (const FSphinxParam& Param : GetDefaultConfigParams())
		{
			SetConfigParam(Param.Name, Param.Type, Param.Value);
		}
	}

	WarnIfModelsAreNotStaged();
	return true;
}

int32 USpeechRecognitionSubsystem::GetCurrentVolume() const
{
	if (listenerThread != nullptr) {
		const int16 returnVal = listenerThread->GetCurrentVolume();
		return returnVal;
	}
	return 0;
}

bool USpeechRecognitionSubsystem::SetConfigParam(FString param, ESpeechRecognitionParamType type, FString value)
{
	if (listenerThread == nullptr) {
		return false;
	}

	param.TrimStartAndEndInline();
	value.TrimStartAndEndInline();
	if (param.IsEmpty()) {
		return false;
	}

	// Validate the incoming string against the data type
	if (type == ESpeechRecognitionParamType::VE_FLOAT)
	{
		// accepts "0" and "1e-40" alike, and rejects text
		const FTCHARToUTF8 narrow(*value);
		char* end = nullptr;
		strtod(narrow.Get(), &end);
		if (value.IsEmpty() || end == nullptr || *end != '\0') {
			return false;
		}
	}
	else if (type == ESpeechRecognitionParamType::VE_BOOLEAN)
	{
		if (value.Equals(TEXT("true"), ESearchCase::IgnoreCase)) {
			value = TEXT("1");
		}
		else if (value.Equals(TEXT("false"), ESearchCase::IgnoreCase)) {
			value = TEXT("0");
		}
		else {
			return false;
		}
	}
	else if (type == ESpeechRecognitionParamType::VE_INTEGER)
	{
		if (!value.IsNumeric() || value.Contains(TEXT("."))) {
			return false;
		}
	}

	// a parameter set twice keeps its last value
	FSphinxParam* Existing = ConfigParams.FindByPredicate([&param](const FSphinxParam& Candidate) { return Candidate.Name == param; });
	if (Existing == nullptr) {
		Existing = &ConfigParams.AddDefaulted_GetRef();
		Existing->Name = param;
	}
	Existing->Type = type;
	Existing->Value = value;
	return true;
}

void USpeechRecognitionSubsystem::ClearConfigParams()
{
	ConfigParams.Reset();
}

bool USpeechRecognitionSubsystem::Shutdown()
{
	if (listenerThread != nullptr) {
		// stops the runnable, joins and deletes the thread...
		listenerThread->ShutDown();
		// ...then releases the worker object itself
		delete listenerThread;
		listenerThread = nullptr;

		State = ESpeechRecognitionState::Stopped;
		bModeEnabled = false;
		Keywords.Reset();
		ConfigParams.Reset();
		RejectedPhrases.Reset();
		return true;
	}
	else{
		return false;
	}
}

void USpeechRecognitionSubsystem::SetReportKeyPhrasesEarly(bool bEarly)
{
	if (bReportKeyPhrasesEarly != bEarly)
	{
		bReportKeyPhrasesEarly = bEarly;
		if (bModeEnabled && Mode == ESpeechRecognitionMode::VE_KEYWORD)
		{
			ApplyMode();
		}
	}
}

void USpeechRecognitionSubsystem::SetKeepBestOfOverlappingKeyPhrases(bool bKeepBest)
{
	if (bKeepBestOfOverlappingKeyPhrases != bKeepBest)
	{
		bKeepBestOfOverlappingKeyPhrases = bKeepBest;
		if (bModeEnabled && Mode == ESpeechRecognitionMode::VE_KEYWORD)
		{
			ApplyMode();
		}
	}
}

/**************************
// Change recognition methods
**************************/
bool USpeechRecognitionSubsystem::EnableKeywordMode(const TArray<FRecognitionPhrase>& wordList)
{
	Keywords = wordList;
	Mode = ESpeechRecognitionMode::VE_KEYWORD;
	bKeywordSourcesDirty = false;
	return ApplyMode();
}

bool USpeechRecognitionSubsystem::EnableGrammarMode(FString grammarName)
{
	GrammarName = grammarName;
	Mode = ESpeechRecognitionMode::VE_GRAMMAR;
	return ApplyMode();
}

bool USpeechRecognitionSubsystem::EnableLanguageModel(FString languageModel)
{
	LanguageModelName = languageModel;
	Mode = ESpeechRecognitionMode::VE_LANGUAGE_MODEL;
	return ApplyMode();
}

bool USpeechRecognitionSubsystem::ApplyMode()
{
	if (listenerThread == nullptr)
	{
		ReportFailure(ESpeechRecognitionError::NotInitialised, TEXT("Call Init before enabling a recognition mode."));
		return false;
	}

	FSpeechWorkerRequest Request;
	FSphinxDecoderConfig& Config = Request.Config;
	Config.AcousticModelDir = USpeechRecognitionSettings::GetAcousticModelDir(LanguageCode);
	Config.DictionaryPath = USpeechRecognitionSettings::GetDictionaryPath(LanguageCode);
	Config.Mode = Mode;
	Config.Params = ConfigParams;
	Config.bReportKeyPhrasesEarly = bReportKeyPhrasesEarly;
	Config.bKeepBestOfOverlappingKeyPhrases = bKeepBestOfOverlappingKeyPhrases;

	switch (Mode)
	{
	case ESpeechRecognitionMode::VE_KEYWORD:
		Config.Keywords = Keywords;
		for (const TPair<TWeakObjectPtr<const UObject>, TArray<FRecognitionPhrase>>& Source : KeywordSources)
		{
			if (Source.Key.IsValid())
			{
				Config.Keywords.Append(Source.Value);
			}
		}
		break;
	case ESpeechRecognitionMode::VE_GRAMMAR:
		Config.GrammarPath = USpeechRecognitionSettings::GetGrammarPath(LanguageCode, GrammarName);
		break;
	case ESpeechRecognitionMode::VE_LANGUAGE_MODEL:
		Config.LanguageModelPath = USpeechRecognitionSettings::GetLanguageModelPath(LanguageCode, LanguageModelName);
		break;
	}

	// Missing files are reported here, on the calling thread, so that the caller
	// gets false straight away. What only the decoder can tell (a broken model,
	// words missing from the dictionary, no microphone) arrives as an event.
	ESpeechRecognitionError Error = ESpeechRecognitionError::None;
	FString Message;
	if (!FSphinxDecoder::ValidateFiles(Config, Error, Message))
	{
		StopListening();
		ReportFailure(Error, Message);
		return false;
	}

	Request.Generation = ++RequestGeneration;
	Request.bListen = true;
	listenerThread->Request(MoveTemp(Request));

	bModeEnabled = true;
	State = ESpeechRecognitionState::Starting;
	RejectedPhrases.Reset();
	return true;
}

void USpeechRecognitionSubsystem::StopListening()
{
	if (listenerThread != nullptr)
	{
		FSpeechWorkerRequest Request;
		Request.Generation = ++RequestGeneration;
		Request.bListen = false;
		listenerThread->Request(MoveTemp(Request));
		State = ESpeechRecognitionState::Initialised;
	}
	bModeEnabled = false;
}

void USpeechRecognitionSubsystem::ReportFailure(ESpeechRecognitionError Error, const FString& Message)
{
	LastError = Error;
	LastErrorMessage = Message;
	if (listenerThread != nullptr)
	{
		State = ESpeechRecognitionState::Failed;
	}
	UE_LOG(SpeechRecognitionPlugin, Error, TEXT("%s"), *Message);
	OnRecognitionFailed.Broadcast(Error, Message);
}

/**************************
// Key phrases contributed by other objects
**************************/
void USpeechRecognitionSubsystem::AddKeywordSource(const UObject* Source, const TArray<FRecognitionPhrase>& Phrases)
{
	if (Source == nullptr)
	{
		return;
	}
	KeywordSources.Add(TWeakObjectPtr<const UObject>(Source), Phrases);
	bKeywordSourcesDirty = true;
}

void USpeechRecognitionSubsystem::RemoveKeywordSource(const UObject* Source)
{
	if (KeywordSources.Remove(TWeakObjectPtr<const UObject>(Source)) > 0)
	{
		bKeywordSourcesDirty = true;
	}
}

/**************************
// State
**************************/
ESpeechRecognitionState USpeechRecognitionSubsystem::GetState() const
{
	return State;
}

ESpeechRecognitionError USpeechRecognitionSubsystem::GetLastError() const
{
	return LastError;
}

FString USpeechRecognitionSubsystem::GetLastErrorMessage() const
{
	return LastErrorMessage;
}

TArray<FString> USpeechRecognitionSubsystem::GetRejectedPhrases() const
{
	return RejectedPhrases;
}

FString USpeechRecognitionSubsystem::GetLanguageCode() const
{
	return LanguageCode;
}

TArray<FString> USpeechRecognitionSubsystem::GetInstalledLanguages()
{
	return USpeechRecognitionSettings::FindInstalledLanguages();
}

/**************************
// Events from the worker thread
**************************/
void USpeechRecognitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (listenerThread == nullptr)
	{
		return;
	}

	// forget the sources that were destroyed without removing themselves
	for (auto It = KeywordSources.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
			bKeywordSourcesDirty = true;
		}
	}

	// one restart per frame, however many sources changed
	if (bKeywordSourcesDirty)
	{
		bKeywordSourcesDirty = false;
		if (KeywordSources.Num() > 0 || Keywords.Num() > 0)
		{
			Mode = ESpeechRecognitionMode::VE_KEYWORD;
			ApplyMode();
		}
		else if (bModeEnabled && Mode == ESpeechRecognitionMode::VE_KEYWORD)
		{
			// the last source left and there is nothing else to spot
			StopListening();
		}
	}

	FSpeechWorkerEvent Event;
	while (listenerThread != nullptr && listenerThread->PollEvent(Event))
	{
		if (Event.Generation != RequestGeneration)
		{
			// left over from a mode that has been replaced since
			continue;
		}

		switch (Event.Type)
		{
		case FSpeechWorkerEvent::EType::Listening:
			State = ESpeechRecognitionState::Listening;
			LastError = ESpeechRecognitionError::None;
			LastErrorMessage.Reset();
			RejectedPhrases = MoveTemp(Event.RejectedPhrases);
			UE_LOG(SpeechRecognitionPlugin, Log, TEXT("Speech Recognition has started"));
			OnListeningStarted.Broadcast();
			break;

		case FSpeechWorkerEvent::EType::Failed:
			bModeEnabled = false;
			ReportFailure(Event.Error, Event.Message);
			break;

		case FSpeechWorkerEvent::EType::SpeechStarted:
			UE_LOG(SpeechRecognitionPlugin, Verbose, TEXT("Listening"));
			OnStartedSpeaking.Broadcast();
			break;

		case FSpeechWorkerEvent::EType::Recognised:
		{
			FRecognisedPhrases Recognised;
			Recognised.phrases.Reserve(Event.Phrases.Num());
			for (const FRecognisedPhraseDetail& Phrase : Event.Phrases)
			{
				Recognised.phrases.Add(Phrase.Phrase);
			}
			OnWordsSpoken.Broadcast(Recognised);
			OnPhrasesRecognised.Broadcast(Event.Phrases);
			break;
		}

		case FSpeechWorkerEvent::EType::Unrecognised:
			OnUnknownPhrase.Broadcast();
			break;

		case FSpeechWorkerEvent::EType::SpeechEnded:
			OnStoppedSpeaking.Broadcast();
			break;
		}
	}
}

TStatId USpeechRecognitionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USpeechRecognitionSubsystem, STATGROUP_Tickables);
}

bool USpeechRecognitionSubsystem::IsTickable() const
{
	return listenerThread != nullptr && Super::IsTickable();
}

bool USpeechRecognitionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (Super::ShouldCreateSubsystem(Outer))
	{
		if (const UWorld* World = Cast<UWorld>(Outer))
		{
			return !World->IsNetMode(NM_DedicatedServer);
		}
	}

	return false;
}

void USpeechRecognitionSubsystem::Deinitialize()
{
	Shutdown();

	Super::Deinitialize();
}

/**************************
// Packaging hint
**************************/
void USpeechRecognitionSubsystem::WarnIfModelsAreNotStaged() const
{
#if WITH_EDITOR
	// The models are plain files, not assets: a packaged game only gets them if the
	// project stages their folder. Say so once per session, with the line to add.
	static bool bAlreadyChecked = false;
	if (bAlreadyChecked)
	{
		return;
	}
	bAlreadyChecked = true;

	FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FPaths::NormalizeDirectoryName(ContentDir);
	const FString ModelRoot = USpeechRecognitionSettings::GetModelRoot();
	if (!ModelRoot.StartsWith(ContentDir + TEXT("/")))
	{
		// models kept outside Content are the project's own business
		return;
	}
	const FString RelativeModelDir = ModelRoot.RightChop(ContentDir.Len() + 1);

	TArray<FString> StagedDirectories;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("DirectoriesToAlwaysStageAsNonUFS"), StagedDirectories, GGameIni);
	const FString Wanted = FString::Printf(TEXT("\"%s\""), *RelativeModelDir);
	for (const FString& Entry : StagedDirectories)
	{
		if (Entry.Contains(Wanted, ESearchCase::IgnoreCase))
		{
			return;
		}
	}

	UE_LOG(SpeechRecognitionPlugin, Display,
		TEXT("The speech models in Content/%s are not staged for packaged builds. To ship them, add this to Config/DefaultGame.ini under [/Script/UnrealEd.ProjectPackagingSettings]: +DirectoriesToAlwaysStageAsNonUFS=(Path=\"%s\")"),
		*RelativeModelDir, *RelativeModelDir);
#endif
}
