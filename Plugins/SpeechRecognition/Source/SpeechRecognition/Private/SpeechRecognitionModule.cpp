// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "ISpeechRecognition.h"
#include "SpeechRecognition.h"
#include "SphinxRuntime.h"

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

//General Log
DEFINE_LOG_CATEGORY(SpeechRecognitionPlugin);

class FSpeechRecognition : public ISpeechRecognition
{

	/** Handles of the third-party libraries, null when they could not be loaded */
	void* SphinxBaseHandle = nullptr;
	void* PocketSphinxHandle = nullptr;

public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** ISpeechRecognition implementation */
	virtual bool AreLibrariesLoaded() const override;

};

IMPLEMENT_MODULE( FSpeechRecognition, SpeechRecognition )

void FSpeechRecognition::StartupModule()
{
#if PLATFORM_WINDOWS
    // The DLLs ship inside this plugin, wherever it is installed (project or
    // engine). Ask the plugin manager for that folder instead of searching every
    // plugin on disk for a file with the right name.
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SpeechRecognition"));
    if (!Plugin.IsValid())
    {
        UE_LOG(SpeechRecognitionPlugin, Error, TEXT("SpeechRecognition: the plugin manager does not know this plugin, so its DLLs cannot be located."));
        return;
    }

    const FString ThirdPartyDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("ThirdParty"));
    const FString SphinxBaseDir = ThirdPartyDir / TEXT("SphinxBase/Libraries/x64");
    const FString PocketSphinxDir = ThirdPartyDir / TEXT("PocketSphinx/Libraries/x64");

    FPlatformProcess::AddDllDirectory(*SphinxBaseDir);
    FPlatformProcess::AddDllDirectory(*PocketSphinxDir);

    // PocketSphinx imports SphinxBase by name: load SphinxBase first.
    SphinxBaseHandle = FPlatformProcess::GetDllHandle(*(SphinxBaseDir / TEXT("sphinxbase.dll")));
    if (SphinxBaseHandle != nullptr)
    {
        PocketSphinxHandle = FPlatformProcess::GetDllHandle(*(PocketSphinxDir / TEXT("pocketsphinx.dll")));
    }

    if (!AreLibrariesLoaded())
    {
        // Do not hard-crash the editor/game (the previous behaviour was a
        // checkf). Log the problem, and let USpeechRecognitionSubsystem
        // fail Init() gracefully instead.
        UE_LOG(SpeechRecognitionPlugin, Error,
            TEXT("SpeechRecognition: failed to load sphinxbase.dll and/or pocketsphinx.dll from '%s'. ")
            TEXT("Speech recognition will be unavailable. Ensure the plugin's ThirdParty folder is present (see the plugin README)."),
            *ThirdPartyDir);
        return;
    }

    SphinxRuntime::InstallLogCallback();
#endif
}

bool FSpeechRecognition::AreLibrariesLoaded() const
{
    return SphinxBaseHandle != nullptr && PocketSphinxHandle != nullptr;
}

void FSpeechRecognition::ShutdownModule()
{
    if (AreLibrariesLoaded())
    {
        SphinxRuntime::RemoveLogCallback();
    }
    if (PocketSphinxHandle != nullptr)
    {
        FPlatformProcess::FreeDllHandle(PocketSphinxHandle);
        PocketSphinxHandle = nullptr;
    }
    if (SphinxBaseHandle != nullptr)
    {
        FPlatformProcess::FreeDllHandle(SphinxBaseHandle);
        SphinxBaseHandle = nullptr;
    }
}
