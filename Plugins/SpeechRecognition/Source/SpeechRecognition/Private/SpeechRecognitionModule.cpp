// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SpeechRecognition.h"

IMPLEMENT_MODULE( FSpeechRecognition, SpeechRecognition )

void FSpeechRecognition::StartupModule()
{
    // Assume available; on Windows this is downgraded if the DLLs are missing.
    // On other platforms the Sphinx libraries are statically linked.
    bLibrariesLoaded = true;

    if(PLATFORM_WINDOWS) {
        //Search project plugins folder for Dll
        FString dllName = "SphinxBase.dll";
        if (SearchForDllPath(FPaths::ProjectPluginsDir(), dllName))
        {
        }
        else if (SearchForDllPath(FPaths::EnginePluginsDir(), dllName)) //Failed in project dir, try engine plugins dir
        {
        }
        else
        {
            bLibrariesLoaded = false;
        }
        
        dllName = "PocketSphinx.dll";
        if (SearchForDllPath(FPaths::ProjectPluginsDir(), dllName))
        {
        }
        else if (SearchForDllPath(FPaths::EnginePluginsDir(), dllName)) //Failed in project dir, try engine plugins dir
        {
        }
        else
        {
            bLibrariesLoaded = false;
        }

        if (!bLibrariesLoaded)
        {
            // Do not hard-crash the editor/game (the previous behaviour was a
            // checkf). Log the problem, and let USpeechRecognitionSubsystem
            // fail Init() gracefully instead.
            UE_LOG(SpeechRecognitionPlugin, Error,
                TEXT("SpeechRecognition: failed to locate SphinxBase.dll and/or PocketSphinx.dll under the project or engine Plugins directory. ")
                TEXT("Speech recognition will be unavailable. Ensure the plugin's ThirdParty DLLs are present (see the plugin README)."));
        }
    }
}

bool FSpeechRecognition::AreLibrariesLoaded() const
{
    return bLibrariesLoaded;
}

bool FSpeechRecognition::SearchForDllPath(const FString& _searchBase, FString _dllName)
{
	//Search Plugins folder for an instance of Dll.dll, and add to platform search path
	TArray<FString> directoriesToSkip;
	IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FLocalTimestampDirectoryVisitor Visitor(PlatformFile, directoriesToSkip, directoriesToSkip, false);
	PlatformFile.IterateDirectory(*_searchBase, Visitor);

	for (TMap<FString, FDateTime>::TIterator TimestampIt(Visitor.FileTimes); TimestampIt; ++TimestampIt)
	{
		const FString file = TimestampIt.Key();
		const FString filePath = FPaths::GetPath(file);
		const FString fileName = FPaths::GetCleanFilename(file);
		char* filename_str = TCHAR_TO_ANSI(*fileName);
		if (fileName.Equals(_dllName, ESearchCase::IgnoreCase))
		{
			FPlatformProcess::AddDllDirectory(*filePath); // only load dll when needed for use. Broken with 4.11.
			FPlatformProcess::GetDllHandle(*file); // auto-load dll with plugin - needed as 4.11 breaks above line.
			return true;
		}
	}
	return false;
}

void FSpeechRecognition::ShutdownModule()
{
	//TODO
}
