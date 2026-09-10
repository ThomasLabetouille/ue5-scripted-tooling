#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "BlueprintEditingSubsystem.generated.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

UCLASS()
class ROOMGENERATOR_API UBlueprintEditingSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "RoomGenerator|BPEdit")
    UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName);

    UFUNCTION(BlueprintCallable, Category = "RoomGenerator|BPEdit")
    UEdGraphNode* AddFunctionCallNode(UBlueprint* Blueprint, const FString& GraphName,
        const FString& FunctionName, const FString& ClassName,
        int32 NodeX = 0, int32 NodeY = 0);

    UFUNCTION(BlueprintCallable, Category = "RoomGenerator|BPEdit")
    UEdGraphNode* AddCastNode(UBlueprint* Blueprint, const FString& GraphName,
        const FString& TargetClassName, int32 NodeX = 0, int32 NodeY = 0);

    UFUNCTION(BlueprintCallable, Category = "RoomGenerator|BPEdit")
    UEdGraphNode* AddForEachLoopNode(UBlueprint* Blueprint, const FString& GraphName,
        int32 NodeX = 0, int32 NodeY = 0);

    UFUNCTION(BlueprintCallable, Category = "RoomGenerator|BPEdit")
    UEdGraphNode* AddMacroNode(UBlueprint* Blueprint, const FString& GraphName,
        const FString& MacroName, int32 NodeX = 0, int32 NodeY = 0);

    /** Cree tous les noeuds + connexions en UN seul appel Python.
     *  JSON: {"nodes":[{"id":"n0","type":"event","name":"BeginPlay","x":0,"y":0},...],
     *         "connections":[{"from":"n0","fp":"then","to":"n1","tp":"execute"},...]}
     *  Types: event, custom_event, function, cast, branch, macro, var_get, var_set, sequence
     */
    UFUNCTION(BlueprintCallable, Category = "RoomGenerator|BPEdit")
    FString BatchWireGraph(UBlueprint* Blueprint, const FString& GraphName, const FString& GraphJson);

    UFUNCTION(BlueprintCallable, Category = "RoomGenerator|BPEdit")
    UClass* ResolveClass(const FString& ClassPath);

    /**
     * Ajoute une nouvelle variable membre à un Blueprint (BatchWireGraph/BPGraph ne sait
     * câbler QUE des variables déjà existantes — var_get/var_set n'en créent aucune).
     * PinCategory : "int" | "bool" | "float" | "string" | "name".
     * Retourne "OK" ou "Error: ...". Ajouté 2026-07-15 pour le puzzle générique (Axe D
     * de ROADMAP_JEU_COMPLET.md) — capacité générique réutilisable, pas spécifique au puzzle.
     */
    UFUNCTION(BlueprintCallable, Category = "RoomGenerator|BPEdit")
    FString AddMemberVariable(UBlueprint* Blueprint, const FString& VarName,
        const FString& PinCategory, const FString& DefaultValue);

    /**
     * Déplace un dossier Content Browser de SourcePath vers DestPath,
     * en mettant à jour toutes les références (identique au drag&drop UI).
     * Retourne "" si OK, ou un message d'erreur.
     * Ex: MoveFolder("/Game/IA", "/Game/HorrorGame/IA")
     */
    UFUNCTION(BlueprintCallable, Category = "RoomGenerator|AssetManagement")
    FString MoveFolder(const FString& SourcePath, const FString& DestPath);

private:
    UEdGraph* FindGraphInternal(UBlueprint* BP, const FName& GraphFName);
    UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, int32 Direction = -1);
};
