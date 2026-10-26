// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class SpeechRecognition : ModuleRules
	{

        private string ThirdPartyPath
        {
            get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/")); }
        }

		public SpeechRecognition(ReadOnlyTargetRules Target) : base(Target)
        {
	        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
				    "Core",
				    "CoreUObject",
				    "Engine",
				    "DeveloperSettings",
				    "GameplayTags"
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
				    // IPluginManager, to find the plugin's own ThirdParty folder
				    "Projects",
				    // languages without a PocketSphinx model: a speech-to-text model run by
				    // the Neural Network Engine
				    "NNE"
				}
				);

            // The bundled libraries are 64-bit Windows builds. The plugin's
            // descriptor lists no other platform.
            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                LoadSphinxLibrary("SphinxBase", "sphinxbase");
                LoadSphinxLibrary("PocketSphinx", "pocketsphinx");
            }
		}

        private void LoadSphinxLibrary(string FolderName, string LibraryName)
        {
            string LibrariesPath = Path.Combine(ThirdPartyPath, FolderName, "Libraries", "x64");

            // Private: no public header of the plugin includes a Sphinx header
            PrivateIncludePaths.Add(Path.Combine(ThirdPartyPath, FolderName, "Includes"));

            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, LibraryName + ".lib"));

            // Loaded by hand in StartupModule, from the plugin's own folder
            PublicDelayLoadDLLs.Add(LibraryName + ".dll");
            RuntimeDependencies.Add(Path.Combine(LibrariesPath, LibraryName + ".dll"));
        }

    }
}
