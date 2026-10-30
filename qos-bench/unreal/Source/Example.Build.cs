using UnrealBuildTool;

public class Otuken : ModuleRules
{
	public Otuken(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			// QOS benchmark harness
			"NNE",
			"RHI",
			"RenderCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
