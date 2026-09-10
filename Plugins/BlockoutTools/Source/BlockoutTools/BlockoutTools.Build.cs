using UnrealBuildTool;

public class BlockoutTools : ModuleRules
{
    public BlockoutTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.NoPCHs;

        // Compilation fichier par fichier, pas en bloc unifie.
        //
        // BlockoutPanelGeometry.cpp fait un "using BlockoutPanelGeometry::Epsilon;" au niveau
        // fichier. En build unifie, plusieurs .cpp sont concatenes : ce nom devient visible pour
        // tout le bloc, et le moindre en-tete moteur declarant son propre "Epsilon" -- Chaos le
        // fait a sept endroits, tire par StaticMeshResources.h -- declenche C4459, traite comme
        // une erreur. Symptome trompeur : la compilation echoue en pointant des fichiers du
        // MOTEUR alors que la cause est ici.
        //
        // Desactiver l'unite coute quelques secondes et supprime toute une classe de collisions
        // entre fichiers. Le noyau geometrique, deja teste, n'est pas touche.
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "UnrealEd",              // GEditor, FScopedTransaction
            "EditorSubsystem",       // UEditorSubsystem (UBlockoutGeometrySubsystem)
            "EngineSettings",        // UGameMapsSettings -- resolution du personnage du projet hote
            "Slate",
            "SlateCore",
            "ToolMenus",             // entree de menu Tools -> Outil Blockout
            "WorkspaceMenuStructure",
            "AssetRegistry",         // FAssetData (selecteurs d'asset)
            "EditorScriptingUtilities", // UEditorActorSubsystem (selection, duplication, destruction)
            "LevelEditor",           // GCurrentLevelEditingViewportClient (bouton "position camera")
            "PropertyEditor",        // SObjectPropertyEntryBox (selecteurs de mesh / texture)
            "ImageWrapper",          // lecture d'un PNG disque (Generateur depuis un plan 2D)
            // Dessin libre : ecriture d'un vrai asset UStaticMesh a partir d'un
            // FMeshDescription (BlockoutPanelMeshAsset). UnrealEd les expose deja en
            // dependance publique, mais on les declare : ce module inclut leurs headers.
            "MeshDescription",
            "StaticMeshDescription",
            // UEdMode / UBaseLegacyWidgetEdMode (mode d'edition du viewport) et
            // FEditorModeInfo -- meme remarque, transitifs via UnrealEd mais inclus ici.
            "EditorFramework",
            "InteractiveToolsFramework",
            "PhysicsCore",           // ECollisionTraceFlag (collision complexe des panneaux perces)
            "InputCore",             // EKeys::LeftMouseButton / Enter / Escape / BackSpace --
                                     // premier outil du plugin a lire des touches (mode Dessin libre).
                                     // Les EKeys sont des STATIQUES exportees par InputCore : sans ce
                                     // module tout compile et seul le LINK echoue.
        });
    }
}
