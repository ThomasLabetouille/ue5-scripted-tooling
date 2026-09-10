#include "PIECommandSubsystem.h"

#include "Editor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

// ─────────────────────────────────────────────────────────────────────────────

UWorld* UPIECommandSubsystem::GetPIEWorld() const
{
    // Cherche le monde PIE actif parmi tous les mondes enregistrés
    if (!GEditor) return nullptr;

    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
    {
        if (Ctx.WorldType == EWorldType::PIE && Ctx.World() && Ctx.World()->HasBegunPlay())
            return Ctx.World();
    }
    return nullptr;
}

AActor* UPIECommandSubsystem::FindActorByTag(UWorld* World, const FString& Tag) const
{
    if (!World) return nullptr;
    const FName TagName(*Tag);

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if ((*It)->ActorHasTag(TagName))
            return *It;
    }
    return nullptr;
}

TArray<AActor*> UPIECommandSubsystem::FindAllActorsByTag(UWorld* World, const FString& Tag) const
{
    TArray<AActor*> Result;
    if (!World) return Result;
    const FName TagName(*Tag);

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if ((*It)->ActorHasTag(TagName))
            Result.Add(*It);
    }
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────

bool UPIECommandSubsystem::ExecuteInPIE(const FString& ConsoleCommand)
{
    UWorld* W = GetPIEWorld();
    if (!W) return false;

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
    {
        PC->ConsoleCommand(ConsoleCommand, true);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────

bool UPIECommandSubsystem::CallFunction(AActor* Actor, const FString& FunctionName, float ArgFloat, const FString& ArgString)
{
    if (!Actor) return false;

    UFunction* Func = Actor->FindFunction(FName(*FunctionName));
    if (!Func) return false;

    // Cas 1 : fonction sans paramètres
    if (Func->ParmsSize == 0)
    {
        Actor->ProcessEvent(Func, nullptr);
        return true;
    }

    // Fix 2026-07-21 (audit RoomGenerator) : l'ancien bridge (fix 2026-07-20) ne supportait
    // qu'un unique paramètre float, et refusait proprement tout le reste (bool/int32/FString,
    // ou plusieurs paramètres) — sûr, mais limitant pour piloter des fonctions de gameplay/
    // playtest courantes. Extension du type de paramètre supporté à bool/int32/FString en plus
    // de float. La règle de sécurité du 2026-07-20 reste inchangée : toujours refuser plutôt que
    // deviner si la fonction n'a pas EXACTEMENT 1 paramètre d'entrée, ou si son type n'est pas
    // l'un des 4 supportés.
    FProperty* SingleParam = nullptr;
    int32 ParamCount = 0;
    for (TFieldIterator<FProperty> ParamIt(Func); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
    {
        if (ParamIt->PropertyFlags & CPF_ReturnParm) continue; // ignorer la valeur de retour
        ++ParamCount;
        SingleParam = *ParamIt;
    }

    if (ParamCount != 1 || !SingleParam)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("CallFunction: '%s' a %d parametre(s) d'entree — seule une signature a ")
            TEXT("EXACTEMENT 1 parametre est supportee par ce bridge, appel refuse."),
            *FunctionName, ParamCount);
        return false;
    }

    // Buffer de paramètres construit/détruit proprement via InitializeStruct/DestroyStruct
    // (pattern UE standard pour appeler une UFunction par réflexion) — nécessaire pour gérer
    // correctement un FStrProperty (alloue de la mémoire sur le tas), contrairement à un simple
    // FMemory::Memzero qui suffisait quand seul float était supporté.
    uint8* Params = (uint8*)FMemory_Alloca(Func->ParmsSize);
    Func->InitializeStruct(Params);

    bool bTypeSupported = true;
    if (FFloatProperty* FP = CastField<FFloatProperty>(SingleParam))
    {
        const float Value = ArgString.IsEmpty() ? ArgFloat : FCString::Atof(*ArgString);
        FP->SetPropertyValue_InContainer(Params, Value);
    }
    else if (FBoolProperty* BP = CastField<FBoolProperty>(SingleParam))
    {
        const FString S = ArgString.IsEmpty() ? FString::SanitizeFloat(ArgFloat) : ArgString;
        const bool bValue = S.Equals(TEXT("true"), ESearchCase::IgnoreCase) || S.Equals(TEXT("1"));
        BP->SetPropertyValue_InContainer(Params, bValue);
    }
    else if (FIntProperty* IP = CastField<FIntProperty>(SingleParam))
    {
        const int32 Value = ArgString.IsEmpty() ? FMath::RoundToInt(ArgFloat) : FCString::Atoi(*ArgString);
        IP->SetPropertyValue_InContainer(Params, Value);
    }
    else if (FStrProperty* SP = CastField<FStrProperty>(SingleParam))
    {
        SP->SetPropertyValue_InContainer(Params, ArgString);
    }
    else
    {
        bTypeSupported = false;
    }

    bool bResult = false;
    if (bTypeSupported)
    {
        Actor->ProcessEvent(Func, Params);
        bResult = true;
    }
    else
    {
        // Meme philosophie que le fix 2026-07-20 : refuser plutot que d'executer a l'aveugle
        // avec un parametre non initialise/mal type (ex: struct, enum, TArray, objet...).
        UE_LOG(LogTemp, Warning,
            TEXT("CallFunction: '%s' a un type de parametre non supporte (%s) — appel refuse ")
            TEXT("plutot que d'executer a l'aveugle."),
            *FunctionName, *SingleParam->GetCPPType());
    }

    Func->DestroyStruct(Params);
    return bResult;
}

// ─────────────────────────────────────────────────────────────────────────────

FString UPIECommandSubsystem::CallActorFunction(const FString& ActorTag, const FString& FunctionName, float ArgFloat, const FString& ArgString)
{
    UWorld* W = GetPIEWorld();
    if (!W)
        return TEXT("ERREUR: PIE non actif");

    // Tag "Player" → utiliser le pawn du joueur directement
    AActor* Target = nullptr;
    if (ActorTag == TEXT("Player") || ActorTag == TEXT("player"))
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
            Target = PC->GetPawn();
    }
    else
    {
        Target = FindActorByTag(W, ActorTag);
    }

    if (!Target)
        return FString::Printf(TEXT("ERREUR: Aucun acteur avec le tag '%s'"), *ActorTag);

    bool bOK = CallFunction(Target, FunctionName, ArgFloat, ArgString);
    if (!bOK)
        return FString::Printf(TEXT("ERREUR: Fonction '%s' introuvable ou signature non supportee sur %s"),
            *FunctionName, *Target->GetClass()->GetName());

    return ArgString.IsEmpty()
        ? FString::Printf(TEXT("OK: %s::%s(%.1f) appelé"),
            *Target->GetClass()->GetName(), *FunctionName, ArgFloat)
        : FString::Printf(TEXT("OK: %s::%s(\"%s\") appelé"),
            *Target->GetClass()->GetName(), *FunctionName, *ArgString);
}

// ─────────────────────────────────────────────────────────────────────────────

int32 UPIECommandSubsystem::CallAllActorsFunction(const FString& ActorTag, const FString& FunctionName, float ArgFloat, const FString& ArgString)
{
    UWorld* W = GetPIEWorld();
    if (!W) return 0;

    TArray<AActor*> Targets = FindAllActorsByTag(W, ActorTag);
    int32 Count = 0;
    for (AActor* A : Targets)
    {
        if (CallFunction(A, FunctionName, ArgFloat, ArgString))
            ++Count;
    }
    return Count;
}

// ─────────────────────────────────────────────────────────────────────────────

FString UPIECommandSubsystem::ListActorFunctions(const FString& ActorTag) const
{
    UWorld* W = GetPIEWorld();
    if (!W) return TEXT("[]");

    AActor* Target = nullptr;
    if (ActorTag == TEXT("Player") || ActorTag == TEXT("player"))
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
            Target = PC->GetPawn();
    }
    else
    {
        Target = FindActorByTag(W, ActorTag);
    }
    if (!Target) return TEXT("[]");

    TArray<FString> Names;
    for (TFieldIterator<UFunction> FuncIt(Target->GetClass()); FuncIt; ++FuncIt)
    {
        UFunction* Func = *FuncIt;
        if (Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
        {
            Names.Add(FString::Printf(TEXT("\"%s\""), *Func->GetName()));
        }
    }
    Names.Sort();
    return TEXT("[") + FString::Join(Names, TEXT(",")) + TEXT("]");
}

// ─────────────────────────────────────────────────────────────────────────────

bool UPIECommandSubsystem::TeleportPlayer(float X, float Y, float Z)
{
    UWorld* W = GetPIEWorld();
    if (!W) return false;

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
    {
        APawn* Pawn = PC->GetPawn();
        if (Pawn)
        {
            Pawn->SetActorLocation(FVector(X, Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────

bool UPIECommandSubsystem::SetPlayerHP(float HP)
{
    UWorld* W = GetPIEWorld();
    if (!W) return false;

    APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!Pawn) return false;

    // Modifier CurrentHP directement via réflexion
    for (TFieldIterator<FFloatProperty> It(Pawn->GetClass()); It; ++It)
    {
        if (It->GetFName() == FName("CurrentHP"))
        {
            It->SetPropertyValue_InContainer(Pawn, HP);
            return true;
        }
    }
    return false;
}
