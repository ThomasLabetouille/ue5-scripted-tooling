using UnrealBuildTool;

public class RoomGenerator : ModuleRules
{
    public RoomGenerator(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.NoPCHs;  // LNK2011 fix — Live Coding ne peut pas linker le PCH objet du patch

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "AIModule",
            "HTTP",
            "JsonUtilities",
            "InputCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "UnrealEd",
            "EditorSubsystem",
            "BlueprintGraph",
            "Kismet",
            "Json",
            "Slate",
            "SlateCore",
            "EditorStyle",
            "ToolMenus",
            "WorkspaceMenuStructure",
            "PythonScriptPlugin",
            "AssetTools",
            "AssetRegistry",
            // EditorScriptingUtilities/LevelEditor/PropertyEditor/ImageWrapper retires le
            // 2026-07-30 (session 21) — n'etaient necessaires qu'a SBlockoutToolPanel, dont
            // la copie ici a ete supprimee (le panneau vit desormais uniquement dans le
            // plugin BlockoutTools). Confirme par grep avant suppression : aucun autre
            // fichier de ce module ne les referencait.
            // Niagara — NiagaraEditingSubsystem
            "Niagara",
            "NiagaraEditor",
            "NiagaraCore",
        });
    }
}
