using UnrealBuildTool;

public class SphinxSampleEditorTarget : TargetRules
{
	public SphinxSampleEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SphinxSample");
	}
}
