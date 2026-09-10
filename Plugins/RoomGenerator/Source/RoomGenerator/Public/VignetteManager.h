#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/PostProcessVolume.h"
#include "VignetteManager.generated.h"

/**
 * AVignetteManager
 * Gère DEUX systèmes en un :
 *
 * 1. FEEDBACK DANGER (visuel)
 *    Inspecte les BP_IA_Enemy toutes les 0.08s.
 *    Si un ennemi chasse ET que la lampe torche ne l'éclaire pas :
 *      → augmente l'aberration chromatique + vignette de bord (comme Outlast)
 *      → PAS de color overlay rouge — visibilité totale conservée
 *
 * 2. SYSTÈME CACHETTE (gameplay)
 *    bIsPlayerHiding : true quand le joueur est caché.
 *    Quand true : force CanSeePlayer?=false sur tous les ennemis
 *    (les ennemis perdent le joueur même s'ils étaient en poursuite).
 *    BP_HidingSpot appelle SetPlayerHiding(true/false) via Blueprint.
 */
UCLASS(BlueprintType)
class ROOMGENERATOR_API AVignetteManager : public AActor
{
    GENERATED_BODY()

public:
    AVignetteManager();
    virtual void Tick(float DeltaTime) override;

    /** Activer/désactiver la cachette depuis Blueprint (appelé par BP_HidingSpot) */
    UFUNCTION(BlueprintCallable, Category="HidingSystem")
    void SetPlayerHiding(bool bHiding);

    /** Lire l'état de cachette (BlueprintPure = pas de exec pin) */
    UFUNCTION(BlueprintPure, Category="HidingSystem")
    bool GetPlayerHiding() const { return bIsPlayerHiding; }

    /** État de cachette — accessible en lecture depuis Blueprint */
    UPROPERTY(BlueprintReadOnly, Category="HidingSystem")
    bool bIsPlayerHiding = false;

    /**
     * Fix 2026-07-21 (audit RoomGenerator) : ce nom de classe (sous-chaine, insensible à la casse
     * via Contains()) était codé en dur ("BP_IA_Enemy") — hérité de HoverGame, où c'est le vrai
     * nom des ennemis IA. Dans un projet qui ne les nomme pas ainsi (ex: RPG_Test, dont les
     * ennemis sont "BP_RPGEnemy" et sont passifs, pas des IA de poursuite), le système de
     * feedback danger ne matchait jamais RIEN, silencieusement, sans qu'aucune erreur ne
     * remonte — un piège pour quiconque place cet acteur en pensant qu'il fait quelque chose.
     * Exposé en UPROPERTY pour pouvoir le reconfigurer (ou le désactiver via une chaîne vide)
     * sans recompiler. Défaut inchangé pour ne rien casser côté HoverGame.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HidingSystem")
    FString EnemyClassNameFilter = TEXT("BP_IA_Enemy");

private:
    bool  bVignetteActive   = false;
    float CheckTimer        = 0.f;
    float CurrentAberration = 1.5f;
    float CurrentVignette   = 0.9f;
    float PulseTimer        = 0.f;
    float NoMatchWarnTimer  = 0.f;
    bool  bWarnedNoMatch    = false;

    // Valeurs baseline
    static constexpr float BASE_ABERRATION   = 1.5f;
    static constexpr float BASE_VIGNETTE     = 0.9f;
    // Valeurs en danger (aberration chromatique = impression de stress/peur)
    static constexpr float DANGER_ABERRATION = 5.0f;
    static constexpr float DANGER_VIGNETTE   = 1.4f;
    // Interpolation et pulse
    static constexpr float INTERP_SPEED      = 2.0f;
    static constexpr float PULSE_FREQ        = 1.3f;  // ~78 bpm
    // Intervalle de check ennemi
    static constexpr float CHECK_INTERVAL    = 0.08f;

    APostProcessVolume* CachedPPV = nullptr;
    APostProcessVolume* FindUnboundPPV(UWorld* World);

    /** Quand caché : force CanSeePlayer?=false sur tous les ennemis */
    void ForceEnemiesLosePlayer(UWorld* World);
};
