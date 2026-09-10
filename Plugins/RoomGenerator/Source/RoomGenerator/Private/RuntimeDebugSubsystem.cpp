#include "RuntimeDebugSubsystem.h"

#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers internes
// ─────────────────────────────────────────────────────────────────────────────

UWorld* URuntimeDebugSubsystem::GetPIEWorld() const
{
    // Pendant le PIE, get_game_world() côté Python appelle GetGameInstance()->GetWorld()
    // Ici on passe par l'outer (GameInstance) → World
    if (UGameInstance* GI = GetGameInstance())
        return GI->GetWorld();
    return nullptr;
}

float URuntimeDebugSubsystem::ReadFloat(const AActor* Actor, const FName& PropName) const
{
    if (!Actor) return -1.f;
    FProperty* Prop = Actor->GetClass()->FindPropertyByName(PropName);
    if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
        return FP->GetPropertyValue_InContainer(Actor);
    // Chercher aussi dans la hiérarchie
    for (TFieldIterator<FFloatProperty> It(Actor->GetClass()); It; ++It)
        if (It->GetFName() == PropName)
            return It->GetPropertyValue_InContainer(Actor);
    return -1.f;
}

bool URuntimeDebugSubsystem::ReadBool(const AActor* Actor, const FName& PropName) const
{
    if (!Actor) return false;
    for (TFieldIterator<FBoolProperty> It(Actor->GetClass()); It; ++It)
        if (It->GetFName() == PropName)
            return It->GetPropertyValue_InContainer(Actor);
    return false;
}

int32 URuntimeDebugSubsystem::ReadInt(const AActor* Actor, const FName& PropName) const
{
    if (!Actor) return -1;
    for (TFieldIterator<FIntProperty> It(Actor->GetClass()); It; ++It)
        if (It->GetFName() == PropName)
            return It->GetPropertyValue_InContainer(Actor);
    return -1;
}

FString URuntimeDebugSubsystem::ReadString(const AActor* Actor, const FName& PropName) const
{
    if (!Actor) return TEXT("?");
    for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
    {
        if (It->GetFName() != PropName) continue;

        // Enum → string
        if (FEnumProperty* EP = CastField<FEnumProperty>(*It))
        {
            int64 Val = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Actor));
            return EP->GetEnum()->GetNameStringByValue(Val);
        }
        // Byte enum
        if (FByteProperty* BP = CastField<FByteProperty>(*It))
        {
            if (BP->Enum)
            {
                uint8 Val = BP->GetPropertyValue_InContainer(Actor);
                return BP->Enum->GetNameStringByValue(Val);
            }
        }
    }
    return TEXT("?");
}

FString URuntimeDebugSubsystem::VecToJson(const FVector& V) const
{
    return FString::Printf(TEXT("[%.1f, %.1f, %.1f]"), V.X, V.Y, V.Z);
}

// ─────────────────────────────────────────────────────────────────────────────

bool URuntimeDebugSubsystem::IsPIEActive() const
{
    UWorld* W = GetPIEWorld();
    return W && W->HasBegunPlay();
}

// ─────────────────────────────────────────────────────────────────────────────

FString URuntimeDebugSubsystem::GetPlayerJson() const
{
    UWorld* W = GetPIEWorld();
    if (!W) return TEXT("{\"error\": \"PIE non actif\"}");

    APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;

    if (!Pawn)
        return TEXT("{\"error\": \"Pas de joueur\"}");

    float HP      = ReadFloat(Pawn, FName("CurrentHP"));
    float MaxHP   = ReadFloat(Pawn, FName("MaxHP"));
    float Mana    = ReadFloat(Pawn, FName("CurrentMana"));
    float MaxMana = ReadFloat(Pawn, FName("MaxMana"));
    bool  bWeapon = ReadBool(Pawn,  FName("bHasWeapon"));
    bool  bShield = ReadBool(Pawn,  FName("bHasShield"));
    bool  bBlock  = ReadBool(Pawn,  FName("bIsBlocking"));

    FVector Loc = Pawn->GetActorLocation();
    FVector Vel = Pawn->GetVelocity();
    float Speed = Vel.Size2D();

    return FString::Printf(
        TEXT("{\"hp\":%.0f,\"max_hp\":%.0f,\"mana\":%.0f,\"max_mana\":%.0f,"
             "\"has_weapon\":%s,\"has_shield\":%s,\"is_blocking\":%s,"
             "\"speed\":%.1f,\"location\":%s}"),
        HP, MaxHP, Mana, MaxMana,
        bWeapon ? TEXT("true") : TEXT("false"),
        bShield ? TEXT("true") : TEXT("false"),
        bBlock  ? TEXT("true") : TEXT("false"),
        Speed,
        *VecToJson(Loc)
    );
}

// ─────────────────────────────────────────────────────────────────────────────

FString URuntimeDebugSubsystem::GetEnemiesJson() const
{
    UWorld* W = GetPIEWorld();
    if (!W) return TEXT("[]");

    TArray<FString> Entries;

    for (TActorIterator<AActor> It(W); It; ++It)
    {
        AActor* A = *It;
        if (!A->ActorHasTag(FName("Enemy"))) continue;

        float HP    = ReadFloat(A, FName("CurrentHP"));
        float MaxHP = ReadFloat(A, FName("MaxHP"));
        bool  bDead = ReadBool(A,  FName("bIsDead"));
        FString State = ReadString(A, FName("State"));
        FVector Loc = A->GetActorLocation();

        Entries.Add(FString::Printf(
            TEXT("{\"class\":\"%s\",\"hp\":%.0f,\"max_hp\":%.0f,\"dead\":%s,\"state\":\"%s\",\"location\":%s}"),
            *A->GetClass()->GetName(),
            HP, MaxHP,
            bDead ? TEXT("true") : TEXT("false"),
            *State,
            *VecToJson(Loc)
        ));
    }

    return TEXT("[") + FString::Join(Entries, TEXT(",")) + TEXT("]");
}

// ─────────────────────────────────────────────────────────────────────────────

FString URuntimeDebugSubsystem::GetGameStateJson() const
{
    return FString::Printf(
        TEXT("{\"player\":%s,\"enemies\":%s}"),
        *GetPlayerJson(),
        *GetEnemiesJson()
    );
}
