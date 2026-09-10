#include "SBlockoutToolPanel.h"
#include "BlockoutGeometrySubsystem.h"
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "LevelEditorViewport.h"

#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
// Resolution automatique du personnage du projet hote (ResolveProjectCharacterClass) --
// ce plugin n'a aucun chemin de Blueprint code en dur, il interroge le projet.
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameMapsSettings.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "CollisionShape.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UObjectGlobals.h"

#include "AssetRegistry/AssetData.h"
#include "PropertyCustomizationHelpers.h"   // SObjectPropertyEntryBox (module PropertyEditor)
#include "ScopedTransaction.h"              // FScopedTransaction (undo Ctrl+Z du Swap)

// Generateur depuis un plan 2D -- lecture d'une Texture2D (FTextureSource, editeur)
// ou d'un PNG sur disque (IImageWrapper).
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"   // persistance des champs (GEditorPerProjectIni)

#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "BlockoutToolPanel"

// SBlockoutToolPanel_Swap.cpp -- Swap intelligent (remplace le blockout par un mesh
// d'art final, en conservant position/rotation/tags/nom/dossier Outliner).
// Issu du decoupage de SBlockoutToolPanel.cpp (2026-07-30, session 21).

// GetEAS() est maintenant SBlockoutToolPanel::GetEAS(), definie une seule fois
// dans SBlockoutToolPanel.cpp (voir commentaire dans le .h) -- ne pas la redupliquer
// ici, ca casse le build des que ce fichier se retrouve unity-merge avec un autre
// .cpp qui la definit aussi (erreur C2084, trouvee le 2026-08-10).

// ---------------------------------------------------------------------------

FString SBlockoutToolPanel::GetSwapMeshPath() const
{
    return SwapMeshPath;
}

void SBlockoutToolPanel::OnSwapMeshChanged(const FAssetData& AssetData)
{
    SwapMeshPath = AssetData.GetObjectPathString();
    if (SwapInfoLabel.IsValid())
    {
        SwapInfoLabel->SetText(SwapMeshPath.IsEmpty()
            ? LOCTEXT("SwapMeshCleared", "(aucun mesh sélectionné)")
            : FText::FromString(FString::Printf(TEXT("Mesh sélectionné : %s"), *AssetData.AssetName.ToString())));
    }
}

TArray<AActor*> SBlockoutToolPanel::FindActorsMatchingSwapIdentifier(FString& OutIdentifier) const
{
    // Migre vers UBlockoutGeometrySubsystem::FindActorsByLabelContains le
    // 2026-08-10 (voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md item #2) --
    // ce wrapper ne fait plus que lire le champ UI et deleguer au sous-systeme.
    OutIdentifier.Empty();
    if (SwapIdentifierBox.IsValid())
    {
        OutIdentifier = SwapIdentifierBox->GetText().ToString();
        OutIdentifier.TrimStartAndEndInline();
    }
    if (OutIdentifier.IsEmpty())
    {
        return TArray<AActor*>();
    }

    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    return Sub ? Sub->FindActorsByLabelContains(nullptr, OutIdentifier) : TArray<AActor*>();
}

FReply SBlockoutToolPanel::OnSwapCountClicked()
{
    FString Identifier;
    const TArray<AActor*> Matches = FindActorsMatchingSwapIdentifier(Identifier);

    if (Identifier.IsEmpty())
    {
        SetStatus(TEXT("Saisis un identifiant (ex: Boite_Porte_A) avant de compter."), true);
        return FReply::Handled();
    }

    if (SwapInfoLabel.IsValid())
    {
        SwapInfoLabel->SetText(FText::FromString(FString::Printf(
            TEXT("%d boîte(s) trouvée(s) pour '%s'"), Matches.Num(), *Identifier)));
    }
    SetStatus(FString::Printf(TEXT("%d boîte(s) correspondent à '%s' — rien n'a été modifié."),
        Matches.Num(), *Identifier), Matches.Num() == 0);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnSwapReplaceClicked()
{
    // Migre vers UBlockoutGeometrySubsystem::SwapActorsToMesh le 2026-08-10 (voir
    // Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md item #2) -- deja signale "NON
    // VERIFIE en conditions reelles" avant cette migration, seul item du backlog
    // de test juge suffisamment a risque (action destructive) pour justifier
    // l'effort. La transaction/undo (FScopedTransaction, Modify()) reste une
    // responsabilite du panneau Slate -- le sous-systeme ne fait aucune
    // hypothese sur un contexte d'undo actif ou non.
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        SetStatus(TEXT("Monde editeur introuvable."), true);
        return FReply::Handled();
    }

    UBlockoutGeometrySubsystem* Sub = GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>();
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    if (SwapMeshPath.IsEmpty())
    {
        SetStatus(TEXT("Choisis d'abord le mesh final dans le sélecteur."), true);
        return FReply::Handled();
    }

    UStaticMesh* NewMesh = LoadObject<UStaticMesh>(nullptr, *SwapMeshPath);
    if (!NewMesh)
    {
        SetStatus(FString::Printf(TEXT("Mesh introuvable : %s"), *SwapMeshPath), true);
        return FReply::Handled();
    }

    FString Identifier;
    TArray<AActor*> Matches = FindActorsMatchingSwapIdentifier(Identifier);
    if (Identifier.IsEmpty())
    {
        SetStatus(TEXT("Saisis un identifiant (ex: Boite_Porte_A) avant de remplacer."), true);
        return FReply::Handled();
    }
    if (Matches.Num() == 0)
    {
        SetStatus(FString::Printf(TEXT("Aucune boîte ne correspond à '%s' — rien à remplacer."), *Identifier), true);
        return FReply::Handled();
    }

    // Operation DESTRUCTIVE (detruit les boites d'origine) -- encapsulee dans une
    // transaction editeur pour que Ctrl+Z dans le viewport restaure l'etat d'avant
    // (boites recreees + meshes supprimes).
    const FScopedTransaction Transaction(LOCTEXT("SwapBlockoutTransaction", "Swap blockout vers mesh final"));
    World->Modify();
    for (AActor* Old : Matches)
    {
        if (Old) Old->Modify();
    }

    const TArray<AActor*> NewActors = Sub->SwapActorsToMesh(nullptr, Matches, NewMesh);
    const int32 Replaced = NewActors.Num();

    if (SwapInfoLabel.IsValid())
    {
        SwapInfoLabel->SetText(FText::FromString(FString::Printf(
            TEXT("%d boîte(s) remplacée(s) par %s"), Replaced, *NewMesh->GetName())));
    }
    SetStatus(FString::Printf(
        TEXT("%d boîte(s) '%s' remplacée(s) par %s (transform + tags conservés) — Ctrl+Z pour annuler."),
        Replaced, *Identifier, *NewMesh->GetName()), Replaced == 0);
    return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Gabarit de reference (echelle joueur)
// ---------------------------------------------------------------------------


#undef LOCTEXT_NAMESPACE
