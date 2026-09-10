#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RuntimeDebugSubsystem.generated.h"

/**
 * RuntimeDebugSubsystem — lit l'état runtime du jeu pendant le PIE sans screenshot.
 *
 * Accessible depuis Python pendant le PIE :
 *   world = unreal.get_game_world()
 *   gi    = world.get_game_instance()
 *   sub   = unreal.get_engine_subsystem(unreal.RuntimeDebugSubsystem)  # non, GameInstance
 *   # Méthode directe :
 *   sub = gi.get_subsystem(unreal.RuntimeDebugSubsystem)
 *   print(sub.get_game_state_json())
 *
 * Retourne un JSON avec tous les acteurs taggés Player/Enemy et leurs propriétés.
 * Utilise la réflexion UE5 pour lire les float properties par nom — aucun couplage
 * avec les classes RPG_Test.
 */
UCLASS()
class ROOMGENERATOR_API URuntimeDebugSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    /**
     * Retourne un JSON string décrivant l'état complet du jeu :
     *   - player : HP, MaxHP, Mana, MaxMana, Location, Velocity, bHasWeapon, bHasShield
     *   - enemies[] : tag, HP, MaxHP, Location, State, bIsDead
     *
     * Exemple de retour :
     * {
     *   "player": { "hp": 80, "max_hp": 100, "mana": 60, "location": [x,y,z] },
     *   "enemies": [ { "hp": 50, "max_hp": 100, "state": "Chase", "location": [x,y,z] } ]
     * }
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeDebug")
    FString GetGameStateJson() const;

    /** Retourne uniquement les infos du joueur (plus rapide). */
    UFUNCTION(BlueprintCallable, Category = "RuntimeDebug")
    FString GetPlayerJson() const;

    /** Retourne la liste des ennemis avec leur état. */
    UFUNCTION(BlueprintCallable, Category = "RuntimeDebug")
    FString GetEnemiesJson() const;

    /** Retourne true si le PIE est actif et le monde disponible. */
    UFUNCTION(BlueprintCallable, Category = "RuntimeDebug")
    bool IsPIEActive() const;

private:
    /** Lit une propriété float sur un acteur par son nom via réflexion. Retourne -1 si absente. */
    float ReadFloat(const AActor* Actor, const FName& PropName) const;

    /** Lit une propriété bool sur un acteur par son nom via réflexion. */
    bool ReadBool(const AActor* Actor, const FName& PropName) const;

    /** Lit une propriété int32 sur un acteur par son nom via réflexion. */
    int32 ReadInt(const AActor* Actor, const FName& PropName) const;

    /** Lit une propriété FString sur un acteur par son nom via réflexion. */
    FString ReadString(const AActor* Actor, const FName& PropName) const;

    /** Sérialise une FVector en "[x, y, z]". */
    FString VecToJson(const FVector& V) const;

    UWorld* GetPIEWorld() const;
};
