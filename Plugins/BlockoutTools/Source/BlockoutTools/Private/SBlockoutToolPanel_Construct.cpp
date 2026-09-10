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

#include "BlockoutDrawSettings.h"
#include "BlockoutCutMode.h"   // section "Decoupe" : bouton d'activation du mode   // section "Dessin libre" : valeurs partagees avec UBlockoutDrawMode

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

// SBlockoutToolPanel_Construct.cpp -- construction du panneau Slate (Construct()).
// Issu du decoupage de SBlockoutToolPanel.cpp (2026-07-30, session 21).

void SBlockoutToolPanel::Construct(const FArguments& InArgs)
{
    OverlayTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(this, &SBlockoutToolPanel::TickOverlay), 0.0f);

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)

            // ── En-tete ─────────────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Title", "Outil Blockout"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f)
            [ SNew(SSeparator) ]

            // ── Overlay metriques gameplay ─────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(
                    TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(
                        this, &SBlockoutToolPanel::GetMetricsSectionTitle)),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("MetricsHint", "Ligne verte = hauteur de saut max du personnage, à la position du joueur\nen cours de test (sinon au point de départ du niveau).\n\nCercles jaune/rouge = portées de détection/attaque des ennemis. Saisis\nci-dessous le tag qui identifie tes ennemis (laisse « Enemy » si tu n'en\nas pas, ou si tu ne sais pas : rien ne s'affichera, sans erreur)."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("MetricsEnemyTag", "Tag des ennemis"), MetricsEnemyTagBox, TEXT("Enemy")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeCheckboxField(LOCTEXT("OverlayEnable", "Afficher l'overlay"), OverlayEnabledCheck) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [
                SAssignNew(MetricsInfoLabel, STextBlock)
                .Text(LOCTEXT("MetricsIdle", "(overlay desactive)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)))
                .AutoWrapText(true)
            ]

                )
            ]

            // ── Simulateur FOV / Frustum joueur ────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(
                    TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(
                        this, &SBlockoutToolPanel::GetFovSectionTitle)),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("FovHint", "Position/Direction/Inclinaison = point de vue simule. \"Aller a\ncette vue\" deplace la camera du viewport ; la case ci-dessous dessine\nle frustum en direct (visible depuis un autre angle, ex. vue de dessus)."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForFovClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("FovPosX", "Position X"), FovPosXBox, TEXT("0")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("FovPosY", "Position Y"), FovPosYBox, TEXT("0")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("FovPosZ", "Position Z"), FovPosZBox, TEXT("0")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("FovYaw", "Direction (yaw)"), FovYawBox, TEXT("0")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("FovPitch", "Inclinaison (pitch)"), FovPitchBox, TEXT("0")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("FovOverride", "FOV manuel (deg, optionnel)"), FovOverrideBox, TEXT("")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("FovDistance", "Distance du frustum"), FovDistanceBox, TEXT("1500")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("FovAspect", "Ratio d'aspect (L/H)"), FovAspectBox, TEXT("1.778")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("JumpToFov", "📷 Aller à cette vue"))
                .ToolTipText(LOCTEXT("JumpToFovTip", "Déplace la caméra du viewport éditeur à cette position/rotation, avec le FOV du joueur"))
                .OnClicked(this, &SBlockoutToolPanel::OnJumpToFovViewClicked)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeCheckboxField(LOCTEXT("FovShowFrustum", "Afficher le frustum (en direct)"), FovShowFrustumCheck) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [
                SAssignNew(FovInfoLabel, STextBlock)
                .Text(LOCTEXT("FovIdle", "(frustum désactivé)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.8f)))
                .AutoWrapText(true)
            ]

                )
            ]

            // ── Vérificateur de pente automatique ──────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(
                    TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(
                        this, &SBlockoutToolPanel::GetSlopeSectionTitle)),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("SlopeHint", "Coche la case : colore le sol autour de la caméra du viewport, en direct.\n\nVERT = le joueur peut gravir · ROUGE = trop raide, il glissera.\nLaisse le champ VIDE pour utiliser l'angle marchable réel du personnage\n(rappelé sous la case). Saisis une valeur pour tester un autre seuil.\n\nÀ SAVOIR : la sonde descend à la verticale et mesure le SOL sur lequel\non se tiendrait. Elle ne colore donc pas les faces verticales des murs\n(elle voit leur dessus, plat). Un blockout fait de boîtes et de rampes\ndouces est légitimement tout vert : pour voir du rouge, baisse le seuil\nsous la pente de tes rampes, ou teste sur du relief raide."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            // Seuil VERT -> ORANGE, vide = angle marchable reel du personnage. Sans ce
            // champ (situation des sessions 6 a 16), AUCUNE valeur saisie ne pouvait
            // faire disparaitre le vert sur une pente douce -- d'ou le "peu importe la
            // valeur que je rentre c'est toujours vert" de Thomas.
            [ MakeField(LOCTEXT("SlopeWalkableAngle", "Seuil marchable (vide = joueur)"), SlopeWalkableAngleBox, TEXT("")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
            [ MakeCheckboxField(LOCTEXT("SlopeShow", "Afficher la vérification de pente (en direct)"), SlopeShowCheck) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [
                SAssignNew(SlopeInfoLabel, STextBlock)
                .Text(LOCTEXT("SlopeIdle", "(vérification désactivée)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)))
                .AutoWrapText(true)
            ]

                )
            ]

            // ── Verificateur de hauteur libre ──────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(
                    TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(
                        this, &SBlockoutToolPanel::GetClearanceSectionTitle)),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ClearanceHint", "Le pendant du vérificateur de pente, pour la hauteur.\n\nVERT = le joueur passe debout · ROUGE = plafond trop bas, passage\nbloqué (couloir écrasé, dessous d'arche, dalle posée trop bas).\n\nLaisse le champ VIDE pour utiliser la hauteur réelle de la capsule du\npersonnage. Un endroit à ciel ouvert est toujours vert."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ClearanceMin", "Hauteur libre mini (vide = joueur)"), ClearanceMinBox, TEXT("")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
            [ MakeCheckboxField(LOCTEXT("ClearanceShow", "Afficher la vérification de hauteur (en direct)"), ClearanceShowCheck) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [
                SAssignNew(ClearanceInfoLabel, STextBlock)
                .Text(LOCTEXT("ClearanceIdle", "(vérification désactivée)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)))
                .AutoWrapText(true)
            ]
                )
            ]

            // ── Salle ───────────────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("RoomSection", "Salle"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RoomName", "Nom"), RoomNameBox, TEXT("Salle_1"), [this](){ return OnGenerateRoomClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForRoomClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("RoomPosX", "Position X"), RoomPosXBox, 0.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateRoomClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("RoomPosY", "Position Y"), RoomPosYBox, 0.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateRoomClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("RoomPosZ", "Position Z"), RoomPosZBox, 0.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateRoomClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("RoomSizeX", "Taille X"), RoomSizeXBox, 1200.f, 10.f, 1.f, TOptional<float>(), [this](){ return OnGenerateRoomClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("RoomSizeY", "Taille Y"), RoomSizeYBox, 1200.f, 10.f, 1.f, TOptional<float>(), [this](){ return OnGenerateRoomClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("RoomHeight", "Hauteur"), RoomHeightBox, 300.f, 10.f, 1.f, TOptional<float>(), [this](){ return OnGenerateRoomClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeCheckboxField(LOCTEXT("RoomNoCeiling", "Sans plafond (ouvert vers le haut)"), RoomNoCeilingCheck) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenRoom", "Générer une salle"))
                .OnClicked(this, &SBlockoutToolPanel::OnGenerateRoomClicked)
            ]

                )
            ]

            // ── Couloir ─────────────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("CorrSection", "Couloir"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("CorrName", "Nom"), CorrNameBox, TEXT("Couloir_1"), [this](){ return OnGenerateCorridorClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForCorridorClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 2.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("CorrCameraHint", "(remplit le point de Depart — regle l'Arrivee toi-meme)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("CorrStartX", "Depart X"), CorrStartXBox, 0.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateCorridorClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("CorrStartY", "Depart Y"), CorrStartYBox, 0.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateCorridorClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("CorrEndX", "Arrivee X"), CorrEndXBox, 500.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateCorridorClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("CorrEndY", "Arrivee Y"), CorrEndYBox, 0.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateCorridorClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("CorrPosZ", "Position Z"), CorrPosZBox, 0.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateCorridorClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("CorrWidth", "Largeur"), CorrWidthBox, 400.f, 10.f, 1.f, TOptional<float>(), [this](){ return OnGenerateCorridorClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("CorrHeight", "Hauteur"), CorrHeightBox, 300.f, 10.f, 1.f, TOptional<float>(), [this](){ return OnGenerateCorridorClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenCorr", "Générer un couloir"))
                .OnClicked(this, &SBlockoutToolPanel::OnGenerateCorridorClicked)
            ]

                )
            ]

            // ── Dessin libre (mode viewport) ───────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("DrawSection", "Dessin libre (contour clique dans le viewport)"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DrawHint",
                    "Au lieu de saisir des dimensions : clique le contour de la salle au sol,\n"
                    "clic sur le 1er point pour fermer, puis la hauteur suit la souris.\n"
                    "Ctrl aimante sur la grille. Tirer SOUS le plan extrude vers le bas."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("DrawName", "Nom"), DrawNameBox, TEXT("Salle_Dessin")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForDrawPlaneClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("DrawGroundZ", "Plan de dessin Z"), DrawGroundZBox, 0.f, 10.f, TOptional<float>(), TOptional<float>()) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("DrawHeight", "Hauteur de depart"), DrawHeightBox, 300.f, 10.f, 1.f, TOptional<float>()) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("DrawThickness", "Epaisseur"), DrawThicknessBox, 20.f, 1.f, 1.f, TOptional<float>()) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeCheckboxField(LOCTEXT("DrawFloor", "Generer un sol"), DrawFloorCheck) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeCheckboxField(LOCTEXT("DrawCeiling", "Generer un plafond"), DrawCeilingCheck) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("DrawStart", "Activer le mode dessin"))
                .ToolTipText(LOCTEXT("DrawStartTip", "Bascule le viewport dans le mode Dessin Blockout. Echap (contour vide) revient a la selection."))
                .OnClicked(this, &SBlockoutToolPanel::OnStartFreehandDrawClicked)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("DrawStop", "Quitter le mode dessin"))
                .OnClicked(this, &SBlockoutToolPanel::OnStopFreehandDrawClicked)
            ]

                )
            ]

            // ── Decoupe (mode viewport) ──────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("CutSection", "Decoupe (percer une porte, une fenetre)"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("CutHint",
                    "Survole la face a percer : son contour s'allume. Clique les points de\n"
                    "l'ouverture, puis le 1er point (ou Entree) pour percer.\n"
                    "Retour arriere retire le dernier point. Echap efface, puis quitte.\n"
                    "Ne fonctionne que sur des boites et sur les volumes du mode dessin."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("CutStart", "Activer le mode decoupe"))
                .ToolTipText(LOCTEXT("CutStartTip", "Bascule le viewport dans le mode Decoupe Blockout. Le trou est perce dans le maillage ET dans la collision."))
                .OnClicked(this, &SBlockoutToolPanel::OnStartCutModeClicked)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("CutStop", "Quitter le mode decoupe"))
                .OnClicked(this, &SBlockoutToolPanel::OnStopCutModeClicked)
            ]

                )
            ]

            // ── Dalle / plancher intermediaire ─────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("SlabSection", "Dalle / Plancher (mezzanine, grenier...)"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("SlabHint", "Un plancher seul, sans murs — pose-le a une Position Z\nau milieu d'une salle plus haute pour faire un etage."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("SlabName", "Nom"), SlabNameBox, TEXT("Dalle_1"), [this](){ return OnGenerateSlabClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForSlabClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("SlabPosX", "Position X"), SlabPosXBox, 0.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateSlabClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("SlabPosY", "Position Y"), SlabPosYBox, 0.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateSlabClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("SlabPosZ", "Position Z"), SlabPosZBox, 300.f, 10.f, TOptional<float>(), TOptional<float>(), [this](){ return OnGenerateSlabClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("SlabSizeX", "Taille X"), SlabSizeXBox, 800.f, 10.f, 1.f, TOptional<float>(), [this](){ return OnGenerateSlabClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("SlabSizeY", "Taille Y"), SlabSizeYBox, 800.f, 10.f, 1.f, TOptional<float>(), [this](){ return OnGenerateSlabClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeSpinField(LOCTEXT("SlabThickness", "Epaisseur"), SlabThicknessBox, 20.f, 1.f, 1.f, TOptional<float>(), [this](){ return OnGenerateSlabClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenSlab", "Générer une dalle"))
                .OnClicked(this, &SBlockoutToolPanel::OnGenerateSlabClicked)
            ]

                )
            ]

            // ── Escalier ────────────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("StairSection", "Escalier"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("StairHint", "Position = pied de l'escalier, au sol. Nombre de marches et giron\ncalcules automatiquement (formule de Blondel + metriques du joueur)."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("StairName", "Nom"), StairNameBox, TEXT("Escalier_1"), [this](){ return OnGenerateStaircaseClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForStaircaseClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("StairPosX", "Position X"), StairPosXBox, TEXT("0"), [this](){ return OnGenerateStaircaseClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("StairPosY", "Position Y"), StairPosYBox, TEXT("0"), [this](){ return OnGenerateStaircaseClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("StairPosZ", "Position Z"), StairPosZBox, TEXT("0"), [this](){ return OnGenerateStaircaseClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("StairYaw", "Direction (yaw)"), StairYawBox, TEXT("0"), [this](){ return OnGenerateStaircaseClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("StairHeight", "Hauteur totale"), StairHeightBox, TEXT("200"), [this](){ return OnGenerateStaircaseClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("StairWidth", "Largeur"), StairWidthBox, TEXT("200"), [this](){ return OnGenerateStaircaseClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenStair", "Générer un escalier"))
                .OnClicked(this, &SBlockoutToolPanel::OnGenerateStaircaseClicked)
            ]

                )
            ]

            // ── Rampe ───────────────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("RampSection", "Rampe"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("RampHint", "Position = bas de la rampe, au sol. Remplir soit Longueur horizontale,\nsoit Angle voulu — si Angle est rempli, il est prioritaire sur Longueur."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RampName", "Nom"), RampNameBox, TEXT("Rampe_1"), [this](){ return OnGenerateRampClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForRampClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RampPosX", "Position X"), RampPosXBox, TEXT("0"), [this](){ return OnGenerateRampClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RampPosY", "Position Y"), RampPosYBox, TEXT("0"), [this](){ return OnGenerateRampClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RampPosZ", "Position Z"), RampPosZBox, TEXT("0"), [this](){ return OnGenerateRampClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RampYaw", "Direction (yaw)"), RampYawBox, TEXT("0"), [this](){ return OnGenerateRampClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RampHeight", "Hauteur totale"), RampHeightBox, TEXT("150"), [this](){ return OnGenerateRampClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RampWidth", "Largeur"), RampWidthBox, TEXT("200"), [this](){ return OnGenerateRampClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RampRun", "Longueur horizontale"), RampRunBox, TEXT("400"), [this](){ return OnGenerateRampClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("RampAngle", "Angle voulu (deg, optionnel)"), RampAngleBox, TEXT(""), [this](){ return OnGenerateRampClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenRamp", "Générer une rampe"))
                .OnClicked(this, &SBlockoutToolPanel::OnGenerateRampClicked)
            ]

                )
            ]

            // ── Cone de vision IA ──────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("ConeSection", "Cône de vision IA"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ConeHint", "Position = sommet (apex) au sol. Secteur angulaire plat, en\néventail de segments, centre sur Direction (yaw)."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ConeName", "Nom"), ConeNameBox, TEXT("VisionCone_1"), [this](){ return OnGenerateVisionConeClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForVisionConeClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ConePosX", "Position X"), ConePosXBox, TEXT("0"), [this](){ return OnGenerateVisionConeClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ConePosY", "Position Y"), ConePosYBox, TEXT("0"), [this](){ return OnGenerateVisionConeClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ConePosZ", "Position Z"), ConePosZBox, TEXT("0"), [this](){ return OnGenerateVisionConeClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ConeYaw", "Direction (yaw)"), ConeYawBox, TEXT("0"), [this](){ return OnGenerateVisionConeClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ConeRadius", "Portée"), ConeRadiusBox, TEXT("500"), [this](){ return OnGenerateVisionConeClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ConeAngle", "Angle total (deg)"), ConeAngleBox, TEXT("90"), [this](){ return OnGenerateVisionConeClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ConeHeight", "Épaisseur"), ConeHeightBox, TEXT("10"), [this](){ return OnGenerateVisionConeClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ConeSegments", "Segments"), ConeSegmentsBox, TEXT("12"), [this](){ return OnGenerateVisionConeClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenCone", "Générer un cône de vision"))
                .OnClicked(this, &SBlockoutToolPanel::OnGenerateVisionConeClicked)
            ]

                )
            ]

            // ── Arche de pont ───────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("ArchSection", "Arche de pont"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ArchHint", "Position = sol, au milieu entre les 2 piliers. Direction (yaw) =\naxe de traversee (le tablier roule dans cet axe)."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchName", "Nom"), ArchNameBox, TEXT("Arche_1"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForBridgeArchClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchPosX", "Position X"), ArchPosXBox, TEXT("0"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchPosY", "Position Y"), ArchPosYBox, TEXT("0"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchPosZ", "Position Z"), ArchPosZBox, TEXT("0"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchYaw", "Direction (yaw)"), ArchYawBox, TEXT("0"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchSpan", "Portée (entre piliers)"), ArchSpanBox, TEXT("600"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchRise", "Flèche (hauteur de l'arc)"), ArchRiseBox, TEXT("250"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchThickness", "Épaisseur des voussoirs"), ArchThicknessBox, TEXT("40"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchWidth", "Largeur du pont"), ArchWidthBox, TEXT("300"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchPierHeight", "Hauteur des piliers"), ArchPierHeightBox, TEXT("100"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchDeckThickness", "Épaisseur du tablier"), ArchDeckThicknessBox, TEXT("30"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("ArchSegments", "Voussoirs (segments)"), ArchSegmentsBox, TEXT("10"), [this](){ return OnGenerateBridgeArchClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenArch", "Générer une arche de pont"))
                .OnClicked(this, &SBlockoutToolPanel::OnGenerateBridgeArchClicked)
            ]

                )
            ]

            // ── Tunnel courbé ───────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("TunnelSection", "Tunnel courbé"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("TunnelHint", "Position = depart, au sol. Angle > 0 tourne a gauche, < 0 tourne\na droite. Approxime par une chaine de couloirs droits (segments)."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelName", "Nom"), TunnelNameBox, TEXT("Tunnel_1"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForCurvedTunnelClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelPosX", "Position X"), TunnelPosXBox, TEXT("0"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelPosY", "Position Y"), TunnelPosYBox, TEXT("0"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelPosZ", "Position Z"), TunnelPosZBox, TEXT("0"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelYaw", "Direction de depart (yaw)"), TunnelYawBox, TEXT("0"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelRadius", "Rayon de courbure"), TunnelRadiusBox, TEXT("800"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelAngle", "Angle total (deg, signé)"), TunnelAngleBox, TEXT("90"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelWidth", "Largeur"), TunnelWidthBox, TEXT("400"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelHeight", "Hauteur"), TunnelHeightBox, TEXT("300"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("TunnelSegments", "Segments"), TunnelSegmentsBox, TEXT("6"), [this](){ return OnGenerateCurvedTunnelClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenTunnel", "Générer un tunnel courbé"))
                .OnClicked(this, &SBlockoutToolPanel::OnGenerateCurvedTunnelClicked)
            ]

                )
            ]

            // ── Generateur depuis un plan 2D ───────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("PlanSection", "Générateur depuis un plan 2D"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PlanHint", "Érige des murs là où l'image est SOMBRE (traits noirs = murs).\nRenseigne soit une texture importée, soit un fichier image — si une\ntexture est sélectionnée, elle est prioritaire. Le plan est centré sur\nla Position et les pixels voisins sont fusionnés en grands murs."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("PlanName", "Nom"), PlanNameBox, TEXT("Plan_1"), [this](){ return OnGenerateFromPlanClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.f, 2.f, 6.f, 2.f)
                [
                    SNew(SBox)
                    .WidthOverride(90.f)
                    [ SNew(STextBlock).Text(LOCTEXT("PlanTexture", "Texture du plan")) ]
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .Padding(0.f, 2.f)
                [
                    SNew(SObjectPropertyEntryBox)
                    .AllowedClass(UTexture2D::StaticClass())
                    .ObjectPath(this, &SBlockoutToolPanel::GetPlanTexturePath)
                    .OnObjectChanged(this, &SBlockoutToolPanel::OnPlanTextureChanged)
                    .AllowClear(true)
                    .DisplayUseSelected(true)
                    .DisplayBrowse(true)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("PlanFile", "…ou fichier image"), PlanFilePathBox, TEXT(""), [this](){ return OnGenerateFromPlanClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [ MakeUseCameraButton(FOnClicked::CreateSP(this, &SBlockoutToolPanel::OnUseCameraForPlanClicked)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("PlanPosX", "Position X"), PlanPosXBox, TEXT("0"), [this](){ return OnGenerateFromPlanClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("PlanPosY", "Position Y"), PlanPosYBox, TEXT("0"), [this](){ return OnGenerateFromPlanClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("PlanPosZ", "Position Z"), PlanPosZBox, TEXT("0"), [this](){ return OnGenerateFromPlanClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("PlanScale", "UU par pixel"), PlanScaleBox, TEXT("10"), [this](){ return OnGenerateFromPlanClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("PlanWallHeight", "Hauteur des murs"), PlanWallHeightBox, TEXT("300"), [this](){ return OnGenerateFromPlanClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("PlanCellPixels", "Résolution (px/cellule)"), PlanCellPixelsBox, TEXT("4"), [this](){ return OnGenerateFromPlanClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("PlanThreshold", "Seuil de noir (0-255)"), PlanThresholdBox, TEXT("128"), [this](){ return OnGenerateFromPlanClicked(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeCheckboxField(LOCTEXT("PlanFloor", "Générer aussi une dalle de sol"), PlanFloorCheck) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenPlan", "Générer depuis le plan"))
                .OnClicked(this, &SBlockoutToolPanel::OnGenerateFromPlanClicked)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [
                SAssignNew(PlanInfoLabel, STextBlock)
                .Text(LOCTEXT("PlanIdle", "(aucun plan généré)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.8f, 0.9f)))
                .AutoWrapText(true)
            ]

                )
            ]

            // ── Swap intelligent : blockout → mesh final ───────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("SwapSection", "Swap intelligent (blockout → mesh final)"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("SwapHint", "Remplace TOUTES les boîtes dont le nom contient l'identifiant par\nle mesh d'art choisi. Position, rotation, tags, nom et dossier sont\nconservés ; le mesh garde sa taille d'origine (aucun étirement).\n⚠ Action destructive — utilise Ctrl+Z dans le viewport pour annuler."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("SwapIdentifier", "Identifiant (nom contient)"), SwapIdentifierBox, TEXT("Boîte_Porte_A")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.f, 2.f, 6.f, 2.f)
                [
                    SNew(SBox)
                    .WidthOverride(90.f)
                    [ SNew(STextBlock).Text(LOCTEXT("SwapMesh", "Mesh final")) ]
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .Padding(0.f, 2.f)
                [
                    SNew(SObjectPropertyEntryBox)
                    .AllowedClass(UStaticMesh::StaticClass())
                    .ObjectPath(this, &SBlockoutToolPanel::GetSwapMeshPath)
                    .OnObjectChanged(this, &SBlockoutToolPanel::OnSwapMeshChanged)
                    .AllowClear(true)
                    .DisplayUseSelected(true)
                    .DisplayBrowse(true)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 3.f, 0.f)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("SwapCount", "🔍 Compter"))
                    .ToolTipText(LOCTEXT("SwapCountTip", "Compte les boîtes correspondantes SANS rien modifier — à faire avant de remplacer"))
                    .OnClicked(this, &SBlockoutToolPanel::OnSwapCountClicked)
                ]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(3.f, 0.f, 0.f, 0.f)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("SwapReplace", "♻ Remplacer"))
                    .ToolTipText(LOCTEXT("SwapReplaceTip", "Remplace les boîtes par le mesh final (destructif — Ctrl+Z pour annuler)"))
                    .OnClicked(this, &SBlockoutToolPanel::OnSwapReplaceClicked)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [
                SAssignNew(SwapInfoLabel, STextBlock)
                .Text(LOCTEXT("SwapIdle", "(aucun remplacement effectué)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.8f, 0.9f)))
                .AutoWrapText(true)
            ]

                )
            ]

            // ── Gabarit de reference (echelle joueur) ──────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("GaugeSection", "Gabarit de référence (échelle joueur)"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("GaugeHint", "Pose au sol, sous la caméra, un repère aux dimensions RÉELLES du\npersonnage : un volume à sa taille, et une dalle fine à sa hauteur de\nsaut max. Sert à juger une échelle ou la franchissabilité d'un rebord\nà l'œil, sans lancer le jeu.\n\nC'est un repère jetable : supprime-le avec « Annuler » ci-dessous."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("SpawnGauge", "📏 Poser un gabarit ici"))
                .ToolTipText(LOCTEXT("SpawnGaugeTip", "Pose le gabarit sur le sol situé sous la caméra du viewport"))
                .OnClicked(this, &SBlockoutToolPanel::OnSpawnReferenceGaugeClicked)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [
                SAssignNew(GaugeInfoLabel, STextBlock)
                .Text(LOCTEXT("GaugeIdle", "(aucun gabarit posé)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.8f, 0.9f)))
                .AutoWrapText(true)
            ]
                )
            ]

            // ── Alignement sur grille ───────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("GridSection", "Alignement sur grille"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("GridHint", "Le bouton caméra remplit une position brute (ex. 1237,42) qui n'est\nalignée sur rien. Sans effet en blockout, mais un kit d'art modulaire\nposé par le Swap intelligent sur une position non alignée ne se\nraccorde pas avec ses voisins (joints ouverts).\n\nCoche la case pour qu'à chaque génération, l'ORIGINE saisie soit\narrondie automatiquement au multiple de la taille de grille ci-dessous.\nLe bouton du bas aligne les acteurs déjà posés et sélectionnés dans\nle viewport (Ctrl+Z pour annuler)."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("GridSize", "Taille de grille (UU)"), GridSizeBox, TEXT("100")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [ MakeCheckboxField(LOCTEXT("GridSnap", "Aligner automatiquement à la génération"), GridSnapCheck) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("SnapSelection", "📐 Aligner la sélection sur la grille"))
                .ToolTipText(LOCTEXT("SnapSelectionTip", "Arrondit la position des acteurs sélectionnés dans le viewport au multiple de la taille de grille"))
                .OnClicked(this, &SBlockoutToolPanel::OnSnapSelectionToGridClicked)
            ]
                )
            ]

            // ── Duplication en serie ────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 1.f)
            [
                MakeSection(LOCTEXT("DupSection", "Duplication en série"),
                    SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 4.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DupHint", "Sélectionne un ou plusieurs acteurs dans le viewport, règle Nombre de\ncopies / Espacement / Direction, puis clique Dupliquer. Chaque acteur\nsélectionné est copié N fois le long de la direction donnée -- pratique\npour des piliers, une palissade, des segments de couloir en ligne.\n\nDirection en degrés (0 = vers +X, 90 = vers +Y), même convention que\nles autres outils. Ctrl+Z pour annuler."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("DupCount", "Nombre de copies"), DupCountBox, TEXT("5")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f)
            [ MakeField(LOCTEXT("DupSpacing", "Espacement (UU)"), DupSpacingBox, TEXT("200")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [ MakeField(LOCTEXT("DupDirYaw", "Direction (yaw)"), DupDirYawBox, TEXT("0")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 2.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("Duplicate", "🧱 Dupliquer la sélection"))
                .ToolTipText(LOCTEXT("DuplicateTip", "Duplique chaque acteur sélectionné N fois le long de la direction donnée"))
                .OnClicked(this, &SBlockoutToolPanel::OnDuplicateInSeriesClicked)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 6.f)
            [
                SAssignNew(DupInfoLabel, STextBlock)
                .Text(LOCTEXT("DupIdle", "(rien de dupliqué pour l'instant)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.8f, 0.9f)))
                .AutoWrapText(true)
            ]
                )
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
            [ SNew(SSeparator) ]

            // ── Historique des generations : annuler / tout annuler / selectionner ──
            // Volontairement PAS repliable : c'est le controle d'annulation partage par
            // tous les outils, il doit rester visible en permanence.
            // UX (session 21) : "Annuler" faisait deja GenerationHistory.Pop() -- un clic
            // repete remonte donc deja plusieurs generations en arriere. Ce qui manquait
            // reellement : voir les dernieres entrees (pas seulement la toute derniere,
            // voir RefreshLastGenLabel) et un bouton pour tout vider d'un coup.
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 4.f, 6.f, 2.f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("HistorySection", "Historique des générations"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
            [
                SAssignNew(LastGenLabel, STextBlock)
                .Text(LOCTEXT("NoLastGen", "(rien de généré pour l'instant)"))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 2.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 3.f, 0.f)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("Undo", "↩ Annuler"))
                    .ToolTipText(LOCTEXT("UndoTip", "Supprime les acteurs de la génération la PLUS RÉCENTE de l'historique. Cliquer plusieurs fois de suite remonte plus loin en arrière, une génération à la fois."))
                    .OnClicked(this, &SBlockoutToolPanel::OnUndoClicked)
                ]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(3.f, 0.f, 0.f, 0.f)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("SelectLast", "🎯 Sélectionner"))
                    .ToolTipText(LOCTEXT("SelectLastTip", "Sélectionne les acteurs de la dernière génération pour les déplacer avec le gizmo"))
                    .OnClicked(this, &SBlockoutToolPanel::OnSelectLastClicked)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 6.f)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("UndoAll", "🗑 Tout annuler"))
                .ToolTipText(LOCTEXT("UndoAllTip", "Supprime TOUTES les générations de cette session en un clic (équivalent à cliquer \"Annuler\" jusqu'à vider l'historique)"))
                .OnClicked(this, &SBlockoutToolPanel::OnUndoAllClicked)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 4.f)
            [ SNew(SSeparator) ]

            // ── Statut ──────────────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f)
            [
                SAssignNew(StatusLabel, STextBlock)
                .Text(LOCTEXT("StatusIdle", "Pret."))
                .AutoWrapText(true)
            ]
        ]
    ];

    // Tous les champs sont construits (donc enregistres) : on peut restaurer les
    // valeurs de la session precedente par-dessus les defauts.
    LoadPanelSettings();

    // Section "Dessin libre" : ses valeurs ne vivent PAS dans le .ini du panneau mais
    // dans UBlockoutDrawSettings, partage avec le mode viewport. On les repousse donc
    // APRES LoadPanelSettings, sinon la hauteur reglee a la souris lors du dernier
    // dessin serait ecrasee par l'ancienne valeur du champ.
    if (const UBlockoutDrawSettings* DrawSettings = UBlockoutDrawSettings::Get())
    {
        if (DrawNameBox.IsValid())      { DrawNameBox->SetText(FText::FromString(DrawSettings->RoomName)); }
        if (DrawGroundZBox.IsValid())   { DrawGroundZBox->SetValue(DrawSettings->GroundZ); }
        if (DrawHeightBox.IsValid())    { DrawHeightBox->SetValue(DrawSettings->Height); }
        if (DrawThicknessBox.IsValid()) { DrawThicknessBox->SetValue(DrawSettings->WallThickness); }
        if (DrawFloorCheck.IsValid())   { DrawFloorCheck->SetIsChecked(DrawSettings->bAddFloor ? ECheckBoxState::Checked : ECheckBoxState::Unchecked); }
        if (DrawCeilingCheck.IsValid()) { DrawCeilingCheck->SetIsChecked(DrawSettings->bAddCeiling ? ECheckBoxState::Checked : ECheckBoxState::Unchecked); }
    }
}

// ---------------------------------------------------------------------------


#undef LOCTEXT_NAMESPACE
