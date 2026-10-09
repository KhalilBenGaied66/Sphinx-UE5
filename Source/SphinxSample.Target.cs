using UnrealBuildTool;

public class SphinxSampleTarget : TargetRules
{
	public SphinxSampleTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SphinxSample");
	}
}
