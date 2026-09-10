#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "NiagaraEditingSubsystem.generated.h"

class UNiagaraSystem;
class UMaterialInterface;

/**
 * NiagaraEditingSubsystem — contrôle complet des assets Niagara depuis Python.
 *
 * Exemple d'usage Python (configurer un cloud trail style Sable) :
 *   nes = unreal.get_editor_subsystem(unreal.NiagaraEditingSubsystem)
 *   ns  = unreal.load_asset("/Game/HoverGame/FX/NS_HoverClouds")
 *
 *   print(nes.get_module_names(ns, 0))          # lister les modules disponibles
 *   print(nes.is_lightweight_emitter(ns, 0))    # détecter si LW
 *   print(nes.get_sprite_renderer_material(ns, 0))  # matériau actuel
 *
 *   nes.set_sprite_renderer_material(ns, 0, "/Game/.../M_CloudPuff.M_CloudPuff")
 *   nes.set_module_float(ns, 0, "SpawnRate",  "SpawnRate", 15.0)
 *   nes.set_module_float(ns, 0, "Lifetime",   "Minimum",   1.0)
 *   nes.set_module_float(ns, 0, "Lifetime",   "Maximum",   2.0)
 *   nes.set_module_float(ns, 0, "SpriteSize", "Minimum",   200.0)
 *   nes.set_module_float(ns, 0, "SpriteSize", "Maximum",   400.0)
 *   nes.set_module_float(ns, 0, "AddVelocity","SpeedMin",  150.0)
 *   nes.set_module_float(ns, 0, "AddVelocity","SpeedMax",  350.0)
 *   nes.set_module_float(ns, 0, "AddVelocity","ConeAngle", 120.0)
 *   nes.set_module_bool (ns, 0, "GravityForce","bEnabled", False)
 *   nes.set_module_float(ns, 0, "Drag",        "Drag",     3.0)
 *   nes.compile_system(ns)
 *   unreal.EditorAssetLibrary.save_asset("/Game/HoverGame/FX/NS_HoverClouds")
 */
UCLASS()
class ROOMGENERATOR_API UNiagaraEditingSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:

    // ── USER PARAMETERS ──────────────────────────────────────────────────────

    /** Ajoute User.{ParamName} de type float. SetVariableFloat(FName("User.X"), v) fonctionne après. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool AddUserParamFloat(UNiagaraSystem* System, const FString& ParamName, float DefaultValue = 0.f);

    /** Ajoute User.{ParamName} de type LinearColor. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool AddUserParamLinearColor(UNiagaraSystem* System, const FString& ParamName, FLinearColor DefaultValue);

    /** Ajoute User.{ParamName} de type Vector. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool AddUserParamVector(UNiagaraSystem* System, const FString& ParamName, FVector DefaultValue);

    /** Ajoute User.{ParamName} de type bool. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool AddUserParamBool(UNiagaraSystem* System, const FString& ParamName, bool DefaultValue = false);

    /** Liste les User Parameters exposés : ["User.SpawnRate|NiagaraFloat", ...] */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    TArray<FString> GetUserParams(UNiagaraSystem* System);

    // ── EMITTER INFO ─────────────────────────────────────────────────────────

    /** Noms des émetteurs du système. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    TArray<FString> GetEmitterNames(UNiagaraSystem* System);

    /** True si l'émetteur est Lightweight (SetVariable* peut être no-op sans User Params). */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool IsLightweightEmitter(UNiagaraSystem* System, int32 EmitterIndex);

    // ── MODULE READ ───────────────────────────────────────────────────────────

    /** Liste tous les modules Niagara d'un émetteur.
     *  Retourne ["NomModule|Phase", ...] ex: ["SpawnRate|Spawn", "GravityForce|Update"] */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    TArray<FString> GetModuleNames(UNiagaraSystem* System, int32 EmitterIndex);

    /** Lit la valeur float d'un pin de module. Retourne -1.0 si pin introuvable. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    float GetModuleFloat(UNiagaraSystem* System, int32 EmitterIndex,
                         const FString& ModuleName, const FString& PropertyName);

    // ── MODULE WRITE ──────────────────────────────────────────────────────────

    /** Définit un pin float d'un module Niagara.
     *  ModuleName  : partie du nom de module (ex: "SpawnRate", "AddVelocity", "Drag").
     *  PropertyName: nom du pin d'entrée (ex: "SpawnRate", "Minimum", "Maximum", "ConeAngle").
     *  Fonctionne sur les graphes Spawn ET Update.
     */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool SetModuleFloat(UNiagaraSystem* System, int32 EmitterIndex,
                        const FString& ModuleName, const FString& PropertyName, float Value);

    /** Définit un pin Vector d'un module (direction, position, etc.). */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool SetModuleVector(UNiagaraSystem* System, int32 EmitterIndex,
                         const FString& ModuleName, const FString& PropertyName, FVector Value);

    /** Définit un pin LinearColor d'un module (couleur constante, etc.). */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool SetModuleColor(UNiagaraSystem* System, int32 EmitterIndex,
                        const FString& ModuleName, const FString& PropertyName, FLinearColor Value);

    /** Définit un pin bool d'un module (bEnabled, bCompute, etc.). */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool SetModuleBool(UNiagaraSystem* System, int32 EmitterIndex,
                       const FString& ModuleName, const FString& PropertyName, bool Value);

    // ── MATERIAL ─────────────────────────────────────────────────────────────

    /** Retourne le chemin du matériau sur le Sprite Renderer (ou "" si aucun/introuvable). */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    FString GetSpriteRendererMaterial(UNiagaraSystem* System, int32 EmitterIndex);

    /** Assigne un matériau au Sprite Renderer. Corrigé pour fonctionner sur LW ET Standard. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool SetSpriteRendererMaterial(UNiagaraSystem* System, int32 EmitterIndex,
                                   const FString& MaterialPath);

    // ── COMPILATION ──────────────────────────────────────────────────────────

    /** Active ou désactive un module entier (ex: GravityForce → false pour désactiver la gravité). */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool SetModuleEnabled(UNiagaraSystem* System, int32 EmitterIndex,
                          const FString& ModuleName, bool bEnabled);

    /** Liste les paramètres accessibles d'un module (noms de pins dans la chaîne InputMap).
     *  Utiliser pour trouver les noms exacts à passer à SetModuleFloat/Vector/Color/Bool. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    TArray<FString> GetModuleParameters(UNiagaraSystem* System, int32 EmitterIndex,
                                        const FString& ModuleName);

    /** Déclenche la recompilation du système Niagara. Appeler après toute modification. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    bool CompileSystem(UNiagaraSystem* System);

    // ── DIAGNOSTIC ───────────────────────────────────────────────────────────

    /** Diagnostique l'état interne d'un émetteur. */
    UFUNCTION(BlueprintCallable, Category="NiagaraEditing")
    TArray<FString> DiagnoseEmitter(UNiagaraSystem* System, int32 EmitterIndex);

private:
    FString NormalizeParamName(const FString& Name) const;
};
