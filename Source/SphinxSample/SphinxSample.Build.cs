using UnrealBuildTool;

public class SphinxSample : ModuleRules
{
	public SphinxSample(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DeveloperSettings", "GameplayTags", "InputCore", "SpeechRecognition" });
	}
}
