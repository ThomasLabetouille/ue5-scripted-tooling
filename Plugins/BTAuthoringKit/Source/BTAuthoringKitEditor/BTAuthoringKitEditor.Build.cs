using UnrealBuildTool;

public class BTAuthoringKitEditor : ModuleRules
{
	public BTAuthoringKitEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AIModule",
			"InputCore",
		});

		// Modules editor-only necessaires pour manipuler le graphe visuel du Behavior Tree
		// (UBehaviorTreeGraph / UBehaviorTreeGraphNode) et pas seulement l'arbre runtime.
		// NOTE (a verifier en priorite si la compilation echoue sur ces includes) : les noms de
		// module ci-dessous sont corrects pour la plupart des versions UE5 (5.0 a 5.8+), mais
		// "BehaviorTreeEditor" a pu etre renomme ou restructure dans certaines versions moteur.
		// Si le module ne se trouve pas, chercher dans <EngineDir>/Source/Editor/ le dossier
		// correspondant a l'editeur de Behavior Tree et corriger le nom ici.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"GraphEditor",
			"BlueprintGraph",
			"AIGraph",
			"BehaviorTreeEditor",
			"Kismet",
			"Json",
			"JsonUtilities",
			"AssetRegistry",
		});
	}
}
