#include "SBlockoutToolPanel.h"
#include "BlockoutGeometrySubsystem.h"
#include "BlockoutDrawMode.h"
#include "BlockoutCutMode.h"
#include "BlockoutDrawSettings.h"
#include "Editor.h"
#include "EditorModeManager.h"   // GLevelEditorModeTools().ActivateMode
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
#include "Widgets/Input/SSpinBox.h"   // SBlockoutToolPanel.h ne fait que DECLARER SSpinBox<float> :
                                      // toute dereference (SetValue/GetValue) exige le type complet.
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "BlockoutToolPanel"

// SBlockoutToolPanel_Actions.cpp -- Gabarit de reference, Annuler/Selectionner,
// boutons "utiliser la camera", Alignement sur grille, Duplication en serie.
// Issu du decoupage de SBlockoutToolPanel.cpp (2026-07-30, session 21).

// GetEAS() est maintenant SBlockoutToolPanel::GetEAS(), definie une seule fois
// dans SBlockoutToolPanel.cpp (voir commentaire dans le .h) -- ne pas la redupliquer
// ici, ca casse le build des que ce fichier se retrouve unity-merge avec un autre
// .cpp qui la definit aussi (erreur C2084, trouvee le 2026-08-10).

// ---------------------------------------------------------------------------

FReply SBlockoutToolPanel::OnSpawnReferenceGaugeClicked()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || !GCurrentLevelEditingViewportClient)
    {
        SetStatus(TEXT("Monde éditeur ou viewport introuvable."), true);
        return FReply::Handled();
    }

    // Metriques REELLES du personnage (meme repli PIE -> CDO -> defaut que partout ailleurs).
    FString CapsuleSource, JumpSource;
    const float CapsuleHeight = GetPlayerCapsuleFullHeight(CapsuleSource);
    const float JumpHeight = ComputeMaxJumpHeight(JumpSource);
    float CapsuleRadius = 34.f;
    {
        float MaxStepHeight, WalkableAngle;
        FString MetricsSource;
        GetPlayerStepMetrics(CapsuleRadius, MaxStepHeight, WalkableAngle, MetricsSource);
    }

    // On pose le gabarit sur le SOL sous la camera, pas a la hauteur de la camera --
    // sinon il flotte en l'air et ne sert a rien comme reference. Meme precaution que
    // le verificateur de pente : depart juste au-dessus de la camera pour ne pas
    // transpercer le plafond d'un interieur.
    const FVector CamLoc = GCurrentLevelEditingViewportClient->GetViewLocation();
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GaugeGroundProbe), false);
    FCollisionObjectQueryParams ObjectParams;
    ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
    ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

    FVector GroundLoc = CamLoc;
    FHitResult Hit;
    if (World->SweepSingleByObjectType(Hit,
            CamLoc + FVector(0.f, 0.f, 150.f), CamLoc - FVector(0.f, 0.f, 5000.f),
            FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(FMath::Max(10.f, CapsuleRadius)),
            QueryParams))
    {
        GroundLoc = Hit.ImpactPoint;
    }

    TArray<AActor*> NewActors;
    const FString BaseName = TEXT("Gabarit_Joueur");

    // 1. Volume du joueur : empreinte = diametre de la capsule, hauteur = capsule entiere.
    if (AActor* Body = SpawnRotatedCube(
            GroundLoc + FVector(0.f, 0.f, CapsuleHeight / 2.f), FRotator::ZeroRotator,
            FVector(CapsuleRadius * 2.f, CapsuleRadius * 2.f, CapsuleHeight),
            BaseName + TEXT("_Corps")))
    {
        NewActors.Add(Body);
    }

    // 2. Marqueur de hauteur de saut max : une dalle fine a la hauteur atteignable, plus
    //    large que le corps pour rester visible a cote. Repere directement utile pour
    //    juger si un rebord est franchissable.
    if (JumpHeight > 0.f)
    {
        if (AActor* JumpMark = SpawnRotatedCube(
                GroundLoc + FVector(0.f, 0.f, JumpHeight), FRotator::ZeroRotator,
                FVector(CapsuleRadius * 4.f, CapsuleRadius * 4.f, 4.f),
                BaseName + TEXT("_SautMax")))
        {
            NewActors.Add(JumpMark);
        }
    }

    if (NewActors.Num() == 0)
    {
        SetStatus(TEXT("Échec de la création du gabarit."), true);
        return FReply::Handled();
    }

    PushHistory(BaseName, NewActors);

    const FString Msg = FString::Printf(
        TEXT("Gabarit posé : capsule %.0f×%.0f UU (%s), saut max %.0f UU (%s)"),
        CapsuleRadius * 2.f, CapsuleHeight, *CapsuleSource, JumpHeight, *JumpSource);
    if (GaugeInfoLabel.IsValid())
    {
        GaugeInfoLabel->SetText(FText::FromString(Msg));
    }
    SetStatus(Msg + TEXT(" — supprime-le avec Annuler quand tu n'en as plus besoin."), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUndoClicked()
{
    if (GenerationHistory.Num() == 0)
    {
        SetStatus(TEXT("Rien a annuler."), true);
        return FReply::Handled();
    }

    const FBlockoutGenerationEntry Entry = GenerationHistory.Pop();
    RefreshLastGenLabel();

    UEditorActorSubsystem* EAS = GetEAS();
    int32 Count = 0;
    for (const TWeakObjectPtr<AActor>& WeakA : Entry.Actors)
    {
        AActor* A = WeakA.Get();
        if (!A)
        {
            continue; // deja detruit entre-temps (ex: supprime manuellement par l'utilisateur)
        }
        if (EAS)
        {
            EAS->DestroyActor(A);
        }
        else
        {
            A->Destroy();
        }
        ++Count;
    }

    if (Count == 0)
    {
        SetStatus(FString::Printf(TEXT("'%s' : deja supprime ou introuvable."), *Entry.Name), true);
    }
    else
    {
        SetStatus(FString::Printf(TEXT("'%s' supprime (%d acteur(s))."), *Entry.Name, Count), false);
    }
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUndoAllClicked()
{
    // Vide TOUT l'historique de la session en un clic -- pratique en fin de test/
    // itération sans avoir à cliquer "Annuler" une fois par génération. Detruit dans
    // l'ordre INVERSE de creation (Pop() successif), meme comportement que OnUndoClicked
    // repete a la main, juste sans les clics.
    if (GenerationHistory.Num() == 0)
    {
        SetStatus(TEXT("Rien a annuler."), true);
        return FReply::Handled();
    }

    UEditorActorSubsystem* EAS = GetEAS();
    int32 EntriesCleared = 0;
    int32 ActorsDestroyed = 0;
    while (GenerationHistory.Num() > 0)
    {
        const FBlockoutGenerationEntry Entry = GenerationHistory.Pop();
        ++EntriesCleared;
        for (const TWeakObjectPtr<AActor>& WeakA : Entry.Actors)
        {
            AActor* A = WeakA.Get();
            if (!A)
            {
                continue;
            }
            if (EAS) { EAS->DestroyActor(A); } else { A->Destroy(); }
            ++ActorsDestroyed;
        }
    }
    RefreshLastGenLabel();

    SetStatus(FString::Printf(TEXT("Historique vide : %d génération(s) annulée(s), %d acteur(s) supprimé(s)."),
        EntriesCleared, ActorsDestroyed), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnSelectLastClicked()
{
    if (GenerationHistory.Num() == 0)
    {
        SetStatus(TEXT("Rien a selectionner."), true);
        return FReply::Handled();
    }

    const FBlockoutGenerationEntry& Entry = GenerationHistory.Last();
    TArray<AActor*> ToSelect;
    for (const TWeakObjectPtr<AActor>& WeakA : Entry.Actors)
    {
        if (AActor* A = WeakA.Get())
        {
            ToSelect.Add(A);
        }
    }

    if (ToSelect.Num() == 0)
    {
        SetStatus(FString::Printf(TEXT("'%s' : introuvable (deja supprime ?)."), *Entry.Name), true);
        return FReply::Handled();
    }

    UEditorActorSubsystem* EAS = GetEAS();
    if (EAS)
    {
        EAS->SetSelectedLevelActors(ToSelect);
    }

    SetStatus(FString::Printf(TEXT("'%s' selectionne (%d acteur(s)) — deplace-le avec le gizmo dans le viewport."),
        *Entry.Name, ToSelect.Num()), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUseCameraForRoomClicked()
{
    // RoomPosX/Y/ZBox convertis en SSpinBox<float> (session 21) -> variante Spin.
    FillPositionFromCameraSpin(RoomPosXBox, RoomPosYBox, RoomPosZBox);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUseCameraForCorridorClicked()
{
    FillPositionFromCameraSpin(CorrStartXBox, CorrStartYBox, CorrPosZBox);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUseCameraForSlabClicked()
{
    FillPositionFromCameraSpin(SlabPosXBox, SlabPosYBox, SlabPosZBox);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUseCameraForStaircaseClicked()
{
    FillPositionFromCamera(StairPosXBox, StairPosYBox, StairPosZBox);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUseCameraForRampClicked()
{
    FillPositionFromCamera(RampPosXBox, RampPosYBox, RampPosZBox);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUseCameraForVisionConeClicked()
{
    FillPositionFromCamera(ConePosXBox, ConePosYBox, ConePosZBox);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUseCameraForBridgeArchClicked()
{
    FillPositionFromCamera(ArchPosXBox, ArchPosYBox, ArchPosZBox);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUseCameraForCurvedTunnelClicked()
{
    FillPositionFromCamera(TunnelPosXBox, TunnelPosYBox, TunnelPosZBox);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnUseCameraForFovClicked()
{
    // Contrairement a FillPositionFromCamera (position seule, utilise par les 6
    // generateurs de blockout), cet outil a aussi besoin de la ROTATION de la
    // camera viewport -- nouvel helper GetActiveViewportCameraTransform() dedie.
    FVector Loc;
    FRotator Rot;
    if (!GetActiveViewportCameraTransform(Loc, Rot))
    {
        SetStatus(TEXT("Aucun viewport perspective actif trouve."), true);
        return FReply::Handled();
    }
    if (FovPosXBox.IsValid()) FovPosXBox->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), Loc.X)));
    if (FovPosYBox.IsValid()) FovPosYBox->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), Loc.Y)));
    if (FovPosZBox.IsValid()) FovPosZBox->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), Loc.Z)));
    if (FovYawBox.IsValid())   FovYawBox->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), Rot.Yaw)));
    if (FovPitchBox.IsValid()) FovPitchBox->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), Rot.Pitch)));
    SetStatus(FString::Printf(TEXT("Vue remplie depuis la caméra : (%.0f, %.0f, %.0f), yaw %.1f°, pitch %.1f°"),
        Loc.X, Loc.Y, Loc.Z, Rot.Yaw, Rot.Pitch), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnJumpToFovViewClicked()
{
    if (!GCurrentLevelEditingViewportClient)
    {
        SetStatus(TEXT("Aucun viewport perspective actif trouve."), true);
        return FReply::Handled();
    }

    const float PosX = ParseFloat(FovPosXBox, 0.f);
    const float PosY = ParseFloat(FovPosYBox, 0.f);
    const float PosZ = ParseFloat(FovPosZBox, 0.f);
    const float Yaw   = ParseFloat(FovYawBox, 0.f);
    const float Pitch = ParseFloat(FovPitchBox, 0.f);

    FString FovSource;
    float FOV = GetPlayerCameraFOV(FovSource);
    if (FovOverrideBox.IsValid())
    {
        FString OverrideText = FovOverrideBox->GetText().ToString();
        OverrideText.TrimStartAndEndInline();
        if (!OverrideText.IsEmpty())
        {
            FOV = FCString::Atof(*OverrideText);
            FovSource = TEXT("valeur saisie manuellement");
        }
    }
    FOV = FMath::Clamp(FOV, 1.f, 170.f);

    // API FEditorViewportClient -- non re-verifiee par compilation reelle dans cette
    // session (voir GAME_MEMORY.md/SESSION_RESUME.md), a confirmer au premier build.
    GCurrentLevelEditingViewportClient->SetViewLocation(FVector(PosX, PosY, PosZ));
    GCurrentLevelEditingViewportClient->SetViewRotation(FRotator(Pitch, Yaw, 0.f));
    GCurrentLevelEditingViewportClient->ViewFOV = FOV;
    GCurrentLevelEditingViewportClient->FOVAngle = FOV;
    GCurrentLevelEditingViewportClient->Invalidate();

    SetStatus(FString::Printf(TEXT("Vue déplacée à (%.0f, %.0f, %.0f), FOV %.0f° (%s)"),
        PosX, PosY, PosZ, FOV, *FovSource), false);
    return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Alignement sur grille
// ---------------------------------------------------------------------------

FVector SBlockoutToolPanel::SnapPositionIfEnabled(float X, float Y, float Z) const
{
    // Migre vers UBlockoutGeometrySubsystem::SnapToGrid le 2026-08-10 (voir
    // Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md) -- ce wrapper ne fait plus
    // que lire l'etat des widgets et deleguer le calcul pur au sous-systeme.
    const bool bEnabled = GridSnapCheck.IsValid() && GridSnapCheck->IsChecked();
    const float GridSize = ParseFloat(GridSizeBox, 100.f);
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        // Repli sur l'ancien calcul si le sous-systeme est indisponible --
        // ne jamais laisser la position en entree si la grille est cochee.
        if (!bEnabled) { return FVector(X, Y, Z); }
        const float G = FMath::Max(1.f, GridSize);
        return FVector(FMath::RoundToFloat(X / G) * G, FMath::RoundToFloat(Y / G) * G, FMath::RoundToFloat(Z / G) * G);
    }
    return Sub->SnapToGrid(FVector(X, Y, Z), GridSize, bEnabled);
}

FReply SBlockoutToolPanel::OnSnapSelectionToGridClicked()
{
    UEditorActorSubsystem* EAS = GetEAS();
    if (!EAS)
    {
        SetStatus(TEXT("EditorActorSubsystem introuvable."), true);
        return FReply::Handled();
    }

    TArray<AActor*> Selected = EAS->GetSelectedLevelActors();
    if (Selected.Num() == 0)
    {
        SetStatus(TEXT("Aucun acteur sélectionné dans le viewport."), true);
        return FReply::Handled();
    }

    const float GridSize = FMath::Max(1.f, ParseFloat(GridSizeBox, 100.f));
    UBlockoutGeometrySubsystem* Sub = GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>();

    // Ctrl+Z restaure les positions d'origine -- ce n'est PAS une operation destructive
    // (rien n'est detruit/recree, contrairement au Swap intelligent) mais deplacer des
    // acteurs deja poses merite quand meme un retour arriere facile.
    const FScopedTransaction Transaction(LOCTEXT("SnapGridTransaction", "Aligner sur la grille"));

    int32 Moved = 0;
    for (AActor* A : Selected)
    {
        if (!A)
        {
            continue;
        }
        A->Modify();
        const FVector Loc = A->GetActorLocation();
        // Meme noyau que SnapPositionIfEnabled (UBlockoutGeometrySubsystem::SnapToGrid),
        // bEnabled force a true : ce bouton n'a de sens que si on veut aligner.
        const FVector Snapped = Sub
            ? Sub->SnapToGrid(Loc, GridSize, true)
            : FVector(FMath::RoundToFloat(Loc.X / GridSize) * GridSize,
                      FMath::RoundToFloat(Loc.Y / GridSize) * GridSize,
                      FMath::RoundToFloat(Loc.Z / GridSize) * GridSize);
        A->SetActorLocation(Snapped);
        ++Moved;
    }

    SetStatus(FString::Printf(TEXT("%d acteur(s) aligné(s) sur la grille de %.0f UU — Ctrl+Z pour annuler."),
        Moved, GridSize), false);
    return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Duplication en serie
// ---------------------------------------------------------------------------

FReply SBlockoutToolPanel::OnDuplicateInSeriesClicked()
{
    UEditorActorSubsystem* EAS = GetEAS();
    if (!EAS)
    {
        SetStatus(TEXT("EditorActorSubsystem introuvable."), true);
        return FReply::Handled();
    }

    TArray<AActor*> Selected = EAS->GetSelectedLevelActors();
    if (Selected.Num() == 0)
    {
        SetStatus(TEXT("Sélectionne au moins un acteur dans le viewport avant de dupliquer."), true);
        return FReply::Handled();
    }

    const int32 Count = FMath::Clamp(FMath::RoundToInt(ParseFloat(DupCountBox, 5.f)), 1, 500);
    const float Spacing = ParseFloat(DupSpacingBox, 200.f);
    const float Yaw = ParseFloat(DupDirYawBox, 0.f);

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        SetStatus(TEXT("Monde éditeur introuvable."), true);
        return FReply::Handled();
    }

    // Migre vers UBlockoutGeometrySubsystem::ComputeSeriesOffsets le 2026-08-10
    // (voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md) -- seul le calcul des
    // offsets est delegue, le spawn (DuplicateActor, qui a besoin d'un acteur
    // source + monde reels) reste ici.
    UBlockoutGeometrySubsystem* Sub = GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>();
    TArray<FVector> Offsets;
    if (Sub)
    {
        Offsets = Sub->ComputeSeriesOffsets(Yaw, Spacing, Count);
    }
    else
    {
        const float YawRad = FMath::DegreesToRadians(Yaw);
        const FVector Dir(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
        for (int32 i = 1; i <= Count; ++i) { Offsets.Add(Dir * (Spacing * i)); }
    }

    const FScopedTransaction Transaction(LOCTEXT("DuplicateInSeriesTransaction", "Dupliquer en série"));

    // UEditorActorSubsystem::DuplicateActor -- API dediee du module deja utilise ailleurs
    // dans ce panneau (EditorScriptingUtilities), preferee a un SpawnActor+Template manuel :
    // c'est le MEME chemin qu'un Alt+glisser dans l'editeur, donc composants/materiaux/
    // tags/proprietes Blueprint sont copies fidelement, pas seulement les proprietes de
    // base d'AActor.
    TArray<AActor*> NewActors;
    for (AActor* Source : Selected)
    {
        if (!Source)
        {
            continue;
        }

        const FString SourceLabel = Source->GetActorLabel();
        const FName Folder = Source->GetFolderPath();

        for (int32 i = 0; i < Offsets.Num(); ++i)
        {
            AActor* Dup = EAS->DuplicateActor(Source, World, Offsets[i]);
            if (!Dup)
            {
                continue;
            }

            Dup->SetActorLabel(FString::Printf(TEXT("%s_Dup%02d"), *SourceLabel, i + 1));
            Dup->SetFolderPath(Folder);
            NewActors.Add(Dup);
        }
    }

    if (NewActors.Num() == 0)
    {
        SetStatus(TEXT("Échec de la duplication."), true);
        return FReply::Handled();
    }

    PushHistory(TEXT("Duplication"), NewActors);

    const FString Msg = FString::Printf(TEXT("%d copie(s) créée(s) (%d acteur(s) source, %d chacun), espacement %.0f UU"),
        NewActors.Num(), Selected.Num(), Count, Spacing);
    if (DupInfoLabel.IsValid())
    {
        DupInfoLabel->SetText(FText::FromString(Msg));
    }
    SetStatus(Msg, false);
    return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Dessin libre -- bascule vers UBlockoutDrawMode
//
// Le panneau ne peut PAS capter un clic dans le viewport : seul un mode d'edition
// le peut. Ces trois handlers ne font donc que recopier la saisie dans
// UBlockoutDrawSettings (l'instance que le mode lira) puis activer/desactiver le mode.
// ---------------------------------------------------------------------------

FReply SBlockoutToolPanel::OnUseCameraForDrawPlaneClicked()
{
    FVector CameraLocation;
    if (!GetActiveViewportCameraLocation(CameraLocation))
    {
        SetStatus(TEXT("Aucun viewport de niveau actif."), true);
        return FReply::Handled();
    }

    if (DrawGroundZBox.IsValid())
    {
        DrawGroundZBox->SetValue((float)CameraLocation.Z);
    }

    SetStatus(FString::Printf(
        TEXT("Plan de dessin cale sur l'altitude de la CAMERA (Z = %.0f) -- ajuste si tu voulais le sol."),
        CameraLocation.Z), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnStartFreehandDrawClicked()
{
    UBlockoutDrawSettings* Settings = UBlockoutDrawSettings::Get();
    if (!Settings)
    {
        SetStatus(TEXT("Reglages du dessin introuvables."), true);
        return FReply::Handled();
    }

    Settings->RoomName = DrawNameBox.IsValid() ? DrawNameBox->GetText().ToString() : TEXT("Salle_Dessin");
    if (Settings->RoomName.IsEmpty())
    {
        Settings->RoomName = TEXT("Salle_Dessin");
    }
    Settings->GroundZ       = ParseSpinFloat(DrawGroundZBox, 0.f);
    Settings->Height        = FMath::Max(ParseSpinFloat(DrawHeightBox, 300.f), 1.f);
    Settings->WallThickness = FMath::Max(ParseSpinFloat(DrawThicknessBox, 20.f), 1.f);
    // SCheckBox::IsChecked() renvoie DEJA un bool (c'est GetCheckedState() qui rend
    // l'ECheckBoxState a trois etats) -- le comparer a ECheckBoxState::Checked ne compile pas.
    Settings->bAddFloor     = DrawFloorCheck.IsValid()   && DrawFloorCheck->IsChecked();
    Settings->bAddCeiling   = DrawCeilingCheck.IsValid() && DrawCeilingCheck->IsChecked();
    Settings->bExtrudeUp    = true;
    Settings->SaveConfig();

    GLevelEditorModeTools().ActivateMode(UBlockoutDrawMode::EM_BlockoutDraw);

    if (!GLevelEditorModeTools().IsModeActive(UBlockoutDrawMode::EM_BlockoutDraw))
    {
        // Ne jamais annoncer un succes sur la base d'un appel sans retour : on verifie
        // que le mode est REELLEMENT actif avant de le dire.
        SetStatus(TEXT("Le mode Dessin Blockout n'a pas pu etre active (voir l'Output Log)."), true);
        return FReply::Handled();
    }

    SetStatus(FString::Printf(
        TEXT("Mode dessin actif. Clique le contour au sol (Z = %.0f), puis le 1er point pour fermer."),
        Settings->GroundZ), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnStopFreehandDrawClicked()
{
    if (!GLevelEditorModeTools().IsModeActive(UBlockoutDrawMode::EM_BlockoutDraw))
    {
        SetStatus(TEXT("Le mode dessin n'etait pas actif."), false);
        return FReply::Handled();
    }

    GLevelEditorModeTools().DeactivateMode(UBlockoutDrawMode::EM_BlockoutDraw);
    SetStatus(TEXT("Mode dessin quitte."), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnStartCutModeClicked()
{
    GLevelEditorModeTools().ActivateMode(UBlockoutCutMode::EM_BlockoutCut);

    // Meme regle que pour le dessin : ActivateMode ne rend rien, donc on ne peut pas
    // annoncer un succes sur la foi de l'appel. On verifie que le mode est REELLEMENT
    // actif. C'est exactement le cas ou un bouton "marche" en silence sans rien activer.
    if (!GLevelEditorModeTools().IsModeActive(UBlockoutCutMode::EM_BlockoutCut))
    {
        SetStatus(TEXT("Le mode Decoupe Blockout n'a pas pu etre active (voir l'Output Log)."), true);
        return FReply::Handled();
    }

    SetStatus(TEXT("Mode decoupe actif. Survole la face a percer, clique le contour de "
                   "l'ouverture, puis le 1er point (ou Entree) pour percer."), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnStopCutModeClicked()
{
    if (!GLevelEditorModeTools().IsModeActive(UBlockoutCutMode::EM_BlockoutCut))
    {
        SetStatus(TEXT("Le mode decoupe n'etait pas actif."), false);
        return FReply::Handled();
    }

    GLevelEditorModeTools().DeactivateMode(UBlockoutCutMode::EM_BlockoutCut);
    SetStatus(TEXT("Mode decoupe quitte."), false);
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
