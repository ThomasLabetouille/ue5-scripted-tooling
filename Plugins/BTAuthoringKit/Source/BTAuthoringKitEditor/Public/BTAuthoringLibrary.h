#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BTAuthoringLibrary.generated.h"

class UBlackboardData;
class UBehaviorTree;
class UEdGraphNode;
class UBTCompositeNode;

UENUM(BlueprintType)
enum class EBTAuthoringKeyType : uint8
{
	Object,
	Vector,
	Bool,
	Float,
	Int,
	Enum,
	Name,
	String,
	Class,
};

UENUM(BlueprintType)
enum class EBTAuthoringCompositeType : uint8
{
	Selector,
	Sequence,
	SimpleParallel,
};

UENUM(BlueprintType)
enum class EBTAuthoringNodeKind : uint8
{
	Task,
	Decorator,
	Service,
};

/**
 * Bibliotheque de fonctions pour construire des assets Behavior Tree / Blackboard UE5
 * entierement par script (Python via ue5-mcp, ou tout autre appelant Blueprint/Python) —
 * sans jamais ouvrir l'editeur de graphe a la souris.
 *
 * Toutes les fonctions sont statiques + BlueprintCallable : elles sont donc automatiquement
 * exposees a Python (ex. unreal.BTAuthoringLibrary.create_blackboard(...)), sans glue
 * supplementaire — meme mecanisme que les extensions C++ deja presentes dans ce projet
 * (BatchWireGraph, BlueprintEditingSubsystem.add_member_variable).
 *
 * Workflow type depuis Python :
 *   1. bb = create_blackboard(...)
 *   2. add_blackboard_key(bb, "TargetActor", Object, Pawn)
 *   3. bt = create_behavior_tree(..., bb)
 *   4. root_sel = add_composite_node(bt, Selector, "Root")
 *   5. combat_seq = add_composite_node(bt, Sequence, "Combat")
 *   6. connect_child(root_sel, combat_seq)
 *   7. atk = add_task_node(bt, "/Script/AIModule.BTTask_MoveTo", "Chase")
 *   8. connect_child(combat_seq, atk)
 *   9. set_node_blackboard_key(atk, "BlackboardKey", bb, "TargetActor")
 *  10. compile_behavior_tree(bt)
 *
 * IMPORTANT (a lire avant de debugger un echec de compilation C++) : les fonctions marquees
 * "RISQUE" dans les commentaires de l'implementation (.cpp) manipulent des classes editor-only
 * du module BehaviorTreeEditor/AIGraph dont l'API exacte (noms de fonctions/proprietes) peut
 * varier legerement selon la version d'engine. Si ca ne compile pas, ce sont les premiers
 * endroits a corriger — voir les commentaires "VERIFIER" dans le .cpp.
 */
UCLASS()
class BTAUTHORINGKITEDITOR_API UBTAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ──────────────────────────────────────────────────────────────
	// BLACKBOARD
	// ──────────────────────────────────────────────────────────────

	/** Cree (ou recharge si deja existant) un asset BlackboardData. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|Blackboard")
	static UBlackboardData* CreateBlackboard(const FString& PackagePath, const FString& AssetName);

	/**
	 * Ajoute une cle au Blackboard si elle n'existe pas deja (idempotent, comme le reste des
	 * fonctions de ce fichier). ObjectFilterClass n'est utilise que si KeyType == Object
	 * (peut etre nullptr pour ne filtrer sur aucune classe precise, equivalent a AActor).
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|Blackboard")
	static bool AddBlackboardKey(UBlackboardData* Blackboard, const FString& KeyName, EBTAuthoringKeyType KeyType, UClass* ObjectFilterClass);

	// ──────────────────────────────────────────────────────────────
	// BEHAVIOR TREE — creation et noeuds
	// ──────────────────────────────────────────────────────────────

	/** Cree (ou recharge) un asset BehaviorTree, avec son Blackboard deja assigne. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static UBehaviorTree* CreateBehaviorTree(const FString& PackagePath, const FString& AssetName, UBlackboardData* Blackboard);

	/** Ajoute un noeud composite (Selector/Sequence/SimpleParallel) au graphe. Retourne le noeud cree (handle a reutiliser dans ConnectChild). */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static UEdGraphNode* AddCompositeNode(UBehaviorTree* Tree, EBTAuthoringCompositeType CompositeType, const FString& NodeLabel, int32 PosX, int32 PosY);

	/**
	 * Ajoute un noeud Task au graphe. TaskClassPath : chemin de classe complet, ex.
	 * "/Script/AIModule.BTTask_MoveTo" pour un node standard, ou le chemin d'une classe
	 * Blueprint compilee (ex. "/Game/RPGTest/Blueprints/Enemy/AI/BTTask_ExecuteAttack.BTTask_ExecuteAttack_C").
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static UEdGraphNode* AddTaskNode(UBehaviorTree* Tree, const FString& TaskClassPath, const FString& NodeLabel, int32 PosX, int32 PosY);

	/** Attache un Decorator (par chemin de classe) a un noeud existant (composite ou task). Retourne le noeud du decorator (handle a reutiliser dans SetNodeProperty/SetNodeBlackboardKey), ou nullptr en cas d'echec. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static UEdGraphNode* AddDecoratorToNode(UEdGraphNode* OwnerNode, const FString& DecoratorClassPath, const FString& DecoratorLabel);

	/** Attache un Service (par chemin de classe) a un noeud composite existant. Retourne le noeud du service (handle a reutiliser dans SetNodeProperty/SetNodeBlackboardKey), ou nullptr en cas d'echec. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static UEdGraphNode* AddServiceToNode(UEdGraphNode* OwnerCompositeNode, const FString& ServiceClassPath, const FString& ServiceLabel);

	/** Connecte ParentNode (composite) -> ChildNode dans le graphe (pin Output du parent -> pin Input de l'enfant). */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static bool ConnectChild(UEdGraphNode* ParentNode, UEdGraphNode* ChildNode);

	/** Connecte le noeud racine du graphe (Root) au premier noeud de l'arbre (generalement un composite). */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static bool SetTreeRoot(UBehaviorTree* Tree, UEdGraphNode* RootChildNode);

	// ──────────────────────────────────────────────────────────────
	// CONFIGURATION DES PROPRIETES DE NOEUD
	// ──────────────────────────────────────────────────────────────

	/**
	 * Definit la valeur d'une propriete exposee (EditAnywhere) sur l'instance du noeud, par
	 * reflexion generique (FProperty::ImportText), a partir d'une representation texte —
	 * ex. SetNodeProperty(waitNode, "WaitTime", "2.0"), SetNodeProperty(moveToNode, "AcceptableRadius", "150.0").
	 * Fonctionne pour les types simples (float/int/bool/FString/FName/enum). Pour les
	 * FBlackboardKeySelector, utiliser SetNodeBlackboardKey plutot (structure plus complexe).
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static bool SetNodeProperty(UEdGraphNode* Node, const FString& PropertyName, const FString& ValueAsString);

	/** Definit une propriete de type FBlackboardKeySelector sur l'instance du noeud (Task/Decorator/Service) pour qu'elle pointe vers KeyName du Blackboard donne. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static bool SetNodeBlackboardKey(UEdGraphNode* Node, const FString& PropertyName, UBlackboardData* Blackboard, const FString& KeyName);

	// ──────────────────────────────────────────────────────────────
	// DECOUVERTE — catalogue des noeuds disponibles dans ce projet
	// ──────────────────────────────────────────────────────────────

	/**
	 * Liste les classes de Task/Decorator/Service disponibles dans CE projet (natives du moteur +
	 * C++ custom + Blueprints derives), qu'elles soient deja chargees en memoire ou non. A appeler
	 * avant de faire generer un arbre par un agent IA, pour lui fournir le vrai catalogue plutot
	 * que de le laisser deviner/halluciner un nom de classe qui n'existe pas ici.
	 * Retourne des chemins directement utilisables dans AddTaskNode/AddDecoratorToNode/
	 * AddServiceToNode (ex. "/Script/AIModule.BTTask_MoveTo" pour une classe native,
	 * "/Game/.../BTTask_Foo.BTTask_Foo_C" pour un Blueprint).
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|Discovery")
	static TArray<FString> ListAvailableNodeClasses(EBTAuthoringNodeKind NodeKind);

	/**
	 * Tous les nodes du graphe d'un Behavior Tree EXISTANT, sous-nodes compris.
	 *
	 * Comble le seul trou serieux du kit : jusqu'ici il savait CONSTRUIRE un arbre (Python
	 * gardait les UEdGraphNode* renvoyes par AddCompositeNode/AddTaskNode), mais pas ADRESSER
	 * les nodes d'un arbre deja fait -- UBehaviorTree::BTGraph n'est pas expose a Python, donc
	 * cote script il n'existait aucun moyen d'atteindre le graphe d'un asset existant.
	 *
	 * Sans ces trois fonctions, modifier un arbre existant obligeait soit a le reconstruire
	 * entierement (avec le risque de perdre le parametrage deja en place), soit a ecrire
	 * directement dans l'arbre runtime -- ce que le projet interdit, parce que le resultat
	 * n'apparait pas dans l'editeur et ne survit pas a une recompilation du graphe.
	 *
	 * Usage type : enumerer, identifier l'instance visee par sa classe et ses proprietes,
	 * la modifier avec SetInstanceProperty/SetInstanceBlackboardKey, puis CompileBehaviorTree.
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|Discovery")
	static TArray<UEdGraphNode*> GetGraphNodes(UBehaviorTree* Tree, bool bIncludeSubNodes = true);

	/** Instance runtime portee par un node de graphe (composite, task, decorator ou service). */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|Discovery")
	static UObject* GetGraphNodeInstance(UEdGraphNode* GraphNode);

	/** Node de graphe parent d'un sous-node. Renvoie nullptr pour un node de premier niveau. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|Discovery")
	static UEdGraphNode* GetGraphNodeParent(UEdGraphNode* GraphNode);

	/**
	 * Change la touche des evenements clavier d'un Blueprint (y compris les Input Debug Key).
	 *
	 * Pourquoi ceci existe : le sample lie sa bascule "mode vol de debug" a la touche Z dans le
	 * graphe du pawn. En AZERTY c'est aussi la touche d'avance, donc marcher fait decoller.
	 *
	 * Deux fausses pistes ecartees avant d'en arriver la. Modifier la liaison runtime portee par
	 * l'InputComponent ne trouve rien : un K2Node_InputDebugKeyEvent ne s'enregistre pas au meme
	 * endroit qu'un evenement clavier ordinaire. Et consommer la touche par Enhanced Input
	 * casserait le deplacement, puisque c'est la meme touche. Il faut donc modifier la SOURCE,
	 * c'est-a-dire le noeud du graphe -- ce qui a l'avantage de survivre a une recompilation,
	 * contrairement a toute retouche runtime.
	 *
	 * Le noeud est trouve PAR REFLEXION (propriete FKey nommee "InputKey") plutot que par un cast
	 * vers UK2Node_InputKey / UK2Node_InputDebugKey : ces classes vivent dans des modules editeur
	 * dont les en-tetes changent selon la version du moteur, et une erreur d'include ici casserait
	 * la compilation de tout le projet -- ce qui vient d'arriver une fois.
	 *
	 * Renvoie le nombre de noeuds modifies. Zero signifie que la touche n'a pas ete trouvee :
	 * ne pas confondre avec un succes.
	 *
	 * Les touches sont passees par NOM ("Z", "W", "F5"...) et non par FKey : le struct FKey
	 * n'expose STRICTEMENT RIEN a Python -- ni champ, ni constructeur, ni to_dict -- donc un
	 * parametre FKey serait impossible a fournir depuis un script. Verifie, pas suppose.
	 * Un nom vide en destination neutralise la touche.
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|Input")
	static int32 RemapInputKeyInBlueprint(UBlueprint* Blueprint, const FString& FromKeyName,
		const FString& ToKeyName);

	// ──────────────────────────────────────────────────────────────
	// FINALISATION
	// ──────────────────────────────────────────────────────────────

	/**
	 * Synchronise le graphe visuel vers l'arbre runtime (equivalent du bouton "Compile" de
	 * l'editeur BT), marque l'asset modifie et le sauvegarde sur disque.
	 * Toujours appeler en dernier, apres avoir fini de construire/modifier le graphe.
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static bool CompileBehaviorTree(UBehaviorTree* Tree);

	/**
	 * Repositionne automatiquement tous les noeuds du graphe (X = ordre des freres au sein d'un
	 * meme parent, Y = profondeur dans l'arbre), pour eviter qu'un arbre genere par script avec
	 * beaucoup de noeuds ne les superpose tous en (0,0) et devienne illisible a l'ouverture dans
	 * l'editeur. Purement cosmetique : n'affecte ni la compilation ni le comportement runtime.
	 * A appeler apres avoir fini de connecter tous les noeuds (ConnectChild/SetTreeRoot), avant
	 * CompileBehaviorTree. HorizontalSpacing/VerticalSpacing <= 0 utilisent une valeur par defaut
	 * raisonnable (220 / 180).
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static bool AutoLayoutBehaviorTree(UBehaviorTree* Tree, int32 HorizontalSpacing, int32 VerticalSpacing);

	/**
	 * Filet de securite : resynchronise manuellement les Decorators/Services de l'arbre runtime
	 * (Tree->RootNode) depuis les SubNodes reellement attaches cote graphe editeur, noeud par
	 * noeud. A appeler si des decorators/services ajoutes via AddDecoratorToNode/AddServiceToNode
	 * n'apparaissent plus dans l'arbre compile apres un rechargement de l'editeur (bug observe ou
	 * Graph->UpdateAsset() seul ne suffit pas toujours a les transferer vers le runtime -- cause
	 * exacte non identifiee). CompileBehaviorTree l'appelle deja automatiquement ; exposee ici en
	 * plus pour pouvoir reparer un arbre deja construit sans tout reconstruire.
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static bool RepairDecoratorsAndServices(UBehaviorTree* Tree);

	// ------------------------------------------------------------
	// ATTACHE DIRECTE (contournement) -- ecrit dans l'arbre runtime SANS passer par le graphe
	// ------------------------------------------------------------
	// Observe sur ce projet : apres un rechargement de l'editeur, les decorators/services attaches
	// via AddDecoratorToNode/AddServiceToNode ont disparu non seulement de l'arbre compile mais
	// aussi des SubNodes cote graphe -- RepairDecoratorsAndServices() n'a donc rien a resynchroniser
	// (cause exacte non identifiee, cf. commentaires de CompileBehaviorTree). Les fonctions
	// ci-dessous contournent completement le graphe editeur : elles ecrivent directement dans
	// Tree->RootNode (FBTCompositeChild.Decorators / UBTCompositeNode.Services), qui est ce que
	// UBehaviorTreeComponent execute reellement au runtime -- donc garanties fonctionnelles en jeu,
	// au prix de ne pas etre visibles/editables a la souris dans l'editeur de graphe BT (le noeud
	// n'existe que cote runtime, pas cote UEdGraphNode). A preferer a AddDecoratorToNode/
	// AddServiceToNode tant que la cause du bug de synchronisation n'est pas trouvee.

	/**
	 * Attache un Decorator directement au lien parent->ChildNodeInstance dans l'arbre runtime
	 * (Tree->RootNode), sans passer par le graphe editeur. ChildNodeInstance : l'objet runtime deja
	 * cree (le UBTCompositeNode retourne par AddCompositeNode, ou le UBTTaskNode retourne par
	 * AddTaskNode -- recuperable depuis Python via le NodeInstance du UEdGraphNode, ou via
	 * ChildLink.child_composite / ChildLink.child_task en relisant Tree.RootNode.Children).
	 * Retourne l'instance du decorator cree (a reutiliser dans SetDecoratorInstanceProperty/
	 * SetDecoratorInstanceBlackboardKey), ou nullptr si ChildNodeInstance n'est trouve nulle part
	 * dans l'arbre. Sauvegarde l'asset lui-meme (pas besoin d'appeler CompileBehaviorTree apres).
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static UObject* AddDecoratorDirect(UBehaviorTree* Tree, UObject* ChildNodeInstance, const FString& DecoratorClassPath);

	/** Meme principe qu'AddDecoratorDirect, mais pour un Service attache directement a un noeud composite (UBTCompositeNode.Services). */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static UObject* AddServiceDirect(UBehaviorTree* Tree, UBTCompositeNode* ParentComposite, const FString& ServiceClassPath);

	/** Equivalent de SetNodeProperty, mais Instance est directement l'objet runtime (UBTDecorator/UBTService/UBTTaskNode), pas un UEdGraphNode. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static bool SetInstanceProperty(UObject* Instance, const FString& PropertyName, const FString& ValueAsString);

	/** Equivalent de SetNodeBlackboardKey, mais Instance est directement l'objet runtime. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoring|BehaviorTree")
	static bool SetInstanceBlackboardKey(UObject* Instance, const FString& PropertyName, UBlackboardData* Blackboard, const FString& KeyName);
};
