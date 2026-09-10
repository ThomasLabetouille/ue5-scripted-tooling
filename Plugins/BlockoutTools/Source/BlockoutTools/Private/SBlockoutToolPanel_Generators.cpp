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

// SBlockoutToolPanel_Generators.cpp -- generateurs de geometrie de blockout :
// Salle, Couloir, Dalle, Escalier, Rampe, Cone de vision IA, Arche de pont,
// Tunnel courbe (+ leurs constantes/formules de calcul, dupliquees ici depuis
// l'ancien namespace anonyme de SBlockoutToolPanel.cpp -- portee de fichier,
// donc une copie par .cpp qui les utilise, comme avant le decoupage).
// Issu du decoupage de SBlockoutToolPanel.cpp (2026-07-30, session 21).

namespace
{
    constexpr float DefaultWallThickness = 20.f;

    // GetEAS() est maintenant SBlockoutToolPanel::GetEAS(), definie une seule fois
    // dans SBlockoutToolPanel.cpp (voir commentaire dans le .h) -- ne pas la
    // redupliquer ici, ca casse le build des que ce fichier se retrouve unity-merge
    // avec un autre .cpp qui la definit aussi (erreur C2084, trouvee le 2026-08-10).

    // Escalier/Rampe : le calcul (ComputeStaircasePlan/ComputeRampPlan) et le spawn
    // (GenerateStaircase/GenerateRamp) vivent desormais sur UBlockoutGeometrySubsystem
    // (2026-07-30, session 21) -- voir OnGenerateStaircaseClicked/OnGenerateRampClicked
    // ci-dessous, qui appellent le sous-systeme au lieu de dupliquer la math ici. Objectif :
    // rendre cette logique testable depuis Python sans passer par un clic UI (voir
    // Content/Python/test_blockout_tools.py).

    // Cone de vision / Arche de pont / Tunnel courbe : meme chose, migres le
    // 2026-08-10 (voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md item #1).
    // FArchArcPoint/ComputeArchArcPoint (calcul d'un point de voussoir sur la
    // demi-ellipse de l'arc) vivent desormais uniquement dans
    // BlockoutGeometrySubsystem.cpp -- ne pas les redupliquer ici.
}

// ---------------------------------------------------------------------------

FReply SBlockoutToolPanel::OnGenerateRoomClicked()
{
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    FString Name;
    if (RoomNameBox.IsValid())
    {
        Name = RoomNameBox->GetText().ToString();
        Name.TrimStartAndEndInline();
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("Room_Blockout");
    }

    const float PosX   = ParseSpinFloat(RoomPosXBox, 0.f);
    const float PosY   = ParseSpinFloat(RoomPosYBox, 0.f);
    const float PosZ   = ParseSpinFloat(RoomPosZBox, 0.f);
    const float SizeX  = ParseSpinFloat(RoomSizeXBox, 1200.f);
    const float SizeY  = ParseSpinFloat(RoomSizeYBox, 1200.f);
    const float Height = ParseSpinFloat(RoomHeightBox, 300.f);
    const bool bNoCeiling = RoomNoCeilingCheck.IsValid() && RoomNoCeilingCheck->IsChecked();
    const FVector Origin = SnapPositionIfEnabled(PosX, PosY, PosZ);

    // bNoCeiling passe desormais directement au sous-systeme (2026-07-30, session 21)
    // -- avant, le panneau spawnait le plafond puis le detruisait aussitot si la case
    // etait cochee. Meme resultat visible, un aller-retour d'acteur en moins, et la
    // decision "avec/sans plafond" fait maintenant partie de la LOGIQUE testable
    // (GenerateRoom), pas d'un post-traitement cote UI.
    const TArray<AActor*> Before = SnapshotAllActors();
    Sub->GenerateRoom(Origin, FVector(SizeX, SizeY, 0.f), Height, DefaultWallThickness, Name, bNoCeiling);
    const TArray<AActor*> NewActors = DiffNewActors(Before);

    PushHistory(Name, NewActors);
    SetStatus(FString::Printf(TEXT("Salle '%s' generee a (%.0f, %.0f, %.0f), %.0fx%.0f%s"),
        *Name, Origin.X, Origin.Y, Origin.Z, SizeX, SizeY, bNoCeiling ? TEXT(" (sans plafond)") : TEXT("")), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnGenerateCorridorClicked()
{
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    FString Name;
    if (CorrNameBox.IsValid())
    {
        Name = CorrNameBox->GetText().ToString();
        Name.TrimStartAndEndInline();
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("Corridor_Blockout");
    }

    const float StartX = ParseSpinFloat(CorrStartXBox, 0.f);
    const float StartY = ParseSpinFloat(CorrStartYBox, 0.f);
    const float EndX   = ParseSpinFloat(CorrEndXBox, 500.f);
    const float EndY   = ParseSpinFloat(CorrEndYBox, 0.f);
    const float PosZ   = ParseSpinFloat(CorrPosZBox, 0.f);
    const float Width  = ParseSpinFloat(CorrWidthBox, 400.f);
    const float Height = ParseSpinFloat(CorrHeightBox, 300.f);
    const FVector Start = SnapPositionIfEnabled(StartX, StartY, PosZ);
    const FVector End   = SnapPositionIfEnabled(EndX, EndY, PosZ);

    const TArray<AActor*> Before = SnapshotAllActors();
    Sub->GenerateCorridor(Start, End, Width, Height, DefaultWallThickness, Name);
    const TArray<AActor*> NewActors = DiffNewActors(Before);

    PushHistory(Name, NewActors);
    SetStatus(FString::Printf(TEXT("Couloir '%s' genere de (%.0f,%.0f) a (%.0f,%.0f), Z=%.0f"),
        *Name, Start.X, Start.Y, End.X, End.Y, Start.Z), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnGenerateSlabClicked()
{
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    FString Name;
    if (SlabNameBox.IsValid())
    {
        Name = SlabNameBox->GetText().ToString();
        Name.TrimStartAndEndInline();
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("Dalle_Blockout");
    }

    const float PosX      = ParseSpinFloat(SlabPosXBox, 0.f);
    const float PosY      = ParseSpinFloat(SlabPosYBox, 0.f);
    const float PosZ      = ParseSpinFloat(SlabPosZBox, 300.f);
    const float SizeX     = ParseSpinFloat(SlabSizeXBox, 800.f);
    const float SizeY     = ParseSpinFloat(SlabSizeYBox, 800.f);
    const float Thickness = ParseSpinFloat(SlabThicknessBox, 20.f);
    const FVector Origin = SnapPositionIfEnabled(PosX, PosY, PosZ);

    constexpr float S = 100.f;
    const TArray<AActor*> Before = SnapshotAllActors();
    AActor* Slab = Sub->SpawnScaledCube(nullptr,
        Origin,
        FVector(SizeX / S, SizeY / S, Thickness / S),
        Name);

    if (!Slab)
    {
        SetStatus(TEXT("Echec de la generation de la dalle."), true);
        return FReply::Handled();
    }

    const TArray<AActor*> NewActors = DiffNewActors(Before);
    PushHistory(Name, NewActors);
    SetStatus(FString::Printf(TEXT("Dalle '%s' generee a (%.0f, %.0f, %.0f), %.0fx%.0f"),
        *Name, Origin.X, Origin.Y, Origin.Z, SizeX, SizeY), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnGenerateStaircaseClicked()
{
    // Calcul + spawn migres vers UBlockoutGeometrySubsystem (2026-07-30, session 21) --
    // c'est desormais le sous-systeme qui porte ComputeStaircasePlan/GenerateStaircase,
    // testable depuis Python sans passer par ce handler. Le handler ne fait plus que
    // lire l'UI, appeler le sous-systeme, et afficher le resultat.
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    FString Name;
    if (StairNameBox.IsValid())
    {
        Name = StairNameBox->GetText().ToString();
        Name.TrimStartAndEndInline();
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("Stair_Blockout");
    }

    const float PosX = ParseFloat(StairPosXBox, 0.f);
    const float PosY = ParseFloat(StairPosYBox, 0.f);
    const float PosZ = ParseFloat(StairPosZBox, 0.f);
    const float Yaw  = ParseFloat(StairYawBox, 0.f);
    const float TotalHeight = ParseFloat(StairHeightBox, 200.f);
    const float Width = ParseFloat(StairWidthBox, 200.f);

    if (TotalHeight <= 0.f)
    {
        SetStatus(TEXT("Hauteur d'escalier invalide (doit etre > 0)."), true);
        return FReply::Handled();
    }

    float CapsuleRadius, MaxStepHeight, WalkableAngle;
    FString MetricsSource;
    GetPlayerStepMetrics(CapsuleRadius, MaxStepHeight, WalkableAngle, MetricsSource);

    int32 PlanNumSteps; float PlanStepHeight, PlanGoing, PlanTotalRun; FString PlanWarning;
    Sub->ComputeStaircasePlan(TotalHeight, CapsuleRadius, MaxStepHeight,
        PlanNumSteps, PlanStepHeight, PlanGoing, PlanTotalRun, PlanWarning);

    const FVector Start = SnapPositionIfEnabled(PosX, PosY, PosZ);
    const TArray<AActor*> NewActors = Sub->GenerateStaircase(Start, Yaw, TotalHeight, Width, CapsuleRadius, MaxStepHeight, Name);

    if (NewActors.Num() == 0)
    {
        SetStatus(TEXT("Echec de la generation de l'escalier."), true);
        return FReply::Handled();
    }

    PushHistory(Name, NewActors);

    FString Msg = FString::Printf(
        TEXT("Escalier '%s' : %d marche(s), hauteur/marche %.1f UU, giron %.1f UU (metriques: %s)"),
        *Name, PlanNumSteps, PlanStepHeight, PlanGoing, *MetricsSource);
    if (!PlanWarning.IsEmpty())
    {
        Msg += TEXT(" — ") + PlanWarning;
    }
    SetStatus(Msg, !PlanWarning.IsEmpty());
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnGenerateRampClicked()
{
    // Calcul + spawn migres vers UBlockoutGeometrySubsystem (2026-07-30, session 21) --
    // voir la meme remarque sur OnGenerateStaircaseClicked ci-dessus.
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    FString Name;
    if (RampNameBox.IsValid())
    {
        Name = RampNameBox->GetText().ToString();
        Name.TrimStartAndEndInline();
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("Ramp_Blockout");
    }

    const float PosX = ParseFloat(RampPosXBox, 0.f);
    const float PosY = ParseFloat(RampPosYBox, 0.f);
    const float PosZ = ParseFloat(RampPosZBox, 0.f);
    const float Yaw  = ParseFloat(RampYawBox, 0.f);
    const float TotalHeight = ParseFloat(RampHeightBox, 150.f);
    const float Width = ParseFloat(RampWidthBox, 200.f);

    if (TotalHeight <= 0.f)
    {
        SetStatus(TEXT("Hauteur de rampe invalide (doit etre > 0)."), true);
        return FReply::Handled();
    }

    // L'angle a priorite sur Longueur horizontale s'il est renseigne (non vide) --
    // meme convention que compute_ramp_plan() (stairs_ramps.py) : fournir l'un OU l'autre.
    // Convention subsystem (Blueprint/Python-friendly, pas de TOptional) : <= 0 = "non fourni".
    float AngleOverrideDeg = 0.f;
    if (RampAngleBox.IsValid())
    {
        FString AngleText = RampAngleBox->GetText().ToString();
        AngleText.ReplaceInline(TEXT(","), TEXT("."));
        AngleText.TrimStartAndEndInline();
        if (!AngleText.IsEmpty())
        {
            AngleOverrideDeg = FCString::Atof(*AngleText);
        }
    }
    const float RunOverride = (AngleOverrideDeg > 0.f) ? 0.f : ParseFloat(RampRunBox, 400.f);

    float CapsuleRadius, MaxStepHeight, WalkableAngle;
    FString MetricsSource;
    GetPlayerStepMetrics(CapsuleRadius, MaxStepHeight, WalkableAngle, MetricsSource);

    float PlanRun, PlanAngleDeg, PlanLength; bool bPlanWalkable; FString PlanWarning;
    Sub->ComputeRampPlan(TotalHeight, RunOverride, AngleOverrideDeg, WalkableAngle,
        PlanRun, PlanAngleDeg, PlanLength, bPlanWalkable, PlanWarning);

    const FVector Start = SnapPositionIfEnabled(PosX, PosY, PosZ);
    AActor* RampActor = Sub->GenerateRamp(Start, Yaw, TotalHeight, Width, RunOverride, AngleOverrideDeg, WalkableAngle, Name);

    if (!RampActor)
    {
        SetStatus(TEXT("Echec de la generation de la rampe."), true);
        return FReply::Handled();
    }

    TArray<AActor*> NewActors;
    NewActors.Add(RampActor);
    PushHistory(Name, NewActors);

    FString Msg = FString::Printf(
        TEXT("Rampe '%s' : angle %.1f deg, longueur %.1f UU, run %.1f UU (metriques: %s)"),
        *Name, PlanAngleDeg, PlanLength, PlanRun, *MetricsSource);
    if (!PlanWarning.IsEmpty())
    {
        Msg += TEXT(" — ") + PlanWarning;
    }
    SetStatus(Msg, !bPlanWalkable);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnGenerateVisionConeClicked()
{
    // Migre vers UBlockoutGeometrySubsystem::GenerateVisionCone le 2026-08-10
    // (voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md item #1) -- ce handler ne
    // fait plus que lire l'UI et deleguer le calcul/spawn au sous-systeme, testable
    // depuis Python independamment de ce fichier Slate.
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    FString Name;
    if (ConeNameBox.IsValid())
    {
        Name = ConeNameBox->GetText().ToString();
        Name.TrimStartAndEndInline();
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("VisionCone_Blockout");
    }

    const float PosX = ParseFloat(ConePosXBox, 0.f);
    const float PosY = ParseFloat(ConePosYBox, 0.f);
    const float PosZ = ParseFloat(ConePosZBox, 0.f);
    const float Yaw  = ParseFloat(ConeYawBox, 0.f);
    const float Radius = ParseFloat(ConeRadiusBox, 500.f);
    const float AngleDeg = ParseFloat(ConeAngleBox, 90.f);
    const float Height = ParseFloat(ConeHeightBox, 10.f);
    const int32 NumSegments = FMath::Max(1, FMath::RoundToInt(ParseFloat(ConeSegmentsBox, 12.f)));

    if (Radius <= 0.f || AngleDeg <= 0.f || AngleDeg > 360.f || Height <= 0.f)
    {
        SetStatus(TEXT("Parametres de cone invalides (portee/angle/epaisseur doivent etre > 0, angle <= 360)."), true);
        return FReply::Handled();
    }

    const FVector Apex = SnapPositionIfEnabled(PosX, PosY, PosZ);
    const TArray<AActor*> NewActors = Sub->GenerateVisionCone(Apex, Yaw, Radius, AngleDeg, Height, NumSegments, Name);

    if (NewActors.Num() == 0)
    {
        SetStatus(TEXT("Echec de la generation du cone de vision."), true);
        return FReply::Handled();
    }

    PushHistory(Name, NewActors);
    SetStatus(FString::Printf(TEXT("Cône de vision '%s' : %d segment(s), angle %.0f°, portée %.0f UU"),
        *Name, NumSegments, AngleDeg, Radius), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnGenerateBridgeArchClicked()
{
    // Migre vers UBlockoutGeometrySubsystem::GenerateBridgeArch le 2026-08-10
    // (voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md item #1), meme demarche
    // que le Cone de vision ci-dessus.
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    FString Name;
    if (ArchNameBox.IsValid())
    {
        Name = ArchNameBox->GetText().ToString();
        Name.TrimStartAndEndInline();
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("Arch_Blockout");
    }

    const float PosX = ParseFloat(ArchPosXBox, 0.f);
    const float PosY = ParseFloat(ArchPosYBox, 0.f);
    const float PosZ = ParseFloat(ArchPosZBox, 0.f);
    const float Yaw  = ParseFloat(ArchYawBox, 0.f);
    const float Span = ParseFloat(ArchSpanBox, 600.f);
    const float Rise = ParseFloat(ArchRiseBox, 250.f);
    const float Thickness = ParseFloat(ArchThicknessBox, 40.f);
    const float Width = ParseFloat(ArchWidthBox, 300.f);
    const float PierHeight = ParseFloat(ArchPierHeightBox, 100.f);
    const float DeckThickness = ParseFloat(ArchDeckThicknessBox, 30.f);
    const int32 NumSegments = FMath::Max(2, FMath::RoundToInt(ParseFloat(ArchSegmentsBox, 10.f)));

    if (Span <= 0.f || Rise <= 0.f || Thickness <= 0.f || Width <= 0.f || PierHeight < 0.f || DeckThickness <= 0.f)
    {
        SetStatus(TEXT("Parametres d'arche invalides (portee/fleche/epaisseur/largeur doivent etre > 0)."), true);
        return FReply::Handled();
    }

    const FVector Base = SnapPositionIfEnabled(PosX, PosY, PosZ);
    const TArray<AActor*> NewActors = Sub->GenerateBridgeArch(Base, Yaw, Span, Rise, Thickness, Width,
        PierHeight, DeckThickness, NumSegments, Name);

    if (NewActors.Num() == 0)
    {
        SetStatus(TEXT("Echec de la generation de l'arche."), true);
        return FReply::Handled();
    }

    PushHistory(Name, NewActors);
    SetStatus(FString::Printf(TEXT("Arche '%s' : %d voussoir(s), portée %.0f UU, flèche %.0f UU"),
        *Name, NumSegments, Span, Rise), false);
    return FReply::Handled();
}

FReply SBlockoutToolPanel::OnGenerateCurvedTunnelClicked()
{
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    FString Name;
    if (TunnelNameBox.IsValid())
    {
        Name = TunnelNameBox->GetText().ToString();
        Name.TrimStartAndEndInline();
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("Tunnel_Blockout");
    }

    const float PosX = ParseFloat(TunnelPosXBox, 0.f);
    const float PosY = ParseFloat(TunnelPosYBox, 0.f);
    const float PosZ = ParseFloat(TunnelPosZBox, 0.f);
    const float Yaw  = ParseFloat(TunnelYawBox, 0.f);
    const float Radius = ParseFloat(TunnelRadiusBox, 800.f);
    const float AngleDeg = ParseFloat(TunnelAngleBox, 90.f);
    const float Width = ParseFloat(TunnelWidthBox, 400.f);
    const float Height = ParseFloat(TunnelHeightBox, 300.f);
    const int32 NumSegments = FMath::Max(1, FMath::RoundToInt(ParseFloat(TunnelSegmentsBox, 6.f)));

    if (Radius <= 0.f || Width <= 0.f || Height <= 0.f)
    {
        SetStatus(TEXT("Parametres de tunnel invalides (rayon/largeur/hauteur doivent etre > 0)."), true);
        return FReply::Handled();
    }
    if (FMath::IsNearlyZero(AngleDeg))
    {
        SetStatus(TEXT("Angle nul -- utilise l'outil Couloir pour un tunnel droit."), true);
        return FReply::Handled();
    }

    // Migre vers UBlockoutGeometrySubsystem::GenerateCurvedTunnel le 2026-08-10
    // (voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md item #1) : le calcul des
    // points de la courbe (ComputeCurvedTunnelPoints) est desormais couvert par
    // un test automatise specifiquement sur le SENS du virage (gauche/droite
    // selon le signe de l'angle) -- c'etait le point explicitement marque "NON
    // VERIFIE VISUELLEMENT" avant cette migration. Un test scripte ne remplace
    // pas un vrai coup d'oeil en PIE/screenshot (regle n°1 de CLAUDE.md), donc
    // une confirmation visuelle reste recommandee au moins une fois.
    const FVector Start = SnapPositionIfEnabled(PosX, PosY, PosZ);
    const TArray<AActor*> NewActors = Sub->GenerateCurvedTunnel(Start, Yaw, Radius, AngleDeg, Width, Height,
        DefaultWallThickness, NumSegments, Name);

    if (NewActors.Num() == 0)
    {
        SetStatus(TEXT("Echec de la generation du tunnel."), true);
        return FReply::Handled();
    }

    PushHistory(Name, NewActors);
    SetStatus(FString::Printf(
        TEXT("Tunnel '%s' : %d segment(s), rayon %.0f UU, angle %.0f°"),
        *Name, NumSegments, Radius, AngleDeg), false);
    return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Generateur depuis un plan 2D
// ---------------------------------------------------------------------------


#undef LOCTEXT_NAMESPACE
