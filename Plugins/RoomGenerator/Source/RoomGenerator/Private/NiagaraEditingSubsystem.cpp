#include "NiagaraEditingSubsystem.h"

// ── Niagara runtime ──────────────────────────────────────────────────────────
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraTypes.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraScript.h"

// ── Niagara editor (graphe de noeuds) ───────────────────────────────────────
// Disponibles car le module est Editor-only et NiagaraEditor est en dépendance.
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeFunctionCall.h"

// ── UE editor ────────────────────────────────────────────────────────────────
#include "Materials/MaterialInterface.h"
#include "Editor.h"

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 — Helpers internes
// ─────────────────────────────────────────────────────────────────────────────

namespace NE_Internal
{

/**
 * GetEmitterDataSafe — contourne le bug LW emitter.
 *
 * Handle.GetInstance().GetEmitterData() retourne null sur les LW emitters car
 * leur GUID de version n'est pas dans VersionData[].
 * Fallback : Emitter->GetLatestEmitterData() accède directement VersionData.Last().
 */
static FVersionedNiagaraEmitterData* GetEmitterDataSafe(const FNiagaraEmitterHandle& Handle)
{
    FVersionedNiagaraEmitter VE = Handle.GetInstance();

    // Chemin standard (Standard emitters)
    FVersionedNiagaraEmitterData* Data = VE.GetEmitterData();
    if (Data) return Data;

    // Fallback LW : accès direct à l'objet émetteur
#if WITH_EDITORONLY_DATA
    if (UNiagaraEmitter* Emitter = VE.Emitter.Get())
        return Emitter->GetLatestEmitterData();
#endif
    return nullptr;
}

/**
 * GetAllGraphs — collecte les graphes Spawn + Update de l'émetteur.
 * OutPhaseNames (optionnel) reçoit "Spawn" ou "Update" pour chaque graphe.
 */
static TArray<UNiagaraGraph*> GetAllGraphs(FVersionedNiagaraEmitterData* Data,
                                            TArray<FString>* OutPhaseNames = nullptr)
{
    TArray<UNiagaraGraph*> Graphs;
#if WITH_EDITORONLY_DATA
    if (!Data) return Graphs;

    auto TryAdd = [&](UNiagaraScript* Script, const FString& PhaseName)
    {
        if (!Script) return;
        // UE5.8 : SourceData n'existe plus — utiliser GetLatestSource() (versioned object pattern)
        UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
        if (Src && Src->NodeGraph)
        {
            Graphs.Add(Src->NodeGraph);
            if (OutPhaseNames) OutPhaseNames->Add(PhaseName);
        }
    };

    TryAdd(Data->SpawnScriptProps.Script,  TEXT("Spawn"));
    TryAdd(Data->UpdateScriptProps.Script, TEXT("Update"));
#endif
    return Graphs;
}

/**
 * FindModuleNode — trouve un UNiagaraNodeFunctionCall par partial match (insensible à la casse).
 * Cherche dans GetFunctionName() ET dans FunctionScriptAssetObjectPath.
 */
static UNiagaraNodeFunctionCall* FindModuleNode(UNiagaraGraph* Graph, const FString& ModuleName)
{
#if WITH_EDITOR
    TArray<UNiagaraNodeFunctionCall*> Nodes;
    Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(Nodes);

    for (UNiagaraNodeFunctionCall* N : Nodes)
    {
        if (N->GetFunctionName().Contains(ModuleName, ESearchCase::IgnoreCase))
            return N;

        FString AssetPath = N->FunctionScriptAssetObjectPath.ToString();
        if (AssetPath.Contains(ModuleName, ESearchCase::IgnoreCase))
            return N;
    }
#endif
    return nullptr;
}

/**
 * FindPin — trouve un pin d'entrée par partial match (insensible à la casse).
 * Les pins Niagara peuvent être préfixés de namespace (ex: "Module.SpawnRate").
 */
static UEdGraphPin* FindPin(UNiagaraNodeFunctionCall* Node, const FString& PropertyName)
{
#if WITH_EDITOR
    if (!Node) return nullptr;

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction != EGPD_Input) continue;

        FString PinName = Pin->PinName.ToString();

        if (PinName.Equals(PropertyName, ESearchCase::IgnoreCase))
            return Pin;
        if (PinName.Contains(PropertyName, ESearchCase::IgnoreCase))
            return Pin;

        // Match sur le nom court (après le dernier '.')
        FString ShortName;
        if (PinName.Split(TEXT("."), nullptr, &ShortName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            if (ShortName.Equals(PropertyName, ESearchCase::IgnoreCase))
                return Pin;
        }
    }
#endif
    return nullptr;
}

/** Modifie DefaultValue d'un pin et marque le graphe dirty (version UNiagaraNodeFunctionCall). */
static bool SetPinDefaultAndMark(UNiagaraNodeFunctionCall* Node, UEdGraphPin* Pin,
                                  const FString& NewValue, UNiagaraGraph* Graph)
{
#if WITH_EDITOR
    if (!Pin || !Node || !Graph) return false;
    Pin->DefaultValue = NewValue;
    Node->MarkNodeRequiresSynchronization(TEXT("PinDefaultChanged"), true);
    Graph->NotifyGraphChanged();
    return true;
#else
    return false;
#endif
}

/**
 * SetPinDefaultOnEdGraphNode — variante générique pour n'importe quel nœud Niagara.
 * Utilisé par la traversée InputMap (les nœuds ParameterMapSet ne sont pas forcément des FunctionCall).
 */
static bool SetPinDefaultOnEdGraphNode(UEdGraphNode* Node, UEdGraphPin* Pin,
                                        const FString& NewValue, UNiagaraGraph* Graph)
{
#if WITH_EDITOR
    if (!Pin || !Node || !Graph) return false;

    // Si le pin est connecté, mettre la valeur sur le pin source
    if (Pin->LinkedTo.Num() > 0)
    {
        UEdGraphPin* SourcePin = Pin->LinkedTo[0];
        SourcePin->DefaultValue = NewValue;
        UNiagaraNode* SourceNode = Cast<UNiagaraNode>(SourcePin->GetOwningNode());
        if (SourceNode)
            SourceNode->MarkNodeRequiresSynchronization(TEXT("PinDefaultChanged"), true);
    }
    else
    {
        Pin->DefaultValue = NewValue;
        UNiagaraNode* NiagNode = Cast<UNiagaraNode>(Node);
        if (NiagNode)
            NiagNode->MarkNodeRequiresSynchronization(TEXT("PinDefaultChanged"), true);
    }

    Graph->NotifyGraphChanged();
    return true;
#else
    return false;
#endif
}

/**
 * TraverseInputMapAndSetPin — BFS avec profondeur depuis un nœud de départ.
 *
 * Deux types de progression :
 *  A) InputMap chain (en arrière) : suit les pins InputMap → profondeur inchangée
 *  B) Sub-nodes via connexions (profondeur limitée à MaxDepth) :
 *     ex. UniformRangedFloat connecté à "AddVelocity.Velocity Speed"
 *
 * Cherche le premier pin INPUT (hors InputMap) dont le nom contient PropertyName
 * et qui N'EST PAS connecté (DefaultValue directement settable).
 */
static bool TraverseInputMapAndSetPin(UEdGraphNode* StartNode, const FString& PropertyName,
                                       const FString& Value, const FString& TypeHint,
                                       const FString& ModuleName, UNiagaraGraph* Graph,
                                       UNiagaraSystem* System, const FString& Phase,
                                       int32 MaxDepth = 4)
{
#if WITH_EDITOR
    struct FItem { UEdGraphNode* Node; int32 Depth; };
    TSet<UEdGraphNode*> Visited;
    TArray<FItem> Queue;
    Queue.Add({ StartNode, 0 });
    int32 Head = 0;

    while (Head < Queue.Num())
    {
        const FItem Item = Queue[Head++];
        UEdGraphNode* Current = Item.Node;
        int32 Depth = Item.Depth;

        if (!Current || Visited.Contains(Current)) continue;
        Visited.Add(Current);

        for (UEdGraphPin* CurPin : Current->Pins)
        {
            if (CurPin->Direction != EGPD_Input) continue;
            FString PinName = CurPin->PinName.ToString();

            // ── A) InputMap pin — progresser dans la chaîne (profondeur inchangée) ──
            if (PinName.Equals(TEXT("InputMap"), ESearchCase::IgnoreCase))
            {
                for (UEdGraphPin* Linked : CurPin->LinkedTo)
                    if (Linked && Linked->GetOwningNode())
                        Queue.Add({ Linked->GetOwningNode(), Depth });
                continue;
            }

            // ── B) Pin de valeur — enqueuer le nœud source pour descendre ──
            // (UniformRangedFloat, RandomRangeFloat, etc.)
            if (CurPin->LinkedTo.Num() > 0 && Depth < MaxDepth)
            {
                for (UEdGraphPin* Linked : CurPin->LinkedTo)
                    if (Linked && Linked->GetOwningNode())
                        Queue.Add({ Linked->GetOwningNode(), Depth + 1 });
            }

            // ── Match PropertyName sur ce pin ? ──
            FString ShortName = PinName;
            int32 DotIdx = INDEX_NONE;
            if (PinName.FindLastChar('.', DotIdx))
                ShortName = PinName.RightChop(DotIdx + 1);

            bool bMatch = ShortName.Equals(PropertyName, ESearchCase::IgnoreCase)
                       || ShortName.Contains(PropertyName, ESearchCase::IgnoreCase)
                       || PinName.Contains(PropertyName, ESearchCase::IgnoreCase);

            if (bMatch)
            {
                // Cas 1 : pin non connecté → setter DefaultValue directement
                // Cas 2 : pin connecté → setter DefaultValue sur le pin SOURCE
                //         (UNiagaraNodeInput stocke sa constante dans son OUTPUT pin DefaultValue)
                UEdGraphPin* TargetPin  = CurPin;
                UEdGraphNode* TargetNode = Current;
                bool bWasConnected = false;

                if (CurPin->LinkedTo.Num() > 0)
                {
                    // Remonter jusqu'au pin source non-connecté (au plus 3 niveaux)
                    UEdGraphPin* Src = CurPin->LinkedTo[0];
                    for (int32 k = 0; k < 3 && Src && Src->LinkedTo.Num() > 0; k++)
                        Src = Src->LinkedTo[0]; // edge rare : chaîne d'outputs
                    TargetPin  = Src;
                    TargetNode = Src ? Src->GetOwningNode() : nullptr;
                    bWasConnected = true;
                }

                if (TargetPin && TargetNode)
                {
                    TargetPin->DefaultValue = Value;
                    UNiagaraNode* NiagNode = Cast<UNiagaraNode>(TargetNode);
                    if (NiagNode) NiagNode->MarkNodeRequiresSynchronization(TEXT("PinDefaultChanged"), true);
                    Graph->NotifyGraphChanged();
                    System->MarkPackageDirty();
                    UE_LOG(LogTemp, Log,
                        TEXT("[NiagaraEditing] SetModule%s via InputMap BFS: %s.%s = %s "
                             "(pin '%s', node %s, depth=%d, connected=%d, phase %s)"),
                        *TypeHint, *ModuleName, *PropertyName, *Value,
                        *PinName, *Current->GetClass()->GetName(), Depth,
                        bWasConnected ? 1 : 0, *Phase);
                    return true;
                }
            }
        }
    }
#endif
    return false;
}

/** Cherche un module dans tous les graphes et applique une valeur de pin. */
static bool ApplyModuleValue(UNiagaraSystem* System, int32 EmitterIndex,
                              const FString& ModuleName, const FString& PropertyName,
                              const FString& Value, const FString& TypeHint)
{
#if WITH_EDITORONLY_DATA
    if (!System) return false;

    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    if (!Handles.IsValidIndex(EmitterIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("[NiagaraEditing] ApplyModuleValue: EmitterIndex %d invalide"), EmitterIndex);
        return false;
    }

    FVersionedNiagaraEmitterData* Data = GetEmitterDataSafe(Handles[EmitterIndex]);
    if (!Data)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NiagaraEditing] ApplyModuleValue: EmitterData null."));
        return false;
    }

    TArray<FString> PhaseNames;
    TArray<UNiagaraGraph*> Graphs = GetAllGraphs(Data, &PhaseNames);
    bool bFoundModule = false;

    for (int32 i = 0; i < Graphs.Num(); i++)
    {
        UNiagaraGraph* Graph = Graphs[i];
        UNiagaraNodeFunctionCall* Node = FindModuleNode(Graph, ModuleName);
        if (!Node) continue;

        bFoundModule = true;
        FString Phase = i < PhaseNames.Num() ? PhaseNames[i] : TEXT("?");

        // ── Tentative 1 : pin direct sur le nœud FunctionCall ────────────────
        UEdGraphPin* Pin = FindPin(Node, PropertyName);
        if (Pin)
        {
            if (SetPinDefaultAndMark(Node, Pin, Value, Graph))
            {
                System->MarkPackageDirty();
                UE_LOG(LogTemp, Log,
                    TEXT("[NiagaraEditing] SetModule%s OK: %s.%s = %s (phase %s)"),
                    *TypeHint, *ModuleName, *PropertyName, *Value, *Phase);
                return true;
            }
        }

        // ── Tentative 2 : traversée InputMap (paramètres "cachés" dans le graphe) ──
        // Les valeurs comme SpawnRate, Speed, ConeAngle sont dans des ParameterMapSet
        // nodes qui alimentent le InputMap du FunctionCall.
        UEdGraphPin* InputMapPin = nullptr;
        for (UEdGraphPin* P : Node->Pins)
        {
            if (P->Direction == EGPD_Input &&
                P->PinName.ToString().Equals(TEXT("InputMap"), ESearchCase::IgnoreCase))
            {
                InputMapPin = P;
                break;
            }
        }

        if (InputMapPin)
        {
            for (UEdGraphPin* Linked : InputMapPin->LinkedTo)
            {
                if (!Linked || !Linked->GetOwningNode()) continue;
                if (TraverseInputMapAndSetPin(Linked->GetOwningNode(), PropertyName,
                                              Value, TypeHint, ModuleName, Graph, System, Phase))
                    return true;
            }
        }

        // Log les pins directs disponibles (debug si les deux tentatives échouent)
        TArray<FString> PinNames;
        for (UEdGraphPin* P : Node->Pins)
            if (P->Direction == EGPD_Input) PinNames.Add(P->PinName.ToString());
        UE_LOG(LogTemp, Warning,
            TEXT("[NiagaraEditing] Pin '%s' non trouvé sur '%s' (direct ni InputMap). "
                 "Pins directs: [%s]. Utiliser get_module_parameters() pour voir tous les paramètres."),
            *PropertyName, *ModuleName, *FString::Join(PinNames, TEXT(", ")));
    }

    if (!bFoundModule)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NiagaraEditing] Module '%s' non trouvé dans Spawn+Update. "
                 "Utiliser get_module_names() pour voir les modules disponibles."), *ModuleName);
    }
#endif
    return false;
}

} // namespace NE_Internal


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2 — Normalisation
// ─────────────────────────────────────────────────────────────────────────────

FString UNiagaraEditingSubsystem::NormalizeParamName(const FString& Name) const
{
    return Name.StartsWith(TEXT("User.")) ? Name : TEXT("User.") + Name;
}


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 3 — User Parameters
// ─────────────────────────────────────────────────────────────────────────────

bool UNiagaraEditingSubsystem::AddUserParamFloat(UNiagaraSystem* System,
                                                  const FString& ParamName,
                                                  float DefaultValue)
{
    if (!System) return false;
    const FString FullName = NormalizeParamName(ParamName);
    FNiagaraVariable Var(FNiagaraTypeDefinition::GetFloatDef(), FName(*FullName));
    FNiagaraParameterStore& Store = System->GetExposedParameters();
    Store.AddParameter(Var, false);
    Store.SetParameterValue<float>(DefaultValue, Var, true);
    System->MarkPackageDirty();
    UE_LOG(LogTemp, Log, TEXT("[NiagaraEditing] AddUserParamFloat: %s = %.2f"), *FullName, DefaultValue);
    return true;
}

bool UNiagaraEditingSubsystem::AddUserParamLinearColor(UNiagaraSystem* System,
                                                        const FString& ParamName,
                                                        FLinearColor DefaultValue)
{
    if (!System) return false;
    const FString FullName = NormalizeParamName(ParamName);
    FNiagaraVariable Var(FNiagaraTypeDefinition::GetColorDef(), FName(*FullName));
    FNiagaraParameterStore& Store = System->GetExposedParameters();
    Store.AddParameter(Var, false);
    Store.SetParameterValue<FLinearColor>(DefaultValue, Var, true);
    System->MarkPackageDirty();
    UE_LOG(LogTemp, Log, TEXT("[NiagaraEditing] AddUserParamLinearColor: %s"), *FullName);
    return true;
}

bool UNiagaraEditingSubsystem::AddUserParamVector(UNiagaraSystem* System,
                                                   const FString& ParamName,
                                                   FVector DefaultValue)
{
    if (!System) return false;
    const FString FullName = NormalizeParamName(ParamName);
    FNiagaraVariable Var(FNiagaraTypeDefinition::GetVec3Def(), FName(*FullName));
    FNiagaraParameterStore& Store = System->GetExposedParameters();
    Store.AddParameter(Var, false);
    Store.SetParameterValue<FVector>(DefaultValue, Var, true);
    System->MarkPackageDirty();
    UE_LOG(LogTemp, Log, TEXT("[NiagaraEditing] AddUserParamVector: %s"), *FullName);
    return true;
}

bool UNiagaraEditingSubsystem::AddUserParamBool(UNiagaraSystem* System,
                                                 const FString& ParamName,
                                                 bool DefaultValue)
{
    if (!System) return false;
    const FString FullName = NormalizeParamName(ParamName);
    FNiagaraVariable Var(FNiagaraTypeDefinition::GetBoolDef(), FName(*FullName));
    FNiagaraParameterStore& Store = System->GetExposedParameters();
    Store.AddParameter(Var, false);
    FNiagaraBool NiagaraBool;
    NiagaraBool.SetValue(DefaultValue);
    Store.SetParameterValue<FNiagaraBool>(NiagaraBool, Var, true);
    System->MarkPackageDirty();
    UE_LOG(LogTemp, Log, TEXT("[NiagaraEditing] AddUserParamBool: %s = %d"), *FullName, DefaultValue ? 1 : 0);
    return true;
}

TArray<FString> UNiagaraEditingSubsystem::GetUserParams(UNiagaraSystem* System)
{
    TArray<FString> Result;
    if (!System) return Result;
    const FNiagaraParameterStore& Store = System->GetExposedParameters();
    TArray<FNiagaraVariable> Params;
    Store.GetParameters(Params);
    for (const FNiagaraVariable& Var : Params)
    {
        const FString VarName = Var.GetName().ToString();
        if (VarName.StartsWith(TEXT("User.")))
            Result.Add(FString::Printf(TEXT("%s|%s"), *VarName, *Var.GetType().GetName()));
    }
    return Result;
}


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 4 — Emitter info
// ─────────────────────────────────────────────────────────────────────────────

TArray<FString> UNiagaraEditingSubsystem::GetEmitterNames(UNiagaraSystem* System)
{
    TArray<FString> Result;
    if (!System) return Result;
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
        Result.Add(Handle.GetName().ToString());
    return Result;
}

bool UNiagaraEditingSubsystem::IsLightweightEmitter(UNiagaraSystem* System, int32 EmitterIndex)
{
    if (!System) return false;
    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    if (!Handles.IsValidIndex(EmitterIndex)) return false;

    // LW emitters: GetEmitterData() via version GUID retourne null
    FVersionedNiagaraEmitter VE = Handles[EmitterIndex].GetInstance();
    bool bIsLW = (VE.GetEmitterData() == nullptr);
    UE_LOG(LogTemp, Log, TEXT("[NiagaraEditing] IsLightweightEmitter[%d]: %s"),
           EmitterIndex, bIsLW ? TEXT("YES (LW)") : TEXT("NO (Standard)"));
    return bIsLW;
}


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 5 — Module read
// ─────────────────────────────────────────────────────────────────────────────

TArray<FString> UNiagaraEditingSubsystem::GetModuleNames(UNiagaraSystem* System, int32 EmitterIndex)
{
    TArray<FString> Result;
#if WITH_EDITORONLY_DATA
    if (!System) return Result;

    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    if (!Handles.IsValidIndex(EmitterIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("[NiagaraEditing] GetModuleNames: EmitterIndex %d invalide"), EmitterIndex);
        return Result;
    }

    FVersionedNiagaraEmitterData* Data = NE_Internal::GetEmitterDataSafe(Handles[EmitterIndex]);
    if (!Data)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NiagaraEditing] GetModuleNames: EmitterData null"));
        return Result;
    }

    TArray<FString> PhaseNames;
    TArray<UNiagaraGraph*> Graphs = NE_Internal::GetAllGraphs(Data, &PhaseNames);

    for (int32 i = 0; i < Graphs.Num(); i++)
    {
        FString Phase = i < PhaseNames.Num() ? PhaseNames[i] : TEXT("?");
        TArray<UNiagaraNodeFunctionCall*> Nodes;
        Graphs[i]->GetNodesOfClass<UNiagaraNodeFunctionCall>(Nodes);

        for (UNiagaraNodeFunctionCall* N : Nodes)
        {
            FString FuncName = N->GetFunctionName();
            if (!FuncName.IsEmpty())
                Result.Add(FString::Printf(TEXT("%s|%s"), *FuncName, *Phase));
        }
    }
#endif
    return Result;
}

float UNiagaraEditingSubsystem::GetModuleFloat(UNiagaraSystem* System, int32 EmitterIndex,
                                                const FString& ModuleName,
                                                const FString& PropertyName)
{
#if WITH_EDITORONLY_DATA
    if (!System) return -1.f;
    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    if (!Handles.IsValidIndex(EmitterIndex)) return -1.f;

    FVersionedNiagaraEmitterData* Data = NE_Internal::GetEmitterDataSafe(Handles[EmitterIndex]);
    if (!Data) return -1.f;

    for (UNiagaraGraph* Graph : NE_Internal::GetAllGraphs(Data))
    {
        UNiagaraNodeFunctionCall* Node = NE_Internal::FindModuleNode(Graph, ModuleName);
        if (!Node) continue;

        UEdGraphPin* Pin = NE_Internal::FindPin(Node, PropertyName);
        if (Pin)
        {
            float Val = FCString::Atof(*Pin->DefaultValue);
            UE_LOG(LogTemp, Log, TEXT("[NiagaraEditing] GetModuleFloat: %s.%s = %.3f"),
                   *ModuleName, *PropertyName, Val);
            return Val;
        }
    }
#endif
    return -1.f;
}


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 6 — Module write
// ─────────────────────────────────────────────────────────────────────────────

bool UNiagaraEditingSubsystem::SetModuleFloat(UNiagaraSystem* System, int32 EmitterIndex,
                                               const FString& ModuleName,
                                               const FString& PropertyName,
                                               float Value)
{
    return NE_Internal::ApplyModuleValue(
        System, EmitterIndex, ModuleName, PropertyName,
        FString::SanitizeFloat(Value), TEXT("Float"));
}

bool UNiagaraEditingSubsystem::SetModuleVector(UNiagaraSystem* System, int32 EmitterIndex,
                                                const FString& ModuleName,
                                                const FString& PropertyName,
                                                FVector Value)
{
    // Format struct UE pour les vecteurs dans les pins Niagara
    FString VecStr = FString::Printf(TEXT("X=%f Y=%f Z=%f"), Value.X, Value.Y, Value.Z);
    return NE_Internal::ApplyModuleValue(
        System, EmitterIndex, ModuleName, PropertyName, VecStr, TEXT("Vector"));
}

bool UNiagaraEditingSubsystem::SetModuleColor(UNiagaraSystem* System, int32 EmitterIndex,
                                               const FString& ModuleName,
                                               const FString& PropertyName,
                                               FLinearColor Value)
{
    FString ColStr = FString::Printf(TEXT("(R=%f,G=%f,B=%f,A=%f)"),
                                     Value.R, Value.G, Value.B, Value.A);
    return NE_Internal::ApplyModuleValue(
        System, EmitterIndex, ModuleName, PropertyName, ColStr, TEXT("Color"));
}

bool UNiagaraEditingSubsystem::SetModuleBool(UNiagaraSystem* System, int32 EmitterIndex,
                                              const FString& ModuleName,
                                              const FString& PropertyName,
                                              bool Value)
{
    return NE_Internal::ApplyModuleValue(
        System, EmitterIndex, ModuleName, PropertyName,
        Value ? TEXT("true") : TEXT("false"), TEXT("Bool"));
}


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 7 — Material (corrigé pour LW + Standard via GetEmitterDataSafe)
// ─────────────────────────────────────────────────────────────────────────────

static UNiagaraSpriteRendererProperties* GetSpriteRenderer_Internal(
    UNiagaraSystem* System, int32 EmitterIndex)
{
#if WITH_EDITORONLY_DATA
    if (!System) return nullptr;
    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    if (!Handles.IsValidIndex(EmitterIndex)) return nullptr;

    FVersionedNiagaraEmitterData* Data = NE_Internal::GetEmitterDataSafe(Handles[EmitterIndex]);
    if (!Data) return nullptr;

    for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
    {
        if (UNiagaraSpriteRendererProperties* Sprite =
                Cast<UNiagaraSpriteRendererProperties>(Renderer))
            return Sprite;
    }
#endif
    return nullptr;
}

FString UNiagaraEditingSubsystem::GetSpriteRendererMaterial(UNiagaraSystem* System,
                                                              int32 EmitterIndex)
{
    UNiagaraSpriteRendererProperties* Sprite = GetSpriteRenderer_Internal(System, EmitterIndex);
    if (!Sprite || !Sprite->Material)
        return TEXT("");
    return Sprite->Material->GetPathName();
}

bool UNiagaraEditingSubsystem::SetSpriteRendererMaterial(UNiagaraSystem* System,
                                                          int32 EmitterIndex,
                                                          const FString& MaterialPath)
{
    if (!System) return false;

    UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (!Mat)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NiagaraEditing] Matériau introuvable: %s"), *MaterialPath);
        return false;
    }

    UNiagaraSpriteRendererProperties* Sprite = GetSpriteRenderer_Internal(System, EmitterIndex);
    if (!Sprite)
    {
        bool bIsLW = IsLightweightEmitter(System, EmitterIndex);
        UE_LOG(LogTemp, Warning,
            TEXT("[NiagaraEditing] SetSpriteRendererMaterial: SpriteRenderer null "
                 "(IsLW=%d). Convertir l'émetteur en Standard dans l'éditeur Niagara "
                 "si ce message persiste."), bIsLW ? 1 : 0);
        return false;
    }

    Sprite->Material = Mat;
    Sprite->MarkPackageDirty();
    System->MarkPackageDirty();

    UE_LOG(LogTemp, Log, TEXT("[NiagaraEditing] SetSpriteRendererMaterial OK: %s → %s"),
           *System->GetName(), *MaterialPath);
    return true;
}


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 8 — Enable/disable module + parameter inspection
// ─────────────────────────────────────────────────────────────────────────────

bool UNiagaraEditingSubsystem::SetModuleEnabled(UNiagaraSystem* System, int32 EmitterIndex,
                                                  const FString& ModuleName, bool bEnabled)
{
#if WITH_EDITORONLY_DATA
    if (!System) return false;
    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    if (!Handles.IsValidIndex(EmitterIndex)) return false;

    FVersionedNiagaraEmitterData* Data = NE_Internal::GetEmitterDataSafe(Handles[EmitterIndex]);
    if (!Data) return false;

    TArray<FString> PhaseNames;
    TArray<UNiagaraGraph*> Graphs = NE_Internal::GetAllGraphs(Data, &PhaseNames);
    bool bFound = false;

    for (int32 i = 0; i < Graphs.Num(); i++)
    {
        UNiagaraNodeFunctionCall* Node = NE_Internal::FindModuleNode(Graphs[i], ModuleName);
        if (!Node) continue;

        Node->Modify();
        // ENodeEnabledState::Enabled = 0, Disabled = 1
        Node->SetEnabledState(
            bEnabled ? ENodeEnabledState::Enabled : ENodeEnabledState::Disabled, false);
        Graphs[i]->NotifyGraphChanged();
        System->MarkPackageDirty();

        UE_LOG(LogTemp, Log, TEXT("[NiagaraEditing] SetModuleEnabled: %s → %s (phase %s)"),
            *ModuleName, bEnabled ? TEXT("Enabled") : TEXT("Disabled"),
            i < PhaseNames.Num() ? *PhaseNames[i] : TEXT("?"));
        bFound = true;
    }

    if (!bFound)
        UE_LOG(LogTemp, Warning, TEXT("[NiagaraEditing] SetModuleEnabled: Module '%s' non trouvé"), *ModuleName);
    return bFound;
#else
    return false;
#endif
}

TArray<FString> UNiagaraEditingSubsystem::GetModuleParameters(UNiagaraSystem* System,
                                                                int32 EmitterIndex,
                                                                const FString& ModuleName)
{
    TArray<FString> Result;
#if WITH_EDITORONLY_DATA
    if (!System) return Result;
    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    if (!Handles.IsValidIndex(EmitterIndex)) return Result;

    FVersionedNiagaraEmitterData* Data = NE_Internal::GetEmitterDataSafe(Handles[EmitterIndex]);
    if (!Data) return Result;

    TArray<FString> PhaseNames;
    TArray<UNiagaraGraph*> Graphs = NE_Internal::GetAllGraphs(Data, &PhaseNames);

    for (int32 i = 0; i < Graphs.Num(); i++)
    {
        UNiagaraGraph* Graph = Graphs[i];
        UNiagaraNodeFunctionCall* Node = NE_Internal::FindModuleNode(Graph, ModuleName);
        if (!Node) continue;

        FString Phase = i < PhaseNames.Num() ? PhaseNames[i] : TEXT("?");

        // 1. Pins directs sur le FunctionCall
        for (UEdGraphPin* P : Node->Pins)
        {
            if (P->Direction != EGPD_Input) continue;
            FString Conn = P->LinkedTo.Num() > 0 ? TEXT("connected") : TEXT("default");
            Result.Add(FString::Printf(TEXT("[Direct|%s] %s = '%s' (%s)"),
                *Phase, *P->PinName.ToString(), *P->DefaultValue, *Conn));
        }

        // 2. Traversée InputMap backwards
        UEdGraphPin* InputMapPin = nullptr;
        for (UEdGraphPin* P : Node->Pins)
        {
            if (P->Direction == EGPD_Input &&
                P->PinName.ToString().Equals(TEXT("InputMap"), ESearchCase::IgnoreCase))
            {
                InputMapPin = P;
                break;
            }
        }

        if (InputMapPin)
        {
            TSet<UEdGraphNode*> Visited;
            TQueue<UEdGraphNode*> BFSQueue;
            for (UEdGraphPin* Linked : InputMapPin->LinkedTo)
                if (Linked && Linked->GetOwningNode())
                    BFSQueue.Enqueue(Linked->GetOwningNode());

            while (!BFSQueue.IsEmpty())
            {
                UEdGraphNode* Current;
                BFSQueue.Dequeue(Current);
                if (!Current || Visited.Contains(Current)) continue;
                Visited.Add(Current);

                for (UEdGraphPin* CurPin : Current->Pins)
                {
                    if (CurPin->Direction != EGPD_Input) continue;
                    FString PinName = CurPin->PinName.ToString();
                    if (PinName.Equals(TEXT("InputMap"), ESearchCase::IgnoreCase)) continue;

                    FString ValueStr = CurPin->DefaultValue;
                    if (CurPin->LinkedTo.Num() > 0)
                        ValueStr = FString::Printf(TEXT("→ %s.%s"),
                            *CurPin->LinkedTo[0]->GetOwningNode()->GetClass()->GetName(),
                            *CurPin->LinkedTo[0]->PinName.ToString());

                    Result.Add(FString::Printf(TEXT("[InputMap|%s] %s = '%s' (node: %s)"),
                        *Phase, *PinName, *ValueStr, *Current->GetClass()->GetName()));
                }

                // Continuer la traversée via InputMap
                for (UEdGraphPin* CurPin : Current->Pins)
                {
                    if (CurPin->Direction != EGPD_Input) continue;
                    if (!CurPin->PinName.ToString().Contains(TEXT("InputMap"), ESearchCase::IgnoreCase)) continue;
                    for (UEdGraphPin* Linked : CurPin->LinkedTo)
                        if (Linked && Linked->GetOwningNode())
                            BFSQueue.Enqueue(Linked->GetOwningNode());
                }
            }
        }
    }

    for (const FString& S : Result)
        UE_LOG(LogTemp, Log, TEXT("[GetModuleParameters] %s"), *S);
#endif
    return Result;
}


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 9 — Compilation
// ─────────────────────────────────────────────────────────────────────────────

bool UNiagaraEditingSubsystem::CompileSystem(UNiagaraSystem* System)
{
    if (!System) return false;

    System->RequestCompile(false);

    // Fix 2026-07-20 : RequestCompile() est asynchrone (file d'attente DDC/compile-task) —
    // l'ancien code retournait true immediatement sans jamais attendre la fin reelle de la
    // compilation ni verifier son resultat, meme pattern que CompileBlueprint()
    // (BlueprintGraphHelper.cpp) avant son fix du meme jour. WaitForCompilationComplete()
    // bloque jusqu'a la fin reelle de la compilation ; on verifie ensuite qu'il ne reste plus
    // de requete en attente et que le script de spawn systeme n'est pas en erreur (un systeme
    // dont le spawn script echoue est de toute facon inutilisable en jeu).
    System->WaitForCompilationComplete(false, false);

    bool bSuccess = !System->HasOutstandingCompilationRequests(false);
    if (UNiagaraScript* SpawnScript = System->GetSystemSpawnScript())
    {
        if (SpawnScript->GetVMExecutableData().LastCompileStatus == ENiagaraScriptCompileStatus::NCS_Error)
            bSuccess = false;
    }

    UE_LOG(LogTemp, Log, TEXT("[NiagaraEditing] CompileSystem: %s -> %s"),
        *System->GetName(), bSuccess ? TEXT("OK") : TEXT("ECHEC"));
    return bSuccess;
}


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 10 — Diagnostic
// ─────────────────────────────────────────────────────────────────────────────

TArray<FString> UNiagaraEditingSubsystem::DiagnoseEmitter(UNiagaraSystem* System, int32 EmitterIndex)
{
    TArray<FString> Result;
    if (!System) { Result.Add(TEXT("ERROR: System is null")); return Result; }

    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    Result.Add(FString::Printf(TEXT("EmitterHandles.Num = %d"), Handles.Num()));

    if (!Handles.IsValidIndex(EmitterIndex))
    {
        Result.Add(FString::Printf(TEXT("ERROR: EmitterIndex %d invalide"), EmitterIndex));
        return Result;
    }

    const FNiagaraEmitterHandle& Handle = Handles[EmitterIndex];
    Result.Add(FString::Printf(TEXT("EmitterName = %s"), *Handle.GetName().ToString()));

    FVersionedNiagaraEmitter VE = Handle.GetInstance();

    // Chemin standard (version GUID)
    FVersionedNiagaraEmitterData* DataStd = VE.GetEmitterData();
    Result.Add(FString::Printf(TEXT("GetEmitterData() = %s"), DataStd ? TEXT("non-null") : TEXT("NULL")));

#if WITH_EDITORONLY_DATA
    // Chemin fallback (GetLatestEmitterData)
    UNiagaraEmitter* EmitterObj = VE.Emitter.Get();
    Result.Add(FString::Printf(TEXT("Emitter object = %s"), EmitterObj ? TEXT("valid") : TEXT("NULL")));

    FVersionedNiagaraEmitterData* DataLatest = EmitterObj ? EmitterObj->GetLatestEmitterData() : nullptr;
    Result.Add(FString::Printf(TEXT("GetLatestEmitterData() = %s"), DataLatest ? TEXT("non-null") : TEXT("NULL")));

    if (EmitterObj)
    {
        Result.Add(FString::Printf(TEXT("Emitter class = %s"), *EmitterObj->GetClass()->GetName()));
    }

    // Tester les deux sources de données
    for (int32 Pass = 0; Pass < 2; Pass++)
    {
        FVersionedNiagaraEmitterData* Data = (Pass == 0) ? DataStd : DataLatest;
        FString PassName = (Pass == 0) ? TEXT("Standard") : TEXT("Fallback");
        if (!Data) continue;

        // Renderers
        int32 NumRenderers = Data->GetRenderers().Num();
        Result.Add(FString::Printf(TEXT("[%s] GetRenderers().Num = %d"), *PassName, NumRenderers));

        for (int32 i = 0; i < NumRenderers; i++)
        {
            UNiagaraRendererProperties* R = Data->GetRenderers()[i];
            Result.Add(FString::Printf(TEXT("  Renderer[%d] = %s"), i,
                R ? *R->GetClass()->GetName() : TEXT("null")));
        }

        // Scripts
        UNiagaraScript* SpawnScript  = Data->SpawnScriptProps.Script;
        UNiagaraScript* UpdateScript = Data->UpdateScriptProps.Script;
        Result.Add(FString::Printf(TEXT("[%s] SpawnScript  = %s"), *PassName, SpawnScript  ? TEXT("valid") : TEXT("null")));
        Result.Add(FString::Printf(TEXT("[%s] UpdateScript = %s"), *PassName, UpdateScript ? TEXT("valid") : TEXT("null")));

        if (SpawnScript)
        {
            UNiagaraScriptSourceBase* SrcBase = SpawnScript->GetLatestSource();
            Result.Add(FString::Printf(TEXT("[%s] SpawnScript.GetLatestSource() = %s"),
                *PassName, SrcBase ? TEXT("non-null") : TEXT("NULL")));
            if (SrcBase)
            {
                Result.Add(FString::Printf(TEXT("[%s] Source class = %s"), *PassName,
                    *SrcBase->GetClass()->GetName()));
                UNiagaraScriptSource* NiaSrc = Cast<UNiagaraScriptSource>(SrcBase);
                if (NiaSrc)
                    Result.Add(FString::Printf(TEXT("[%s] NodeGraph = %s"), *PassName,
                        NiaSrc->NodeGraph ? TEXT("non-null") : TEXT("NULL")));
            }
        }
    }

    // Info SimTarget (différencie CPU/GPU/LW)
    if (DataLatest)
    {
        Result.Add(FString::Printf(TEXT("SimTarget = %d (0=CPU, 1=GPU)"),
            (int32)DataLatest->SimTarget));
    }
#endif

    for (const FString& Line : Result)
        UE_LOG(LogTemp, Log, TEXT("[DiagnoseEmitter] %s"), *Line);

    return Result;
}
