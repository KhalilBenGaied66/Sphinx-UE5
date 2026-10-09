
#include "SpeechRecognitionGameStateBase.h"
#include "SpeechRecognitionSubsystem.h"

#include "Engine/World.h"

ASpeechRecognitionGameStateBase::ASpeechRecognitionGameStateBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Language(ESpeechRecognitionLanguage::VE_English)
{
	
}

void ASpeechRecognitionGameStateBase::OnSpeechRecognitionSubsystemInitialized()
{
	K2_OnSpeechRecognitionSubsystemInitialized();
}

void ASpeechRecognitionGameStateBase::BeginPlay()
{
	const UWorld* World = GetWorld();
	if (World && GetLocalRole() != ROLE_SimulatedProxy)
	{
		if (USpeechRecognitionSubsystem* SpeechRecognitionSubsystem = World->GetSubsystem<USpeechRecognitionSubsystem>())
		{
			const bool bInitialised = LanguageCodeOverride.IsEmpty()
				? SpeechRecognitionSubsystem->Init(Language)
				: SpeechRecognitionSubsystem->InitWithLanguageCode(LanguageCodeOverride);
			if (bInitialised)
			{
				if (!OptionalWorldList.IsEmpty())
				{
					SpeechRecognitionSubsystem->EnableKeywordMode(OptionalWorldList);
				}

				OnSpeechRecognitionSubsystemInitialized();
			}
		}
	}
	
	Super::BeginPlay();
}
