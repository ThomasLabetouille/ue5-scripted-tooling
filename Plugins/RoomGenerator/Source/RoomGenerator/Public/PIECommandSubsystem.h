#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "PIECommandSubsystem.generated.h"

/**
 * PIECommandSubsystem — déclenche des events/fonctions Blueprint depuis Python pendant le PIE.
 *
 * Accessible depuis Python :
 *   pie = unreal.get_editor_subsystem(unreal.PIECommandSubsystem)
 *
 *   # Exécuter une console command dans le monde PIE
 *   pie.execute_in_pie("stat fps")
 *
 *   # Appeler une UFUNCTION sur un acteur taggé (un seul parametre d'entree supporte)
 *   pie.call_actor_function("Player", "DoShockwave")                          # 0 parametre
 *   pie.call_actor_function("Enemy",  "ReceiveDamage", 50.0)                  # 1 parametre float
 *   pie.call_actor_function("Enemy",  "SetGuardActive", 0.0, "true")          # 1 parametre bool (fix 2026-07-21)
 *   pie.call_actor_function("Enemy",  "SetComboIndex",  0.0, "2")             # 1 parametre int32 (fix 2026-07-21)
 *   pie.call_actor_function("NPC",    "PlayBarkLine",   0.0, "Hello there")   # 1 parametre FString (fix 2026-07-21)
 *
 *   # Lire la liste des UFUNCTIONs disponibles sur un acteur
 *   print(pie.list_actor_functions("Player"))
 */
UCLASS()
class ROOMGENERATOR_API UPIECommandSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:

    /**
     * Exécute une console command dans le monde PIE actif.
     * Ex: "stat fps", "r.SetRes 1920x1080", "Teleport 0 0 500"
     * Retourne true si la commande a été envoyée (le PIE était actif).
     */
    UFUNCTION(BlueprintCallable, Category = "PIECommand")
    bool ExecuteInPIE(const FString& ConsoleCommand);

    /**
     * Appelle une UFUNCTION par nom sur le premier acteur portant le tag donné.
     * Seule une fonction à exactement 0 ou 1 paramètre d'entrée est supportée (refus sinon,
     * voir CallFunction()). Le paramètre unique peut être float, bool, int32 ou FString :
     *  - ArgFloat != 0 (ou ArgString vide) → utilisé pour un paramètre float/bool/int32.
     *  - ArgString non vide → prioritaire, utilisé tel quel (FString) ou parsé (bool/int32/float).
     *
     * @param ActorTag      Tag UE5 de l'acteur cible (ex: "Player", "Enemy")
     * @param FunctionName  Nom de la UFUNCTION C++ (ex: "DoShockwave", "ReceiveDamage")
     * @param ArgFloat      Argument float optionnel (0 = ignoré si ArgString est fourni)
     * @param ArgString     Argument texte optionnel — fix 2026-07-21, voir exemples ci-dessus
     * @return Message de résultat ou erreur
     */
    UFUNCTION(BlueprintCallable, Category = "PIECommand")
    FString CallActorFunction(const FString& ActorTag, const FString& FunctionName, float ArgFloat = 0.f, const FString& ArgString = FString());

    /**
     * Appelle une UFUNCTION sur TOUS les acteurs portant le tag donné.
     * Utile pour déclencher un event sur tous les ennemis en même temps.
     * Retourne le nombre d'acteurs atteints. Voir CallActorFunction() pour ArgFloat/ArgString.
     */
    UFUNCTION(BlueprintCallable, Category = "PIECommand")
    int32 CallAllActorsFunction(const FString& ActorTag, const FString& FunctionName, float ArgFloat = 0.f, const FString& ArgString = FString());

    /**
     * Liste toutes les UFUNCTIONs BlueprintCallable disponibles sur un acteur taggé.
     * Utile pour découvrir ce qu'on peut appeler.
     * Retourne un JSON array de strings.
     */
    UFUNCTION(BlueprintCallable, Category = "PIECommand")
    FString ListActorFunctions(const FString& ActorTag) const;

    /**
     * Téléporte le joueur à la position donnée dans le PIE.
     */
    UFUNCTION(BlueprintCallable, Category = "PIECommand")
    bool TeleportPlayer(float X, float Y, float Z);

    /**
     * Donne des HP au joueur (appelle ReceiveDamage avec valeur négative si nécessaire).
     * En pratique : set CurrentHP via réflexion directement.
     */
    UFUNCTION(BlueprintCallable, Category = "PIECommand")
    bool SetPlayerHP(float HP);

private:
    UWorld* GetPIEWorld() const;
    AActor* FindActorByTag(UWorld* World, const FString& Tag) const;
    TArray<AActor*> FindAllActorsByTag(UWorld* World, const FString& Tag) const;
    bool CallFunction(AActor* Actor, const FString& FunctionName, float ArgFloat, const FString& ArgString = FString());
};
