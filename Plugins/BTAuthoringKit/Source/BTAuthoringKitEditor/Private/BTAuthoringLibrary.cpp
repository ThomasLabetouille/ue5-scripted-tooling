#include "BTAuthoringLibrary.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "BehaviorTree/BlackboardData.h"
#include "BlackboardDataFactory.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTreeFactory.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"

// ──────────────────────────────────────────────────────────────────────────
// VERIFIER EN PRIORITE si la compilation echoue sur l'un de ces 4 includes :
// ce sont des classes editor-only du module BehaviorTreeEditor. Les noms de
// fichiers ci-dessous sont ceux observes sur les versions UE5 courantes, mais
// peuvent differer legerement selon la version exacte du moteur. Si un fichier
// est introuvable, chercher dans <EngineDir>/Source/Editor/BehaviorTreeEditor/
// Classes/ le fichier correspondant (chercher "GraphNode" dans le nom) et
// corriger le chemin d'include ici.
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "EdGraphSchema_BehaviorTree.h"
// ──────────────────────────────────────────────────────────────────────────

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"

// Pour ListAvailableNodeClasses() : IAssetRegistry::GetDerivedClassNames() est l'API standard du
// moteur pour lister TOUTES les classes derivees d'une classe donnee (natives + Blueprint,
// chargees ou non) -- c'est ce qu'utilisent en interne le Class Viewer et les pickers "Parent
// Class" d'Unreal. VERIFIER EN PRIORITE si la compilation echoue ici : la signature exacte
// (FTopLevelAssetPath vs FName) a change en UE5.1 ; ce fichier vise UE5.8 (FTopLevelAssetPath).
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/TopLevelAssetPath.h"

// Pour RepairDecoratorsAndServices() : SubNodes (Decorators/Services attaches a un noeud) est
// declare sur UAIGraphNode, la classe de base de UBehaviorTreeGraphNode (cf.
// Engine/Source/Editor/AIGraph/Classes/AIGraphNode.h). Module AIGraph deja en dependance.
#include "AIGraphNode.h"
#include "Engine/Blueprint.h"
#include "InputCoreTypes.h"

// ============================================================================
// Helpers internes
// ============================================================================

static bool SaveAssetInternal(UObject* Asset)
{
	if (!Asset) return false;

	Asset->MarkPackageDirty();
	UPackage* Package = Asset->GetOutermost();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	return UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
}

static UObject* CreateOrLoadAsset(const FString& PackagePath, const FString& AssetName, UClass* AssetClass, UFactory* Factory)
{
	const FString ObjectPath = PackagePath + TEXT("/") + AssetName;
	if (FPackageName::DoesPackageExist(ObjectPath))
	{
		if (UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath))
		{
			return Existing;
		}
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	return AssetTools.CreateAsset(AssetName, PackagePath, AssetClass, Factory);
}

static UEdGraphPin* FindFirstPinByDirection(UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
	if (!Node) return nullptr;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction)
		{
			return Pin;
		}
	}
	return nullptr;
}

// Cree le graphe editeur du BehaviorTree en suivant exactement le chemin reel de l'editeur
// (FBehaviorTreeEditor::RestoreBehaviorTree, module BehaviorTreeEditor, verifie contre le
// source du moteur) : FBlueprintEditorUtils::CreateNewGraph() plutot qu'un NewObject() a la
// main, puis Schema->CreateDefaultNodesForGraph() qui est ce qui cree reellement le noeud
// racine (Root) — SetTreeRoot() n'a donc normalement plus besoin de le creer lui-meme.
static UBehaviorTreeGraph* GetOrCreateGraph(UBehaviorTree* Tree)
{
	if (!Tree) return nullptr;

#if WITH_EDITORONLY_DATA
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(Tree->BTGraph);
	if (!Graph)
	{
		Tree->BTGraph = FBlueprintEditorUtils::CreateNewGraph(
			Tree, NAME_None, UBehaviorTreeGraph::StaticClass(), UEdGraphSchema_BehaviorTree::StaticClass());
		Graph = Cast<UBehaviorTreeGraph>(Tree->BTGraph);
		if (!Graph) return nullptr;

		const UEdGraphSchema* Schema = Graph->GetSchema();
		Schema->CreateDefaultNodesForGraph(*Graph);

		Graph->OnCreated();
		Graph->Initialize();
	}
	return Graph;
#else
	return nullptr;
#endif
}

// Cree un noeud de graphe pour une instance de UBTNode/UBTCompositeNode donnee, l'ajoute au
// graphe et le positionne. Utilise FGraphNodeCreator<T>, le pattern reellement employe par le
// moteur (verifie contre Engine/Source/Runtime/Engine/Classes/EdGraph/EdGraph.h — CreateNode()
// accepte un TSubclassOf<NodeType> en parametre optionnel, donc utilisable meme quand la classe
// concrete n'est connue qu'a l'execution) et contre l'usage reel dans
// Engine/Source/Editor/BehaviorTreeEditor/Private/BehaviorTreeGraph.cpp.
static UBehaviorTreeGraphNode* CreateGraphNodeForInstance(UBehaviorTreeGraph* Graph, UObject* NodeInstance,
	TSubclassOf<UBehaviorTreeGraphNode> GraphNodeClass, const FString& Label, int32 PosX, int32 PosY)
{
	if (!Graph || !NodeInstance || !GraphNodeClass) return nullptr;

	FGraphNodeCreator<UBehaviorTreeGraphNode> NodeBuilder(*Graph);
	UBehaviorTreeGraphNode* GraphNode = NodeBuilder.CreateNode(/*bSelectNewNode*/ false, GraphNodeClass);
	GraphNode->NodePosX = PosX;
	GraphNode->NodePosY = PosY;
	GraphNode->NodeInstance = NodeInstance;

	if (!Label.IsEmpty())
	{
		// VERIFIER : selon la version, le libelle affiche peut venir de NodeInstance->NodeName
		// (UBTNode::NodeName, UPROPERTY EditAnywhere) plutot que du commentaire du graphe. Les
		// deux sont poses ici pour couvrir les deux cas.
		GraphNode->NodeComment = Label;
	}

	// Finalize() appelle CreateNewGuid()/PostPlacedNewNode() et AllocateDefaultPins() (si aucun
	// pin n'existe deja) — ne pas les rappeler a la main, FGraphNodeCreator s'en charge et son
	// destructeur verifie (check) que Finalize() a bien ete appele.
	NodeBuilder.Finalize();
	return GraphNode;
}

// ============================================================================
// BLACKBOARD
// ============================================================================

UBlackboardData* UBTAuthoringLibrary::CreateBlackboard(const FString& PackagePath, const FString& AssetName)
{
	UBlackboardDataFactory* Factory = NewObject<UBlackboardDataFactory>();
	UObject* Asset = CreateOrLoadAsset(PackagePath, AssetName, UBlackboardData::StaticClass(), Factory);
	return Cast<UBlackboardData>(Asset);
}

bool UBTAuthoringLibrary::AddBlackboardKey(UBlackboardData* Blackboard, const FString& KeyName, EBTAuthoringKeyType KeyType, UClass* ObjectFilterClass)
{
	if (!Blackboard) return false;

	const FName KeyFName(*KeyName);
	for (const FBlackboardEntry& Existing : Blackboard->Keys)
	{
		if (Existing.EntryName == KeyFName)
		{
			return true; // deja presente : idempotent, pas une erreur
		}
	}

	UBlackboardKeyType* NewKeyType = nullptr;
	switch (KeyType)
	{
	case EBTAuthoringKeyType::Object:
	{
		UBlackboardKeyType_Object* K = NewObject<UBlackboardKeyType_Object>(Blackboard);
		K->BaseClass = ObjectFilterClass ? ObjectFilterClass : AActor::StaticClass();
		NewKeyType = K;
		break;
	}
	case EBTAuthoringKeyType::Class:
	{
		UBlackboardKeyType_Class* K = NewObject<UBlackboardKeyType_Class>(Blackboard);
		K->BaseClass = ObjectFilterClass ? ObjectFilterClass : UObject::StaticClass();
		NewKeyType = K;
		break;
	}
	case EBTAuthoringKeyType::Vector:
		NewKeyType = NewObject<UBlackboardKeyType_Vector>(Blackboard);
		break;
	case EBTAuthoringKeyType::Bool:
		NewKeyType = NewObject<UBlackboardKeyType_Bool>(Blackboard);
		break;
	case EBTAuthoringKeyType::Float:
		NewKeyType = NewObject<UBlackboardKeyType_Float>(Blackboard);
		break;
	case EBTAuthoringKeyType::Int:
		NewKeyType = NewObject<UBlackboardKeyType_Int>(Blackboard);
		break;
	case EBTAuthoringKeyType::Name:
		NewKeyType = NewObject<UBlackboardKeyType_Name>(Blackboard);
		break;
	case EBTAuthoringKeyType::String:
		NewKeyType = NewObject<UBlackboardKeyType_String>(Blackboard);
		break;
	case EBTAuthoringKeyType::Enum:
		// VERIFIER : UBlackboardKeyType_Enum a aussi besoin d'un UEnum* (proprietes EnumType /
		// EnumName selon la version). Pas expose ici pour rester simple ; a completer si un noeud
		// a reellement besoin d'une cle Enum (contournement en attendant : cle Int/Name).
		NewKeyType = NewObject<UBlackboardKeyType_Enum>(Blackboard);
		break;
	default:
		return false;
	}

	FBlackboardEntry NewEntry;
	NewEntry.EntryName = KeyFName;
	NewEntry.KeyType = NewKeyType;
	Blackboard->Keys.Add(NewEntry);

	return SaveAssetInternal(Blackboard);
}

// ============================================================================
// BEHAVIOR TREE — creation
// ============================================================================

UBehaviorTree* UBTAuthoringLibrary::CreateBehaviorTree(const FString& PackagePath, const FString& AssetName, UBlackboardData* Blackboard)
{
	UBehaviorTreeFactory* Factory = NewObject<UBehaviorTreeFactory>();
	UObject* Asset = CreateOrLoadAsset(PackagePath, AssetName, UBehaviorTree::StaticClass(), Factory);
	UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset);

	if (Tree && Blackboard)
	{
		Tree->BlackboardAsset = Blackboard;
	}

	return Tree;
}

// ============================================================================
// DECOUVERTE
// ============================================================================

static UClass* ResolveBTBaseClass(EBTAuthoringNodeKind NodeKind)
{
	switch (NodeKind)
	{
	case EBTAuthoringNodeKind::Task:      return UBTTaskNode::StaticClass();
	case EBTAuthoringNodeKind::Decorator: return UBTDecorator::StaticClass();
	case EBTAuthoringNodeKind::Service:   return UBTService::StaticClass();
	}
	return nullptr;
}

TArray<FString> UBTAuthoringLibrary::ListAvailableNodeClasses(EBTAuthoringNodeKind NodeKind)
{
	TArray<FString> Result;

	UClass* BaseClass = ResolveBTBaseClass(NodeKind);
	if (!BaseClass) return Result;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FTopLevelAssetPath> BaseClassPaths;
	BaseClassPaths.Add(BaseClass->GetClassPathName());
	TSet<FTopLevelAssetPath> ExcludedClassPaths;
	TSet<FTopLevelAssetPath> DerivedClassPaths;
	AssetRegistry.GetDerivedClassNames(BaseClassPaths, ExcludedClassPaths, DerivedClassPaths);

	TSet<FString> Seen;
	for (const FTopLevelAssetPath& ClassPath : DerivedClassPaths)
	{
		FString PathString = ClassPath.ToString();

		// Si la classe est deja chargee en memoire, on prefere son GetPathName() reel (identique
		// au format attendu par AddTaskNode/AddDecoratorToNode/AddServiceToNode plus bas) et on
		// filtre les classes abstraites/deprecated/obsoletes -- invisible depuis un simple chemin
		// d'asset registry non charge, d'ou ce filtre optionnel plutot qu'obligatoire.
		if (UClass* LoadedClass = FindObject<UClass>(nullptr, *PathString))
		{
			if (LoadedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			PathString = LoadedClass->GetPathName();
		}

		bool bAlreadySeen = false;
		Seen.Add(PathString, &bAlreadySeen);
		if (!bAlreadySeen)
		{
			Result.Add(PathString);
		}
	}

	Result.Sort();
	return Result;
}

// ============================================================================
// BEHAVIOR TREE — noeuds
// ============================================================================

UEdGraphNode* UBTAuthoringLibrary::AddCompositeNode(UBehaviorTree* Tree, EBTAuthoringCompositeType CompositeType, const FString& NodeLabel, int32 PosX, int32 PosY)
{
	UBehaviorTreeGraph* Graph = GetOrCreateGraph(Tree);
	if (!Graph) return nullptr;

	UBTCompositeNode* CompositeInstance = nullptr;
	switch (CompositeType)
	{
	case EBTAuthoringCompositeType::Selector:
		CompositeInstance = NewObject<UBTComposite_Selector>(Tree);
		break;
	case EBTAuthoringCompositeType::Sequence:
		CompositeInstance = NewObject<UBTComposite_Sequence>(Tree);
		break;
	case EBTAuthoringCompositeType::SimpleParallel:
		CompositeInstance = NewObject<UBTComposite_SimpleParallel>(Tree);
		break;
	}
	if (!CompositeInstance) return nullptr;

	return CreateGraphNodeForInstance(Graph, CompositeInstance, UBehaviorTreeGraphNode_Composite::StaticClass(), NodeLabel, PosX, PosY);
}

UEdGraphNode* UBTAuthoringLibrary::AddTaskNode(UBehaviorTree* Tree, const FString& TaskClassPath, const FString& NodeLabel, int32 PosX, int32 PosY)
{
	UBehaviorTreeGraph* Graph = GetOrCreateGraph(Tree);
	if (!Graph) return nullptr;

	UClass* TaskClass = LoadClass<UBTTaskNode>(nullptr, *TaskClassPath);
	if (!TaskClass)
	{
		UE_LOG(LogTemp, Error, TEXT("BTAuthoringKit: classe Task introuvable: %s"), *TaskClassPath);
		return nullptr;
	}

	UBTTaskNode* TaskInstance = NewObject<UBTTaskNode>(Tree, TaskClass);
	return CreateGraphNodeForInstance(Graph, TaskInstance, UBehaviorTreeGraphNode_Task::StaticClass(), NodeLabel, PosX, PosY);
}

UEdGraphNode* UBTAuthoringLibrary::AddDecoratorToNode(UEdGraphNode* OwnerNode, const FString& DecoratorClassPath, const FString& DecoratorLabel)
{
	UBehaviorTreeGraphNode* Owner = Cast<UBehaviorTreeGraphNode>(OwnerNode);
	if (!Owner) return nullptr;

	UEdGraph* Graph = Owner->GetGraph();
	if (!Graph) return nullptr;

	UClass* DecoratorClass = LoadClass<UBTDecorator>(nullptr, *DecoratorClassPath);
	if (!DecoratorClass) return nullptr;

	// L'instance runtime (le UBTDecorator lui-meme, pas le noeud de graphe) doit avoir le meme
	// Outer que les composites/tasks crees ailleurs dans ce fichier : le UBehaviorTree (l'asset),
	// pas le UBehaviorTreeGraph (l'editeur). C'est exactement ce que fait le moteur lui-meme dans
	// UAIGraphNode::PostPlacedNewNode() : `MyGraph->GetOuter()`, cf.
	// Engine/Source/Editor/AIGraph/Private/AIGraphNode.cpp.
	UBTDecorator* DecoratorInstance = NewObject<UBTDecorator>(Graph->GetOuter(), DecoratorClass);

	// AddSubNode() (herite de UAIGraphNode, cf. Engine/Source/Editor/AIGraph/Private/AIGraphNode.cpp)
	// fait tout le travail d'attache : Rename() sur le graphe, ParentNode, CreateNewGuid(),
	// PostPlacedNewNode(), AllocateDefaultPins(), AutowireNewNode(), ajout a SubNodes, et appelle
	// lui-meme Graph->UpdateAsset() — ne rien dupliquer manuellement ici.
	//
	// CORRECTIF 2026-08-15 (cause racine du bug "les decorators disparaissent") : NodeInstance DOIT
	// etre assigne AVANT AddSubNode(), pas apres. AddSubNode() declenche en interne
	// Graph->UpdateAsset(), qui reconstruit l'arbre runtime a partir du graphe et ecarte au passage
	// les sous-noeuds dont NodeInstance est nul. En assignant l'instance apres l'appel, le decorator
	// etait donc elimine dans la foulee de sa propre creation : absent de l'arbre compile, absent
	// des SubNodes du graphe (d'ou RepairDecoratorsAndServices() qui ne trouvait jamais rien a
	// resynchroniser), et invisible dans l'editeur — le tout sans le moindre message d'erreur.
	UBehaviorTreeGraphNode_Decorator* DecoratorGraphNode = NewObject<UBehaviorTreeGraphNode_Decorator>(Graph);
	DecoratorGraphNode->NodeInstance = DecoratorInstance;

	if (!DecoratorLabel.IsEmpty())
	{
		DecoratorGraphNode->NodeComment = DecoratorLabel;
	}

	Owner->AddSubNode(DecoratorGraphNode, Graph);

	return DecoratorGraphNode;
}

UEdGraphNode* UBTAuthoringLibrary::AddServiceToNode(UEdGraphNode* OwnerCompositeNode, const FString& ServiceClassPath, const FString& ServiceLabel)
{
	UBehaviorTreeGraphNode* Owner = Cast<UBehaviorTreeGraphNode>(OwnerCompositeNode);
	if (!Owner) return nullptr;

	UEdGraph* Graph = Owner->GetGraph();
	if (!Graph) return nullptr;

	UClass* ServiceClass = LoadClass<UBTService>(nullptr, *ServiceClassPath);
	if (!ServiceClass) return nullptr;

	// Meme raisonnement que AddDecoratorToNode ci-dessus pour l'Outer de l'instance runtime.
	UBTService* ServiceInstance = NewObject<UBTService>(Graph->GetOuter(), ServiceClass);

	// Meme correctif d'ordre que AddDecoratorToNode ci-dessus : NodeInstance avant AddSubNode(),
	// sinon UpdateAsset() (declenche par AddSubNode) ecarte le sous-noeud a instance nulle.
	UBehaviorTreeGraphNode_Service* ServiceGraphNode = NewObject<UBehaviorTreeGraphNode_Service>(Graph);
	ServiceGraphNode->NodeInstance = ServiceInstance;

	if (!ServiceLabel.IsEmpty())
	{
		ServiceGraphNode->NodeComment = ServiceLabel;
	}

	Owner->AddSubNode(ServiceGraphNode, Graph);

	return ServiceGraphNode;
}

bool UBTAuthoringLibrary::ConnectChild(UEdGraphNode* ParentNode, UEdGraphNode* ChildNode)
{
	UEdGraphPin* OutPin = FindFirstPinByDirection(ParentNode, EGPD_Output);
	UEdGraphPin* InPin = FindFirstPinByDirection(ChildNode, EGPD_Input);
	if (!OutPin || !InPin) return false;

	OutPin->MakeLinkTo(InPin);
	return true;
}

bool UBTAuthoringLibrary::SetTreeRoot(UBehaviorTree* Tree, UEdGraphNode* RootChildNode)
{
	UBehaviorTreeGraph* Graph = GetOrCreateGraph(Tree);
	if (!Graph || !RootChildNode) return false;

	UBehaviorTreeGraphNode_Root* RootNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		RootNode = Cast<UBehaviorTreeGraphNode_Root>(Node);
		if (RootNode) break;
	}

	if (!RootNode)
	{
		// GetOrCreateGraph() appelle deja Schema->CreateDefaultNodesForGraph(), qui cree ce noeud
		// racine automatiquement pour un nouveau graphe — ce chemin ne devrait normalement pas
		// etre emprunte, mais on le couvre par securite avec le meme pattern verifie que
		// CreateGraphNodeForInstance (FGraphNodeCreator).
		FGraphNodeCreator<UBehaviorTreeGraphNode_Root> NodeBuilder(*Graph);
		RootNode = NodeBuilder.CreateNode(/*bSelectNewNode*/ false);
		NodeBuilder.Finalize();
	}

	return ConnectChild(RootNode, RootChildNode);
}

// ============================================================================
// PROPRIETES DE NOEUD (reflexion generique)
// ============================================================================

// Coeur reflexif partage entre SetNodeProperty (Instance = NodeInstance d'un UEdGraphNode) et
// SetInstanceProperty (Instance = objet runtime direct, cf. section ATTACHE DIRECTE plus bas) --
// factorise ici pour ne pas dupliquer la logique de fallback struct entre les deux points d'entree.
static bool SetInstancePropertyInternal(UObject* Instance, const FString& PropertyName, const FString& ValueAsString)
{
	if (!Instance) return false;

	FProperty* Prop = Instance->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop) return false;

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Instance);
	const TCHAR* Result = Prop->ImportText_Direct(*ValueAsString, ValuePtr, Instance, PPF_None);
	if (Result != nullptr)
	{
		return true;
	}

	// Confirme sur ce projet (UE5.8) : de nombreuses proprietes de noeuds standards qui etaient de
	// simples float/int/bool dans les versions anterieures (WaitTime sur BTTask_Wait,
	// AcceptableRadius sur BTTask_MoveTo, etc.) sont maintenant des structs FValueOrBBKey_Float /
	// _Int32 / _Bool (UE5.5+, cf. BehaviorTree/ValueOrBBKey.h), qui permettent de lier la valeur a
	// une cle Blackboard. Toutes exposent une sous-propriete "DefaultValue" (protected en C++, mais
	// visible en reflexion) : si l'import direct du texte brut echoue et que la propriete est un
	// struct, on retente avec le format texte standard d'une UStruct, "(DefaultValue=...)".
	if (CastField<FStructProperty>(Prop))
	{
		const FString WrappedValue = FString::Printf(TEXT("(DefaultValue=%s)"), *ValueAsString);
		Result = Prop->ImportText_Direct(*WrappedValue, ValuePtr, Instance, PPF_None);
		return Result != nullptr;
	}

	return false;
}

// Meme principe que SetInstancePropertyInternal ci-dessus, pour les proprietes FBlackboardKeySelector.
static bool SetInstanceBlackboardKeyInternal(UObject* Instance, const FString& PropertyName, UBlackboardData* Blackboard, const FString& KeyName)
{
	if (!Instance || !Blackboard) return false;

	FStructProperty* Prop = FindFProperty<FStructProperty>(Instance->GetClass(), *PropertyName);
	if (!Prop || Prop->Struct != FBlackboardKeySelector::StaticStruct()) return false;

	FBlackboardKeySelector* Selector = Prop->ContainerPtrToValuePtr<FBlackboardKeySelector>(Instance);
	Selector->SelectedKeyName = FName(*KeyName);
	Selector->ResolveSelectedKey(*Blackboard);
	return true;
}

bool UBTAuthoringLibrary::SetNodeProperty(UEdGraphNode* Node, const FString& PropertyName, const FString& ValueAsString)
{
	UBehaviorTreeGraphNode* BTGraphNode = Cast<UBehaviorTreeGraphNode>(Node);
	if (!BTGraphNode || !BTGraphNode->NodeInstance) return false;

	return SetInstancePropertyInternal(BTGraphNode->NodeInstance, PropertyName, ValueAsString);
}

bool UBTAuthoringLibrary::SetNodeBlackboardKey(UEdGraphNode* Node, const FString& PropertyName, UBlackboardData* Blackboard, const FString& KeyName)
{
	UBehaviorTreeGraphNode* BTGraphNode = Cast<UBehaviorTreeGraphNode>(Node);
	if (!BTGraphNode || !BTGraphNode->NodeInstance) return false;

	return SetInstanceBlackboardKeyInternal(BTGraphNode->NodeInstance, PropertyName, Blackboard, KeyName);
}

// ============================================================================
// FINALISATION
// ============================================================================

bool UBTAuthoringLibrary::CompileBehaviorTree(UBehaviorTree* Tree)
{
	if (!Tree) return false;

	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(Tree->BTGraph);
	if (!Graph) return false;

	// VERIFIER EN PRIORITE (l'appel le plus incertain de tout ce fichier) : nom exact de la
	// fonction qui reconstruit l'arbre runtime (Tree->RootNode) a partir du graphe visuel —
	// l'equivalent de cliquer sur "Compile"/sauvegarder dans l'editeur BT. Si UpdateAsset()
	// n'existe pas ou a une signature differente dans cette version d'engine, chercher dans
	// BehaviorTreeGraph.h/.cpp (module BehaviorTreeEditor) une fonction de reconstruction —
	// termes a chercher : "UpdateAsset", "RebuildTree", "OnSave".
	Graph->UpdateAsset();

	// Filet de securite : UpdateAsset() seul n'a pas toujours transfere de facon fiable les
	// Decorators/Services vers l'arbre runtime (observe sur ce projet -- cause exacte non
	// identifiee, reproduit meme sur un arbre minimal fraichement construit). RepairDecoratorsAndServices()
	// resynchronise manuellement depuis le graphe, en plus de UpdateAsset() plutot qu'a sa place :
	// idempotent sur un arbre deja correct, donc sans risque de l'appeler systematiquement ici.
	RepairDecoratorsAndServices(Tree);

	return SaveAssetInternal(Tree);
}

// ============================================================================
// AUTO-LAYOUT
// ============================================================================

// Enfants directs (dans l'ordre des liens) d'un noeud composite du graphe -- suit le meme pin
// Output que ConnectChild()/AddCompositeNode plus haut, mais en lecture plutot qu'en ecriture.
static TArray<UEdGraphNode*> GetChildGraphNodes(UEdGraphNode* Node)
{
	TArray<UEdGraphNode*> Children;
	UEdGraphPin* OutPin = FindFirstPinByDirection(Node, EGPD_Output);
	if (!OutPin) return Children;

	for (UEdGraphPin* LinkedPin : OutPin->LinkedTo)
	{
		if (LinkedPin && LinkedPin->GetOwningNode())
		{
			Children.Add(LinkedPin->GetOwningNode());
		}
	}
	return Children;
}

// Positionne recursivement Node et ses descendants ; retourne la largeur (en "slots" de
// HorizontalSpacing) occupee par le sous-arbre, pour que le parent puisse se centrer au-dessus de
// ses enfants sans les chevaucher. Algorithme delibs simple (pas un vrai Reingold-Tilford) --
// suffisant pour la taille habituelle d'un Behavior Tree (quelques dizaines de noeuds), pas
// optimise pour des arbres tres larges/desequilibres.
static int32 LayoutSubtree(UEdGraphNode* Node, int32 Depth, int32 LeftmostSlot, int32 HorizontalSpacing, int32 VerticalSpacing, TSet<UEdGraphNode*>& Visited)
{
	if (!Node) return 0;

	bool bAlreadyVisited = false;
	Visited.Add(Node, &bAlreadyVisited);
	if (bAlreadyVisited) return 0;

	TArray<UEdGraphNode*> Children = GetChildGraphNodes(Node);

	if (Children.Num() == 0)
	{
		Node->NodePosX = LeftmostSlot * HorizontalSpacing;
		Node->NodePosY = Depth * VerticalSpacing;
		return 1;
	}

	int32 SlotCursor = LeftmostSlot;
	int32 TotalWidth = 0;
	for (UEdGraphNode* Child : Children)
	{
		const int32 ChildWidth = LayoutSubtree(Child, Depth + 1, SlotCursor, HorizontalSpacing, VerticalSpacing, Visited);
		SlotCursor += ChildWidth;
		TotalWidth += ChildWidth;
	}

	Node->NodePosX = (LeftmostSlot + (TotalWidth - 1) / 2) * HorizontalSpacing;
	Node->NodePosY = Depth * VerticalSpacing;

	return FMath::Max(TotalWidth, 1);
}

bool UBTAuthoringLibrary::AutoLayoutBehaviorTree(UBehaviorTree* Tree, int32 HorizontalSpacing, int32 VerticalSpacing)
{
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(Tree ? Tree->BTGraph : nullptr);
	if (!Graph) return false;

	UBehaviorTreeGraphNode_Root* RootNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		RootNode = Cast<UBehaviorTreeGraphNode_Root>(Node);
		if (RootNode) break;
	}
	if (!RootNode) return false;

	if (HorizontalSpacing <= 0) HorizontalSpacing = 220;
	if (VerticalSpacing <= 0) VerticalSpacing = 180;

	// Les decorators/services (sub-nodes) ne sont pas des UEdGraphNode positionnes independamment
	// dans ce layout : l'editeur BT les affiche toujours colles au noeud parent qui les porte, pas
	// besoin de les traiter ici.
	TSet<UEdGraphNode*> Visited;
	LayoutSubtree(RootNode, 0, 0, HorizontalSpacing, VerticalSpacing, Visited);

	Tree->MarkPackageDirty();
	return true;
}

// ============================================================================
// REPARATION DECORATORS/SERVICES
// ============================================================================

// Cote graphe, un noeud (composite ou task) porte ses Decorators/Services attaches comme des
// UAIGraphNode dans son tableau SubNodes (voir AddDecoratorToNode/AddServiceToNode plus haut, et
// AIGraphNode.h). On separe ici les deux types par un simple Cast sur leur NodeInstance runtime.
static void CollectGraphNodeDecoratorsAndServices(UBehaviorTreeGraphNode* GraphNode, TArray<UBTDecorator*>& OutDecorators, TArray<UBTService*>& OutServices)
{
	if (!GraphNode) return;

	for (UAIGraphNode* SubNode : GraphNode->SubNodes)
	{
		if (!SubNode || !SubNode->NodeInstance) continue;

		if (UBTDecorator* Dec = Cast<UBTDecorator>(SubNode->NodeInstance))
		{
			OutDecorators.Add(Dec);
		}
		else if (UBTService* Svc = Cast<UBTService>(SubNode->NodeInstance))
		{
			OutServices.Add(Svc);
		}
	}
}

// Retrouve le UEdGraphNode dont le NodeInstance runtime correspond a Instance (le UBTCompositeNode
// ou UBTTaskNode dont on veut retrouver les SubNodes du cote graphe). Balayage lineaire de
// Graph->Nodes -- suffisant pour la taille habituelle d'un Behavior Tree (quelques dizaines de
// noeuds), pas optimise pour des arbres tres larges.
static UBehaviorTreeGraphNode* FindGraphNodeForInstance(UBehaviorTreeGraph* Graph, UObject* Instance)
{
	if (!Graph || !Instance) return nullptr;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(Node);
		if (BTNode && BTNode->NodeInstance == Instance)
		{
			return BTNode;
		}
	}
	return nullptr;
}

// Parcourt recursivement l'arbre runtime (Composite + ses Children) et resynchronise, pour
// chaque lien parent->enfant, les Decorators depuis le graphe (les Decorators vivent sur le LIEN,
// FBTCompositeChild.Decorators -- pas sur le noeud enfant lui-meme), et pour chaque noeud
// composite rencontre, ses propres Services (les Services vivent eux directement sur le
// UBTCompositeNode, cf. Engine/Source/Runtime/AIModule/Classes/BehaviorTree/BTCompositeNode.h).
static void RepairCompositeRecursive(UBehaviorTreeGraph* Graph, UBTCompositeNode* Composite)
{
	if (!Composite) return;

	if (UBehaviorTreeGraphNode* CompositeGraphNode = FindGraphNodeForInstance(Graph, Composite))
	{
		TArray<UBTDecorator*> UnusedDecoratorsOnComposite;
		TArray<UBTService*> Services;
		CollectGraphNodeDecoratorsAndServices(CompositeGraphNode, UnusedDecoratorsOnComposite, Services);
		Composite->Services = Services;
	}

	for (FBTCompositeChild& Child : Composite->Children)
	{
		UObject* ChildInstance = Child.ChildComposite
			? static_cast<UObject*>(Child.ChildComposite)
			: static_cast<UObject*>(Child.ChildTask);

		if (UBehaviorTreeGraphNode* ChildGraphNode = FindGraphNodeForInstance(Graph, ChildInstance))
		{
			TArray<UBTDecorator*> Decorators;
			TArray<UBTService*> UnusedServicesOnChildLink;
			CollectGraphNodeDecoratorsAndServices(ChildGraphNode, Decorators, UnusedServicesOnChildLink);
			Child.Decorators = Decorators;
		}

		if (Child.ChildComposite)
		{
			RepairCompositeRecursive(Graph, Child.ChildComposite);
		}
	}
}

bool UBTAuthoringLibrary::RepairDecoratorsAndServices(UBehaviorTree* Tree)
{
	if (!Tree || !Tree->RootNode) return false;

	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(Tree->BTGraph);
	if (!Graph) return false;

	RepairCompositeRecursive(Graph, Tree->RootNode);

	return SaveAssetInternal(Tree);
}

// ============================================================================
// ATTACHE DIRECTE (contournement) -- ecrit dans l'arbre runtime SANS passer par le graphe
// ============================================================================
// Voir le commentaire dans BTAuthoringLibrary.h : observe sur ce projet, apres un rechargement de
// l'editeur, les Decorators/Services attaches via AddDecoratorToNode/AddServiceToNode disparaissent
// non seulement de l'arbre compile (Tree->RootNode) mais aussi du graphe editeur (SubNodes) --
// RepairDecoratorsAndServices() n'a donc rien a resynchroniser (repair_and_verify_report.json :
// chase_decorators_after_repair=0, sit_decorators_after_repair=0). Cause exacte non identifiee.
// Les fonctions ci-dessous ecrivent directement dans Tree->RootNode (FBTCompositeChild.Decorators /
// UBTCompositeNode.Services), qui est ce que UBehaviorTreeComponent execute reellement au runtime --
// donc garanties fonctionnelles en jeu, au prix de ne pas etre visibles/editables a la souris dans
// l'editeur de graphe BT (le noeud n'existe que cote runtime, pas cote UEdGraphNode).

// Recherche recursive du FBTCompositeChild dont ChildComposite ou ChildTask correspond a
// ChildInstance, en balayant Tree->RootNode et tous ses descendants composites. Meme limite que
// FindGraphNodeForInstance plus haut (balayage lineaire, suffisant pour la taille habituelle d'un
// Behavior Tree).
static FBTCompositeChild* FindChildLinkForInstance(UBTCompositeNode* Composite, UObject* ChildInstance)
{
	if (!Composite) return nullptr;

	for (FBTCompositeChild& Child : Composite->Children)
	{
		UObject* CurInstance = Child.ChildComposite
			? static_cast<UObject*>(Child.ChildComposite)
			: static_cast<UObject*>(Child.ChildTask);

		if (CurInstance == ChildInstance)
		{
			return &Child;
		}

		if (Child.ChildComposite)
		{
			if (FBTCompositeChild* Found = FindChildLinkForInstance(Child.ChildComposite, ChildInstance))
			{
				return Found;
			}
		}
	}
	return nullptr;
}

UObject* UBTAuthoringLibrary::AddDecoratorDirect(UBehaviorTree* Tree, UObject* ChildNodeInstance, const FString& DecoratorClassPath)
{
	if (!Tree || !Tree->RootNode || !ChildNodeInstance) return nullptr;

	FBTCompositeChild* ChildLink = FindChildLinkForInstance(Tree->RootNode, ChildNodeInstance);
	if (!ChildLink)
	{
		UE_LOG(LogTemp, Error, TEXT("BTAuthoringKit: AddDecoratorDirect -- ChildNodeInstance introuvable dans Tree->RootNode (verifier que l'instance passee est bien le NodeInstance runtime, pas le UEdGraphNode)."));
		return nullptr;
	}

	UClass* DecoratorClass = LoadClass<UBTDecorator>(nullptr, *DecoratorClassPath);
	if (!DecoratorClass) return nullptr;

	// Meme Outer que les instances runtime creees ailleurs dans ce fichier (cf. AddDecoratorToNode) :
	// le UBehaviorTree lui-meme.
	UBTDecorator* DecoratorInstance = NewObject<UBTDecorator>(Tree, DecoratorClass);
	ChildLink->Decorators.Add(DecoratorInstance);

	SaveAssetInternal(Tree);
	return DecoratorInstance;
}

UObject* UBTAuthoringLibrary::AddServiceDirect(UBehaviorTree* Tree, UBTCompositeNode* ParentComposite, const FString& ServiceClassPath)
{
	if (!Tree || !ParentComposite) return nullptr;

	UClass* ServiceClass = LoadClass<UBTService>(nullptr, *ServiceClassPath);
	if (!ServiceClass) return nullptr;

	UBTService* ServiceInstance = NewObject<UBTService>(Tree, ServiceClass);
	ParentComposite->Services.Add(ServiceInstance);

	SaveAssetInternal(Tree);
	return ServiceInstance;
}

bool UBTAuthoringLibrary::SetInstanceProperty(UObject* Instance, const FString& PropertyName, const FString& ValueAsString)
{
	return SetInstancePropertyInternal(Instance, PropertyName, ValueAsString);
}

bool UBTAuthoringLibrary::SetInstanceBlackboardKey(UObject* Instance, const FString& PropertyName, UBlackboardData* Blackboard, const FString& KeyName)
{
	return SetInstanceBlackboardKeyInternal(Instance, PropertyName, Blackboard, KeyName);
}

// ============================================================================
// DECOUVERTE DANS UN ARBRE EXISTANT
// ============================================================================

TArray<UEdGraphNode*> UBTAuthoringLibrary::GetGraphNodes(UBehaviorTree* Tree, bool bIncludeSubNodes)
{
	TArray<UEdGraphNode*> Result;
	if (!Tree)
	{
		return Result;
	}

	// Ne PAS passer par GetOrCreateGraph() ici : cette fonction est en lecture seule et ne doit
	// jamais fabriquer un graphe vide en effet de bord sur un asset qu'on ne fait qu'inspecter.
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(Tree->BTGraph);
	if (!Graph)
	{
		return Result;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		Result.Add(Node);

		if (!bIncludeSubNodes)
		{
			continue;
		}

		// Decorators et services vivent dans SubNodes (declare sur UAIGraphNode), pas dans
		// Graph->Nodes : sans ce parcours, un decorator existant reste inatteignable.
		if (UAIGraphNode* AINode = Cast<UAIGraphNode>(Node))
		{
			for (UAIGraphNode* SubNode : AINode->SubNodes)
			{
				if (SubNode)
				{
					Result.Add(SubNode);
				}
			}
		}
	}

	return Result;
}

UObject* UBTAuthoringLibrary::GetGraphNodeInstance(UEdGraphNode* GraphNode)
{
	UAIGraphNode* AINode = Cast<UAIGraphNode>(GraphNode);
	return AINode ? AINode->NodeInstance : nullptr;
}

UEdGraphNode* UBTAuthoringLibrary::GetGraphNodeParent(UEdGraphNode* GraphNode)
{
	UAIGraphNode* AINode = Cast<UAIGraphNode>(GraphNode);
	return AINode ? Cast<UEdGraphNode>(AINode->ParentNode) : nullptr;
}

// ============================================================================
// REMAPPAGE D'UNE TOUCHE DANS UN GRAPHE BLUEPRINT
// ============================================================================

int32 UBTAuthoringLibrary::RemapInputKeyInBlueprint(UBlueprint* Blueprint,
	const FString& FromKeyName, const FString& ToKeyName)
{
	if (!Blueprint || FromKeyName.IsEmpty())
	{
		return 0;
	}

	const FKey FromKey(*FromKeyName);
	const FKey ToKey = ToKeyName.IsEmpty() ? FKey() : FKey(*ToKeyName);
	if (!FromKey.IsValid())
	{
		return 0;
	}

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);

	int32 Count = 0;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			// Reflexion : on cherche une propriete FKey nommee "InputKey", commune aux noeuds
			// d'evenement clavier ordinaires ET de debogage, sans dependre de leurs classes.
			FStructProperty* const Prop =
				FindFProperty<FStructProperty>(Node->GetClass(), TEXT("InputKey"));
			if (!Prop || Prop->Struct != TBaseStructure<FKey>::Get())
			{
				continue;
			}

			FKey* const Value = Prop->ContainerPtrToValuePtr<FKey>(Node);
			if (!Value || *Value != FromKey)
			{
				continue;
			}

			Node->Modify();
			*Value = ToKey;
			++Count;
		}
	}

	if (Count > 0)
	{
		Blueprint->Modify();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	return Count;
}
