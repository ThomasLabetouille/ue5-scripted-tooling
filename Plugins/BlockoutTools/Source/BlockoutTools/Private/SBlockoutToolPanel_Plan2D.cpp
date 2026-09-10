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

// SBlockoutToolPanel_Plan2D.cpp -- Generateur depuis un plan 2D (lecture d'une
// Texture2D ou d'un PNG disque, murs erige la ou l'image est sombre).
// Issu du decoupage de SBlockoutToolPanel.cpp (2026-07-30, session 21).

FString SBlockoutToolPanel::GetPlanTexturePath() const
{
    return PlanTexturePath;
}

void SBlockoutToolPanel::OnPlanTextureChanged(const FAssetData& AssetData)
{
    PlanTexturePath = AssetData.GetObjectPathString();
    if (PlanInfoLabel.IsValid())
    {
        PlanInfoLabel->SetText(PlanTexturePath.IsEmpty()
            ? LOCTEXT("PlanTextureCleared", "(aucune texture sélectionnée)")
            : FText::FromString(FString::Printf(TEXT("Plan sélectionné : %s"), *AssetData.AssetName.ToString())));
    }
}

FReply SBlockoutToolPanel::OnUseCameraForPlanClicked()
{
    FillPositionFromCamera(PlanPosXBox, PlanPosYBox, PlanPosZBox);
    return FReply::Handled();
}

bool SBlockoutToolPanel::LoadPlanPixels(TArray<uint8>& OutMask, int32& OutW, int32& OutH,
                                        FString& OutSourceName, FString& OutError) const
{
    OutMask.Reset();
    OutW = 0;
    OutH = 0;
    OutSourceName.Empty();
    OutError.Empty();

    const int32 Threshold = FMath::Clamp(FMath::RoundToInt(ParseFloat(PlanThresholdBox, 128.f)), 0, 255);

    // Un pixel compte comme MUR s'il est sombre ET suffisamment opaque -- un plan
    // dessine sur fond transparent (PNG Photoshop) ne doit pas produire de murs
    // partout la ou il n'y a rien.
    auto BuildMaskFromBGRA = [Threshold](const uint8* Data, int64 NumBytes, int32 W, int32 H, TArray<uint8>& Out) -> bool
    {
        const int64 Needed = (int64)W * (int64)H * 4;
        if (!Data || NumBytes < Needed)
        {
            return false;
        }
        Out.SetNumZeroed(W * H);
        for (int32 i = 0; i < W * H; ++i)
        {
            const uint8 B = Data[i * 4 + 0];
            const uint8 G = Data[i * 4 + 1];
            const uint8 R = Data[i * 4 + 2];
            const uint8 A = Data[i * 4 + 3];
            const int32 Lum = (R * 299 + G * 587 + B * 114) / 1000;
            Out[i] = (A >= 128 && Lum < Threshold) ? 1 : 0;
        }
        return true;
    };

    // ── Source 1 : Texture2D importee (prioritaire si une texture est choisie) ──
    if (!PlanTexturePath.IsEmpty())
    {
        UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *PlanTexturePath);
        if (!Tex)
        {
            OutError = FString::Printf(TEXT("Texture introuvable : %s"), *PlanTexturePath);
            return false;
        }

        // FTextureSource = les pixels ORIGINAUX importes, independamment des reglages
        // de compression/sRGB/mipmaps de la texture -- c'est ce qui evite d'avoir a
        // configurer quoi que ce soit sur l'asset avant de l'utiliser ici.
        FTextureSource& Src = Tex->Source;
        if (!Src.IsValid())
        {
            OutError = TEXT("Source de la texture indisponible (asset sans données source ?).");
            return false;
        }

        OutW = Src.GetSizeX();
        OutH = Src.GetSizeY();
        const ETextureSourceFormat Fmt = Src.GetFormat();

        TArray64<uint8> Raw;
        if (!Src.GetMipData(Raw, 0, 0, 0, nullptr))
        {
            OutError = TEXT("Lecture des pixels de la texture impossible.");
            return false;
        }

        if (Fmt == TSF_BGRA8)
        {
            if (!BuildMaskFromBGRA(Raw.GetData(), Raw.Num(), OutW, OutH, OutMask))
            {
                OutError = TEXT("Données de texture incomplètes (BGRA8).");
                return false;
            }
        }
        else if (Fmt == TSF_G8)
        {
            const int64 Needed = (int64)OutW * (int64)OutH;
            if (Raw.Num() < Needed)
            {
                OutError = TEXT("Données de texture incomplètes (G8).");
                return false;
            }
            OutMask.SetNumZeroed(OutW * OutH);
            for (int32 i = 0; i < OutW * OutH; ++i)
            {
                OutMask[i] = (Raw[i] < Threshold) ? 1 : 0;
            }
        }
        else
        {
            // Message explicite plutot qu'un echec silencieux ou un resultat absurde.
            OutError = TEXT("Format de texture non supporté (attendu BGRA8 ou G8). Réimporte le plan en PNG standard, ou utilise le champ 'Fichier PNG'.");
            return false;
        }

        OutSourceName = Tex->GetName();
        return true;
    }

    // ── Source 2 : fichier PNG (ou autre format image) sur disque ──
    FString FilePath;
    if (PlanFilePathBox.IsValid())
    {
        FilePath = PlanFilePathBox->GetText().ToString();
        FilePath.TrimStartAndEndInline();
        FilePath.TrimQuotesInline();
    }
    if (FilePath.IsEmpty())
    {
        OutError = TEXT("Choisis une texture OU renseigne un chemin de fichier image.");
        return false;
    }
    if (!FPaths::FileExists(FilePath))
    {
        OutError = FString::Printf(TEXT("Fichier introuvable : %s"), *FilePath);
        return false;
    }

    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        OutError = FString::Printf(TEXT("Lecture du fichier impossible : %s"), *FilePath);
        return false;
    }

    IImageWrapperModule& ImgModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    const EImageFormat DetectedFormat = ImgModule.DetectImageFormat(FileData.GetData(), FileData.Num());
    if (DetectedFormat == EImageFormat::Invalid)
    {
        OutError = TEXT("Format d'image non reconnu (PNG, JPG, BMP, TGA acceptés).");
        return false;
    }

    TSharedPtr<IImageWrapper> Wrapper = ImgModule.CreateImageWrapper(DetectedFormat);
    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
    {
        OutError = TEXT("Décodage de l'image impossible.");
        return false;
    }

    TArray64<uint8> Raw;
    if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
    {
        OutError = TEXT("Conversion de l'image en BGRA impossible.");
        return false;
    }

    OutW = Wrapper->GetWidth();
    OutH = Wrapper->GetHeight();
    if (!BuildMaskFromBGRA(Raw.GetData(), Raw.Num(), OutW, OutH, OutMask))
    {
        OutError = TEXT("Données d'image incomplètes après décodage.");
        return false;
    }

    OutSourceName = FPaths::GetCleanFilename(FilePath);
    return true;
}

FReply SBlockoutToolPanel::OnGenerateFromPlanClicked()
{
    FString Name;
    if (PlanNameBox.IsValid())
    {
        Name = PlanNameBox->GetText().ToString();
        Name.TrimStartAndEndInline();
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("Plan_Blockout");
    }

    TArray<uint8> Mask;
    int32 ImgW = 0, ImgH = 0;
    FString SourceName, Error;
    if (!LoadPlanPixels(Mask, ImgW, ImgH, SourceName, Error))
    {
        SetStatus(Error, true);
        return FReply::Handled();
    }
    if (ImgW <= 0 || ImgH <= 0)
    {
        SetStatus(TEXT("Image vide ou dimensions invalides."), true);
        return FReply::Handled();
    }

    const FVector PlanOrigin = SnapPositionIfEnabled(
        ParseFloat(PlanPosXBox, 0.f), ParseFloat(PlanPosYBox, 0.f), ParseFloat(PlanPosZBox, 0.f));
    const float PosX = PlanOrigin.X;
    const float PosY = PlanOrigin.Y;
    const float PosZ = PlanOrigin.Z;
    const float UUPerPixel = FMath::Max(0.01f, ParseFloat(PlanScaleBox, 10.f));
    const float WallHeight = FMath::Max(1.f, ParseFloat(PlanWallHeightBox, 300.f));
    const int32 Cell = FMath::Clamp(FMath::RoundToInt(ParseFloat(PlanCellPixelsBox, 4.f)), 1, 64);
    const bool bWithFloor = PlanFloorCheck.IsValid() && PlanFloorCheck->IsChecked();

    // Migre vers UBlockoutGeometrySubsystem::ComputePlanWallRects le 2026-08-10
    // (voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md) -- le noyau pur
    // (echantillonnage + fusion gloutonne + placement monde) est desormais
    // testable depuis Python. Seuls LoadPlanPixels (lecture texture/fichier,
    // deja au-dessus) et le spawn ci-dessous restent cote handler Slate.
    UBlockoutGeometrySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
    if (!Sub)
    {
        SetStatus(TEXT("Sous-systeme de geometrie introuvable."), true);
        return FReply::Handled();
    }

    TArray<FBlockoutPlanWallRect> Walls;
    Sub->ComputePlanWallRects(Mask, ImgW, ImgH, Cell, UUPerPixel, WallHeight, PosX, PosY, PosZ, Walls);

    if (Walls.Num() == 0)
    {
        SetStatus(TEXT("Aucun pixel sombre détecté — augmente le seuil de noir, ou vérifie que le plan a bien des traits foncés."), true);
        return FReply::Handled();
    }

    // Garde-fou : un plan tres detaille avec une resolution trop fine peut produire des
    // milliers d'acteurs (Outliner ingerable, niveau lourd). On refuse proprement en
    // expliquant quoi ajuster, plutot que de faire ramer l'editeur.
    constexpr int32 MaxWallActors = 2000;
    if (Walls.Num() > MaxWallActors)
    {
        SetStatus(FString::Printf(
            TEXT("%d murs seraient générés (limite %d) — augmente 'Résolution' (actuellement %d px/cellule) pour regrouper davantage."),
            Walls.Num(), MaxWallActors, Cell), true);
        return FReply::Handled();
    }

    const float PlanWorldW = ImgW * UUPerPixel;
    const float PlanWorldH = ImgH * UUPerPixel;

    TArray<AActor*> NewActors;
    NewActors.Reserve(Walls.Num() + 1);

    int32 Index = 0;
    for (const FBlockoutPlanWallRect& W : Walls)
    {
        const FString Label = FString::Printf(TEXT("%s_Mur%03d"), *Name, ++Index);
        if (AActor* Wall = SpawnRotatedCube(W.Location, FRotator::ZeroRotator, W.Size, Label))
        {
            NewActors.Add(Wall);
        }
    }

    if (bWithFloor)
    {
        constexpr float FloorThickness = 20.f;
        const FVector FloorLoc(PosX, PosY, PosZ - FloorThickness / 2.f);
        if (AActor* Floor = SpawnRotatedCube(FloorLoc, FRotator::ZeroRotator,
                FVector(PlanWorldW, PlanWorldH, FloorThickness), Name + TEXT("_Sol")))
        {
            NewActors.Add(Floor);
        }
    }

    if (NewActors.Num() == 0)
    {
        SetStatus(TEXT("Échec de la génération depuis le plan."), true);
        return FReply::Handled();
    }

    PushHistory(Name, NewActors);

    if (PlanInfoLabel.IsValid())
    {
        PlanInfoLabel->SetText(FText::FromString(FString::Printf(
            TEXT("%s : %dx%d px → %d mur(s)%s"),
            *SourceName, ImgW, ImgH, Walls.Num(), bWithFloor ? TEXT(" + sol") : TEXT(""))));
    }
    SetStatus(FString::Printf(
        TEXT("Plan '%s' généré depuis %s (%dx%d px) : %d mur(s)%s, emprise %.0fx%.0f UU"),
        *Name, *SourceName, ImgW, ImgH, Walls.Num(),
        bWithFloor ? TEXT(" + sol") : TEXT(""), PlanWorldW, PlanWorldH), false);
    return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Swap intelligent : blockout -> mesh final
// ---------------------------------------------------------------------------


#undef LOCTEXT_NAMESPACE
