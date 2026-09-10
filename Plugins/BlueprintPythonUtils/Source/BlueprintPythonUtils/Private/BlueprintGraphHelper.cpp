#include "BlueprintGraphHelper.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_EditablePinBase.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintNodeSpawner.h"

// ── helpers ───────────────────────────────────────────────────────────────────

static UEdGraph* FindGraph(UBlueprint* BP, const FString& GraphName)
{
    if (!BP) return nullptr;
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (UEdGraph* G : Graphs)
        if (GraphName.IsEmpty() || G->GetName() == GraphName)
            return G;
    return nullptr;
}

static UEdGraphPin* FindAnyPin(UEdGraphNode* Node, const FString& PinName)
{
    if (!Node) return nullptr;
    UEdGraphPin* P = Node->FindPin(*PinName);
    if (P) return P;
    for (UEdGraphPin* Pin : Node->Pins)
        if (Pin && Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
            return Pin;
    return nullptr;
}

static UClass* FindClassByName(const FString& ClassName)
{
    // Fix 2026-07-20 : l'ancienne implementation ne parcourait que les UClass deja chargees en
    // memoire (TObjectIterator), sans le fallback LoadClass()/suffixe "_C" que RoomGenerator
    // applique deja dans ResolveClass() (BlueprintEditingSubsystem.cpp) — une classe Blueprint
    // pas encore chargee (ex. "/Game/RPGTest/Blueprints/Enemy/BP_RPGEnemy.BP_RPGEnemy_C")
    // echouait ici alors qu'elle aurait reussi via bpes().resolve_class(). Meme strategie en
    // 3 etapes desormais, avec l'ancien comportement (nom court parmi les classes deja chargees,
    // ex. "Actor", "Pawn") conserve en dernier recours pour ne rien casser.
    if (UClass* Found = FindObject<UClass>(nullptr, *ClassName))
        return Found;
    if (UClass* Found = LoadClass<UObject>(nullptr, *ClassName))
        return Found;
    if (UClass* Found = LoadClass<UObject>(nullptr, *(ClassName + TEXT("_C"))))
        return Found;

    for (TObjectIterator<UClass> It; It; ++It)
        if (It->GetName().Equals(ClassName, ESearchCase::IgnoreCase))
            return *It;
    return nullptr;
}

static UEdGraphNode* PlaceNodeInGraph(UEdGraph* Graph, UEdGraphNode* NewNode, int32 X, int32 Y)
{
    Graph->Modify();
    Graph->AddNode(NewNode, true, false);
    NewNode->CreateNewGuid();
    NewNode->PostPlacedNewNode();
    NewNode->AllocateDefaultPins();
    NewNode->NodePosX = X;
    NewNode->NodePosY = Y;
    return NewNode;
}

// ── READ ─────────────────────────────────────────────────────────────────────

TArray<FString> UBlueprintGraphHelper::ListGraphNodes(UBlueprint* Blueprint, const FString& GraphName)
{
    TArray<FString> Result;
    if (!Blueprint) return Result;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* G : Graphs)
    {
        if (!GraphName.IsEmpty() && G->GetName() != GraphName) continue;
        for (UEdGraphNode* Node : G->Nodes)
        {
            if (!Node) continue;
            int32 ErrType = 0;
            if (FIntProperty* P = FindFProperty<FIntProperty>(Node->GetClass(), TEXT("ErrorType")))
                ErrType = P->GetPropertyValue_InContainer(Node);
            Result.Add(FString::Printf(TEXT("%s|%s|%d|%d|%d"),
                *Node->GetName(), *Node->GetClass()->GetName(),
                (int32)Node->NodePosX, (int32)Node->NodePosY, ErrType));
        }
    }
    return Result;
}

TArray<FString> UBlueprintGraphHelper::ListNodePins(UEdGraphNode* Node)
{
    TArray<FString> Result;
    if (!Node) return Result;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin) continue;
        FString Dir = (Pin->Direction == EGPD_Output) ? TEXT("OUT") : TEXT("IN");
        FString Linked;
        for (UEdGraphPin* L : Pin->LinkedTo)
            if (L && L->GetOwningNode())
                Linked += L->GetOwningNode()->GetName() + TEXT(".") + L->PinName.ToString() + TEXT(";");
        FString DefVal = Pin->DefaultValue;
        if (DefVal.IsEmpty() && Pin->DefaultObject)
            DefVal = GetPathNameSafe(Pin->DefaultObject);
        Result.Add(FString::Printf(TEXT("%s|%s|%s|%s|%s"),
            *Pin->PinName.ToString(), *Dir,
            *Pin->PinType.PinCategory.ToString(),
            *DefVal, Linked.IsEmpty() ? TEXT("") : *Linked));
    }
    return Result;
}

UEdGraphNode* UBlueprintGraphHelper::FindNodeByName(UBlueprint* Blueprint,
                                                     const FString& GraphName,
                                                     const FString& NodeName)
{
    if (!Blueprint) return nullptr;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* G : Graphs)
    {
        if (!GraphName.IsEmpty() && G->GetName() != GraphName) continue;
        for (UEdGraphNode* Node : G->Nodes)
            if (Node && Node->GetName() == NodeName)
                return Node;
    }
    return nullptr;
}

FString UBlueprintGraphHelper::GetLinkedPin(UEdGraphNode* Node, const FString& PinName)
{
    if (!Node) return FString();
    UEdGraphPin* Pin = FindAnyPin(Node, PinName);
    if (!Pin || Pin->LinkedTo.Num() == 0) return FString();
    UEdGraphPin* L = Pin->LinkedTo[0];
    if (!L || !L->GetOwningNode()) return FString();
    return L->GetOwningNode()->GetName() + TEXT(".") + L->PinName.ToString();
}

FString UBlueprintGraphHelper::GetPinDefaultValue(UEdGraphNode* Node, const FString& PinName)
{
    if (!Node) return FString();
    UEdGraphPin* Pin = FindAnyPin(Node, PinName);
    if (!Pin) return FString();
    if (!Pin->DefaultValue.IsEmpty()) return Pin->DefaultValue;
    if (Pin->DefaultObject) return GetPathNameSafe(Pin->DefaultObject);
    return FString();
}

FString UBlueprintGraphHelper::GetNodeFunctionName(UEdGraphNode* Node)
{
    if (!Node) return FString();
    if (FStructProperty* SP = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("FunctionReference")))
    {
        void* PropAddr = SP->ContainerPtrToValuePtr<void>(Node);
        for (TFieldIterator<FNameProperty> It(SP->Struct); It; ++It)
        {
            FString FN = It->GetName();
            if (FN.Contains(TEXT("MemberName")) || FN.Contains(TEXT("FunctionName")))
                return It->GetPropertyValue_InContainer(PropAddr).ToString();
        }
    }
    return Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
}

// ── ADD NODES ─────────────────────────────────────────────────────────────────

UEdGraphNode* UBlueprintGraphHelper::AddFunctionCallNode(UBlueprint* Blueprint,
    const FString& GraphName, const FString& ClassName, const FString& FunctionName,
    int32 X, int32 Y)
{
    if (!Blueprint) return nullptr;
    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph) return nullptr;

    UClass* OwnerClass = FindClassByName(ClassName);
    if (!OwnerClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("AddFunctionCallNode: class '%s' not found"), *ClassName);
        return nullptr;
    }
    UFunction* Func = OwnerClass->FindFunctionByName(*FunctionName);
    if (!Func)
    {
        UE_LOG(LogTemp, Warning, TEXT("AddFunctionCallNode: function '%s' not found on '%s'"),
            *FunctionName, *ClassName);
        return nullptr;
    }

    UK2Node_CallFunction* NewNode = NewObject<UK2Node_CallFunction>(Graph);
    NewNode->SetFromFunction(Func);
    return PlaceNodeInGraph(Graph, NewNode, X, Y);
}

UEdGraphNode* UBlueprintGraphHelper::AddVariableGetNode(UBlueprint* Blueprint,
    const FString& GraphName, const FString& VariableName, int32 X, int32 Y)
{
    if (!Blueprint) return nullptr;
    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph) return nullptr;

    UK2Node_VariableGet* NewNode = NewObject<UK2Node_VariableGet>(Graph);
    NewNode->VariableReference.SetSelfMember(FName(*VariableName));
    return PlaceNodeInGraph(Graph, NewNode, X, Y);
}

UEdGraphNode* UBlueprintGraphHelper::AddVariableSetNode(UBlueprint* Blueprint,
    const FString& GraphName, const FString& VariableName, int32 X, int32 Y)
{
    if (!Blueprint) return nullptr;
    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph) return nullptr;

    UK2Node_VariableSet* NewNode = NewObject<UK2Node_VariableSet>(Graph);
    NewNode->VariableReference.SetSelfMember(FName(*VariableName));
    return PlaceNodeInGraph(Graph, NewNode, X, Y);
}

UEdGraphNode* UBlueprintGraphHelper::AddBranchNode(UBlueprint* Blueprint,
    const FString& GraphName, int32 X, int32 Y)
{
    if (!Blueprint) return nullptr;
    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph) return nullptr;

    UK2Node_IfThenElse* NewNode = NewObject<UK2Node_IfThenElse>(Graph);
    return PlaceNodeInGraph(Graph, NewNode, X, Y);
}

UEdGraphNode* UBlueprintGraphHelper::AddMacroNode(UBlueprint* Blueprint,
    const FString& GraphName, const FString& MacroName, int32 X, int32 Y)
{
    if (!Blueprint) return nullptr;
    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph) return nullptr;

    // Search standard library macros
    UBlueprint* MacroBP = nullptr;
    UEdGraph* MacroGraph = nullptr;

    TArray<UBlueprint*> AllBPs;
    for (TObjectIterator<UBlueprint> It; It; ++It)
    {
        if (It->BlueprintType == BPTYPE_MacroLibrary ||
            It->GetName().Contains(TEXT("StandardMacros")))
        {
            for (UEdGraph* G : It->MacroGraphs)
            {
                if (G->GetName().Equals(MacroName, ESearchCase::IgnoreCase))
                {
                    MacroBP = *It;
                    MacroGraph = G;
                    break;
                }
            }
        }
        if (MacroGraph) break;
    }

    if (!MacroGraph)
    {
        UE_LOG(LogTemp, Warning, TEXT("AddMacroNode: macro '%s' not found"), *MacroName);
        return nullptr;
    }

    UK2Node_MacroInstance* NewNode = NewObject<UK2Node_MacroInstance>(Graph);
    NewNode->SetMacroGraph(MacroGraph);
    return PlaceNodeInGraph(Graph, NewNode, X, Y);
}

// ── EDIT ──────────────────────────────────────────────────────────────────────

bool UBlueprintGraphHelper::ConnectPins(UEdGraphNode* FromNode, const FString& FromPinName,
                                        UEdGraphNode* ToNode,   const FString& ToPinName)
{
    if (!FromNode || !ToNode) return false;
    UEdGraphPin* From = FindAnyPin(FromNode, FromPinName);
    UEdGraphPin* To   = FindAnyPin(ToNode,   ToPinName);
    if (!From || !To) return false;

    UEdGraph* Graph = FromNode->GetGraph();
    if (!Graph || !Graph->GetSchema())
    {
        UE_LOG(LogTemp, Warning, TEXT("ConnectPins: no graph/schema for node '%s'"), *FromNode->GetName());
        return false;
    }

    FromNode->Modify();
    ToNode->Modify();

    // Fix 2026-07-20 : l'ancien code appelait From->MakeLinkTo(To) directement — une API bas
    // niveau qui lie deux pins sans jamais verifier leur compatibilite de type ni declencher la
    // resolution des pins wildcard (ForEachLoop, Cast To X), et qui retournait donc "true" meme
    // sur une connexion invalide. C'est la cause probable des pieges wildcard deja documentes
    // dans CLAUDE.md ("bgh.connect_pins() ne propage pas la resolution de type"), puisque
    // connect_pins() (Python) appelle directement cette fonction. TryCreateConnection() est le
    // meme chemin que l'editeur graphique utilise reellement pour un drag&drop de pin : il
    // rejette proprement une connexion invalide au lieu de la creer silencieusement — meme fix
    // deja applique a BatchWireGraph (RoomGenerator/BlueprintEditingSubsystem.cpp) le 2026-07-15.
    if (!Graph->GetSchema()->TryCreateConnection(From, To))
    {
        UE_LOG(LogTemp, Warning, TEXT("ConnectPins: connection rejected by schema: %s.%s -> %s.%s"),
            *FromNode->GetName(), *FromPinName, *ToNode->GetName(), *ToPinName);
        return false;
    }
    return true;
}

bool UBlueprintGraphHelper::BreakAllPinLinks(UEdGraphNode* Node, const FString& PinName)
{
    if (!Node) return false;
    UEdGraphPin* Pin = FindAnyPin(Node, PinName);
    if (!Pin) return false;
    Node->Modify();
    Pin->BreakAllPinLinks();
    return true;
}

bool UBlueprintGraphHelper::SetPinDefaultValue(UEdGraphNode* Node, const FString& PinName,
                                                const FString& Value)
{
    if (!Node) return false;
    UEdGraphPin* Pin = FindAnyPin(Node, PinName);
    if (!Pin) return false;
    Node->Modify();
    Pin->DefaultValue = Value;
    return true;
}

bool UBlueprintGraphHelper::SetPinDefaultObject(UEdGraphNode* Node, const FString& PinName,
                                                 UObject* ObjectValue)
{
    if (!Node) return false;
    UEdGraphPin* Pin = FindAnyPin(Node, PinName);
    if (!Pin) return false;
    Node->Modify();
    Pin->DefaultObject = ObjectValue;
    return true;
}

bool UBlueprintGraphHelper::DeleteNode(UEdGraphNode* Node)
{
    if (!Node) return false;
    UEdGraph* Graph = Node->GetGraph();
    if (!Graph) return false;
    Graph->Modify();
    Node->Modify();
    // Break all pin connections first
    for (UEdGraphPin* Pin : Node->Pins)
        if (Pin) Pin->BreakAllPinLinks();
    Graph->RemoveNode(Node);
    return true;
}

bool UBlueprintGraphHelper::MoveNode(UEdGraphNode* Node, int32 X, int32 Y)
{
    if (!Node) return false;
    Node->Modify();
    Node->NodePosX = X;
    Node->NodePosY = Y;
    return true;
}

bool UBlueprintGraphHelper::ReconstructNode(UEdGraphNode* Node)
{
    if (!Node) return false;
    Node->Modify();
    Node->ReconstructNode();
    return true;
}

bool UBlueprintGraphHelper::AddUserDefinedPin(UEdGraphNode* Node, const FString& PinName,
                                               const FString& PinCategory)
{
    UK2Node_EditablePinBase* Editable = Cast<UK2Node_EditablePinBase>(Node);
    if (!Editable) return false;

    FEdGraphPinType PinType;
    if (PinCategory.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    }
    else if (PinCategory.Equals(TEXT("int"), ESearchCase::IgnoreCase) ||
             PinCategory.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
    }
    else if (PinCategory.Equals(TEXT("string"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_String;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("AddUserDefinedPin: unsupported PinCategory '%s' (bool|int|string only)"),
            *PinCategory);
        return false;
    }

    Editable->Modify();
    // UE5.8 : CreateUserDefinedPin() retourne un UEdGraphPin* (pas un TSharedPtr<FUserPinInfo>
    // comme dans des versions plus anciennes du moteur — erreur C2440 a la compilation avant ce
    // fix, corrigee en verifiant la signature reelle dans K2Node_EditablePinBase.h de ce moteur).
    UEdGraphPin* NewPin = Editable->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Output, true);
    if (!NewPin)
    {
        UE_LOG(LogTemp, Warning, TEXT("AddUserDefinedPin: CreateUserDefinedPin failed for '%s'"), *PinName);
        return false;
    }

    Editable->ReconstructNode();
    return true;
}

// ── COMPILE / SAVE ────────────────────────────────────────────────────────────

bool UBlueprintGraphHelper::CompileBlueprint(UBlueprint* Blueprint)
{
    if (!Blueprint) return false;
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    // Fix 2026-07-20 : l'ancien code retournait toujours true sans jamais lire le resultat reel
    // de la compilation Kismet — c'est la cause racine du piege documente plusieurs fois dans
    // CLAUDE.md/GAME_MEMORY.md ("compile_blueprint() peut retourner True meme si le vrai
    // compilateur Blueprint a rejete le graphe avec des erreurs reelles, visible uniquement dans
    // le log"). Blueprint->Status reflete le resultat du dernier CompileBlueprint() : BS_Error
    // si le compilateur Kismet a rejete le graphe (ex. pin wildcard non resolu, pin exec de
    // sortie avec plusieurs connexions, etc.).
    const bool bSuccess = (Blueprint->Status != BS_Error);
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("CompileBlueprint: '%s' a des erreurs de compilation (Status=BS_Error) — voir Saved/Logs/*.log pour le detail Kismet."),
            *Blueprint->GetName());
    }
    return bSuccess;
}

bool UBlueprintGraphHelper::SaveBlueprint(UBlueprint* Blueprint)
{
    if (!Blueprint) return false;
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    UPackage* Package = Blueprint->GetOutermost();
    if (Package) Package->MarkPackageDirty();
    return true;
}
