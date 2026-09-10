using UnrealBuildTool;

public class BlueprintPythonUtils : ModuleRules
{
    public BlueprintPythonUtils(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "UnrealEd",
            "BlueprintGraph",
            "KismetCompiler",
            "GraphEditor",
            "Kismet",
            "ToolMenus",
        });
    }
}
