#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintGraphHelper.generated.h"

UCLASS()
class BLUEPRINTPYTHONUTILS_API UBlueprintGraphHelper : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:

    // ── READ ──────────────────────────────────────────────────────────────────

    /** List every node in a graph: "NodeName|ClassName|X|Y|ERR" per entry */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static TArray<FString> ListGraphNodes(UBlueprint* Blueprint, const FString& GraphName);

    /** List pins of a node: "PinName|Direction|Category|DefaultValue|LinkedTo" */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static TArray<FString> ListNodePins(UEdGraphNode* Node);

    /** Find a node in a Blueprint graph by its FName */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static UEdGraphNode* FindNodeByName(UBlueprint* Blueprint,
                                        const FString& GraphName,
                                        const FString& NodeName);

    /** Who is a pin connected to? Returns "NodeName.PinName" or empty */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static FString GetLinkedPin(UEdGraphNode* Node, const FString& PinName);

    /** Get a pin's current default value */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static FString GetPinDefaultValue(UEdGraphNode* Node, const FString& PinName);

    /** Get event/function name identity of a node */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static FString GetNodeFunctionName(UEdGraphNode* Node);

    // ── ADD NODES ─────────────────────────────────────────────────────────────

    /** Add a function-call node. className e.g. "AIBlueprintHelperLibrary",
     *  functionName e.g. "SimpleMoveToActor". Returns new node or null. */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static UEdGraphNode* AddFunctionCallNode(UBlueprint* Blueprint, const FString& GraphName,
                                              const FString& ClassName, const FString& FunctionName,
                                              int32 X, int32 Y);

    /** Add a variable GET node (self-member variable). */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static UEdGraphNode* AddVariableGetNode(UBlueprint* Blueprint, const FString& GraphName,
                                             const FString& VariableName, int32 X, int32 Y);

    /** Add a variable SET node (self-member variable). */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static UEdGraphNode* AddVariableSetNode(UBlueprint* Blueprint, const FString& GraphName,
                                             const FString& VariableName, int32 X, int32 Y);

    /** Add a Branch (if-then-else) node. */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static UEdGraphNode* AddBranchNode(UBlueprint* Blueprint, const FString& GraphName,
                                        int32 X, int32 Y);

    /** Add a pure macro node by name (e.g. "InRange"). Returns new node or null. */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static UEdGraphNode* AddMacroNode(UBlueprint* Blueprint, const FString& GraphName,
                                       const FString& MacroName, int32 X, int32 Y);

    // ── EDIT ──────────────────────────────────────────────────────────────────

    /** Connect two pins. FromNode.FromPinName --> ToNode.ToPinName */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool ConnectPins(UEdGraphNode* FromNode, const FString& FromPinName,
                            UEdGraphNode* ToNode,   const FString& ToPinName);

    /** Break every link on a named pin */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool BreakAllPinLinks(UEdGraphNode* Node, const FString& PinName);

    /** Set a pin's literal default value */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool SetPinDefaultValue(UEdGraphNode* Node, const FString& PinName,
                                   const FString& Value);

    /** Set a pin's default object reference */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool SetPinDefaultObject(UEdGraphNode* Node, const FString& PinName,
                                    UObject* ObjectValue);

    /** Delete a node from its graph */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool DeleteNode(UEdGraphNode* Node);

    /** Move a node to a new position */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool MoveNode(UEdGraphNode* Node, int32 X, int32 Y);

    /** Reconstruct a node (fixes stale/ERR function references) */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool ReconstructNode(UEdGraphNode* Node);

    /**
     * Ajoute un pin de parametre utilisateur (UK2Node_EditablePinBase) a un Custom Event ou une
     * Function Entry, puis reconstruit le noeud pour materialiser le pin. Direction toujours
     * Output (semantique standard UE : le noeud d'entree "produit" ses parametres en pins de
     * sortie pour le reste du graphe). PinCategory : "bool" | "int" | "string".
     * Ajoute 2026-07-21 (audit RoomGenerator, validation dynamique du fix CallFunction
     * bool/int32/FString) : jusqu'ici aucune fonction exposee ne permettait de creer un Custom
     * Event parametre depuis Python, seulement des events sans parametre (BatchWireGraph type
     * "custom_event"). Retourne false si Node n'est pas un UK2Node_EditablePinBase ou si
     * PinCategory est inconnue.
     */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool AddUserDefinedPin(UEdGraphNode* Node, const FString& PinName, const FString& PinCategory);

    // ── COMPILE / SAVE ────────────────────────────────────────────────────────

    /** Mark Blueprint modified and recompile */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool CompileBlueprint(UBlueprint* Blueprint);

    /** Mark Blueprint dirty (save on next explicit save) */
    UFUNCTION(BlueprintCallable, Category="Graph Utils")
    static bool SaveBlueprint(UBlueprint* Blueprint);
};
