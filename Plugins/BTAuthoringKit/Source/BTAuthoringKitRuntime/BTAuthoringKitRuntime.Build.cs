using UnrealBuildTool;

public class BTAuthoringKitRuntime : ModuleRules
{
	public BTAuthoringKitRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AIModule",
			"EnhancedInput",
			"NavigationSystem",
			"InputCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayTasks",
		});
	}
}
