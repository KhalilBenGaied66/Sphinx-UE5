// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class ISpeechRecognition : public IModuleInterface
{

	int32 instanceCtr = 0;

public:

	static inline ISpeechRecognition& Get()
	{
		return FModuleManager::LoadModuleChecked< ISpeechRecognition >( "SpeechRecognition" );
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("SpeechRecognition");
	}

	int32 GetInstanceCounter()
	{
		return instanceCtr++;
	}

	/** Whether the third-party speech libraries were successfully loaded at module startup */
	virtual bool AreLibrariesLoaded() const
	{
		return false;
	}

};
