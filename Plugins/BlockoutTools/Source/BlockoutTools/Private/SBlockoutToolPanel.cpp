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
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "BlockoutToolPanel"

namespace
{
    /**
     * Retrouve la classe de personnage du PROJET COURANT, sans rien coder en dur.
     *
     * Ce plugin est concu pour etre depose dans n'importe quel projet : il ne peut donc
     * pas connaitre le chemin du Blueprint joueur (dans la version d'origine, interne a
     * un seul projet, ce chemin etait une constante). On interroge donc le projet
     * lui-meme, dans l'ordre du plus specifique au plus general :
     *   1. le GameMode defini dans les World Settings du niveau ouvert (le plus fiable :
     *      c'est celui qui s'appliquera reellement en jouant CE niveau) ;
     *   2. le GameMode par defaut du projet (Project Settings -> Maps & Modes) ;
     *   3. rien -- l'appelant retombe alors sur les valeurs par defaut du moteur.
     * Dans les deux premiers cas on lit `DefaultPawnClass` et on ne la retient que si
     * c'est bien un ACharacter (un simple APawn n'a ni capsule ni CharacterMovement,
     * donc aucune des metriques qui nous interessent).
     */
    UClass* ResolveProjectCharacterClass(FString& OutSource)
    {
        auto CharacterFromGameMode = [](UClass* GameModeClass) -> UClass*
        {
            if (!GameModeClass)
            {
                return nullptr;
            }
            if (AGameModeBase* GmCDO = Cast<AGameModeBase>(GameModeClass->GetDefaultObject()))
            {
                UClass* PawnClass = GmCDO->DefaultPawnClass;
                if (PawnClass && PawnClass->IsChildOf(ACharacter::StaticClass()))
                {
                    return PawnClass;
                }
            }
            return nullptr;
        };

        // 1. GameMode du niveau actuellement ouvert dans l'editeur.
        if (GEditor)
        {
            if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
            {
                if (AWorldSettings* Settings = EditorWorld->GetWorldSettings())
                {
                    if (UClass* Found = CharacterFromGameMode(Settings->DefaultGameMode))
                    {
                        OutSource = FString::Printf(TEXT("%s (GameMode du niveau)"), *Found->GetName());
                        return Found;
                    }
                }
            }
        }

        // 2. GameMode par defaut du projet (Project Settings -> Maps & Modes).
        const FString GameModePath = UGameMapsSettings::GetGlobalDefaultGameMode();
        if (!GameModePath.IsEmpty())
        {
            if (UClass* GmClass = StaticLoadClass(AGameModeBase::StaticClass(), nullptr, *GameModePath))
            {
                if (UClass* Found = CharacterFromGameMode(GmClass))
                {
                    OutSource = FString::Printf(TEXT("%s (GameMode du projet)"), *Found->GetName());
                    return Found;
                }
            }
        }

        return nullptr;
    }
}

// Definition UNIQUE de SBlockoutToolPanel::GetEAS() -- voir commentaire dans
// SBlockoutToolPanel.h. Ne pas redupliquer dans _Actions.cpp/_Swap.cpp/_Generators.cpp.
UEditorActorSubsystem* SBlockoutToolPanel::GetEAS()
{
    return GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------

SBlockoutToolPanel::~SBlockoutToolPanel()
{
    if (OverlayTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(OverlayTickerHandle);
    }
    SavePanelSettings();
}

// ---------------------------------------------------------------------------

float SBlockoutToolPanel::ParseFloat(const TSharedPtr<SEditableTextBox>& Box, float Default)
{
    if (!Box.IsValid())
    {
        return Default;
    }
    FString Text = Box->GetText().ToString();
    Text.ReplaceInline(TEXT(","), TEXT("."));
    Text.TrimStartAndEndInline();
    if (Text.IsEmpty())
    {
        return Default;
    }
    return FCString::Atof(*Text);
}

float SBlockoutToolPanel::ParseSpinFloat(const TSharedPtr<SSpinBox<float>>& Box, float Default)
{
    return Box.IsValid() ? Box->GetValue() : Default;
}

TSharedRef<SWidget> SBlockoutToolPanel::MakeField(const FText& Label, TSharedPtr<SEditableTextBox>& OutBox, const FString& DefaultValue,
    TFunction<FReply()> OnEnterGenerate)
{
    // Construit dans une variable locale (au lieu d'un return direct) pour que OutBox
    // soit deja valide au moment de l'enregistrer pour la persistance.
    TSharedRef<SWidget> Row = SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.f, 2.f, 6.f, 2.f)
        [
            SNew(SBox)
            // FIX UX (session 16, retour Thomas "les ecritures a cote des barres de
            // texte ne sont pas entierement lisibles") : la colonne de libelles etait
            // figee a 90 px SANS retour a la ligne, donc tout label plus long etait
            // coupe net et definitivement illisible ("FOV manuel (de..."). Elargie a
            // 135 px + AutoWrapText (le texte passe sur 2 lignes au lieu d'etre
            // tronque) + infobulle portant le libelle complet, ceinture et bretelles
            // meme si le panneau est docke tres etroit.
            .WidthOverride(135.f)
            [
                SNew(STextBlock)
                .Text(Label)
                .ToolTipText(Label)
                .AutoWrapText(true)
            ]
        ]
        + SHorizontalBox::Slot()
        .FillWidth(1.f)
        .Padding(0.f, 2.f)
        [
            // UX (session 21) : Entree dans ce champ = declenche directement "Generer" de
            // sa section, si un handler a ete fourni -- branche dans la chaine du builder
            // via .OnTextCommitted_Lambda (variante generee par SLATE_EVENT, meme idiome
            // que MakeSpinField/.OnClicked_Lambda ailleurs dans ce fichier). CORRIGE
            // (echec de compilation reel, 2026-07-30) : un premier essai appelait
            // OutBox->SetOnTextCommitted(...) en post-construction -- ce setter n'existe
            // pas sur SEditableTextBox (seul le builder expose OnTextCommitted via
            // SLATE_EVENT). La lambda ne fait rien si OnEnterGenerate n'a pas ete fourni
            // (capture par valeur d'un TFunction vide, verifiee avant appel).
            SAssignNew(OutBox, SEditableTextBox)
            .Text(FText::FromString(DefaultValue))
            .OnTextCommitted_Lambda([OnEnterGenerate](const FText&, ETextCommit::Type CommitType)
            {
                if (OnEnterGenerate && CommitType == ETextCommit::OnEnter)
                {
                    OnEnterGenerate();
                }
            })
        ];

    RegisterPersistedField(Label, OutBox);
    return Row;
}

TSharedRef<SWidget> SBlockoutToolPanel::MakeSpinField(const FText& Label, TSharedPtr<SSpinBox<float>>& OutBox, float DefaultValue,
    float Delta, TOptional<float> MinValue, TOptional<float> MaxValue, TFunction<FReply()> OnEnterGenerate)
{
    // Meme disposition (colonne de libelle 135px + AutoWrapText) que MakeField, juste
    // avec un SSpinBox<float> au lieu d'un SEditableTextBox -- voir la note sur
    // MakeSpinField dans le .h pour les champs a NE PAS convertir (semantique "vide =
    // auto/non fourni").
    // UX (session 21) : "Entree = Generer" branche directement dans la chaine du
    // builder via .OnValueCommitted_Lambda (variante generee par SLATE_EVENT, meme
    // idiome que .OnClicked_Lambda ailleurs dans ce fichier) -- plus sur qu'un setter
    // post-construction dont le nom exact n'est pas garanti d'exister sur ce widget.
    // La lambda ne fait rien si OnEnterGenerate n'a pas ete fourni (capture par valeur
    // d'un TFunction vide, verifiee avant appel).
    TSharedRef<SWidget> Row = SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.f, 2.f, 6.f, 2.f)
        [
            SNew(SBox)
            .WidthOverride(135.f)
            [
                SNew(STextBlock)
                .Text(Label)
                .ToolTipText(Label)
                .AutoWrapText(true)
            ]
        ]
        + SHorizontalBox::Slot()
        .FillWidth(1.f)
        .Padding(0.f, 2.f)
        [
            SAssignNew(OutBox, SSpinBox<float>)
            .Value(DefaultValue)
            .Delta(Delta)
            .MinValue(MinValue)
            .MaxValue(MaxValue)
            // Bornes de la MOLETTE/du GLISSER uniquement -- laisser la saisie clavier
            // directe (double-clic sur le champ) accepter n'importe quelle valeur si
            // Min/MaxValue ne sont pas fournis, meme comportement de liberte qu'un
            // champ texte non borne.
            .MinSliderValue(MinValue)
            .MaxSliderValue(MaxValue)
            .OnValueCommitted_Lambda([OnEnterGenerate](float, ETextCommit::Type CommitType)
            {
                if (OnEnterGenerate && CommitType == ETextCommit::OnEnter)
                {
                    OnEnterGenerate();
                }
            })
        ];

    RegisterPersistedSpinField(Label, OutBox);
    return Row;
}

void SBlockoutToolPanel::RegisterPersistedField(const FText& Label, const TSharedPtr<SEditableTextBox>& Box)
{
    if (!Box.IsValid())
    {
        return;
    }
    // Cle = libelle + numero d'occurrence. Les libelles seuls ne suffisent pas
    // ("Position X" existe dans presque toutes les sections) ; le compteur les
    // desambigue dans l'ordre de construction, qui est deterministe. Si la
    // disposition du panneau change un jour, le pire cas est qu'un champ ne
    // retrouve pas sa valeur -- jamais une valeur ecrite dans le mauvais champ
    // d'un type different, puisque tout est du texte libre.
    FString BaseKey = Label.ToString();
    BaseKey.ReplaceInline(TEXT(" "), TEXT("_"));
    BaseKey.ReplaceInline(TEXT("="), TEXT("_"));   // '=' casserait le format .ini

    int32 Occurrence = 0;
    for (const TPair<FString, TSharedPtr<SEditableTextBox>>& Existing : PersistedFields)
    {
        if (Existing.Key.StartsWith(BaseKey + TEXT("#")))
        {
            ++Occurrence;
        }
    }
    PersistedFields.Emplace(FString::Printf(TEXT("%s#%d"), *BaseKey, Occurrence), Box);
}

void SBlockoutToolPanel::RegisterPersistedSpinField(const FText& Label, const TSharedPtr<SSpinBox<float>>& Box)
{
    if (!Box.IsValid())
    {
        return;
    }
    // Meme mecanisme que RegisterPersistedField, prefixe "SPIN_" pour garantir
    // qu'une cle de ce tableau ne collisionne jamais avec celle d'un champ texte,
    // meme si un libelle venait un jour a coincider entre les deux tableaux.
    FString BaseKey = TEXT("SPIN_") + Label.ToString();
    BaseKey.ReplaceInline(TEXT(" "), TEXT("_"));
    BaseKey.ReplaceInline(TEXT("="), TEXT("_"));

    int32 Occurrence = 0;
    for (const TPair<FString, TSharedPtr<SSpinBox<float>>>& Existing : PersistedSpinFields)
    {
        if (Existing.Key.StartsWith(BaseKey + TEXT("#")))
        {
            ++Occurrence;
        }
    }
    PersistedSpinFields.Emplace(FString::Printf(TEXT("%s#%d"), *BaseKey, Occurrence), Box);
}

namespace
{
    const TCHAR* BlockoutSettingsSection = TEXT("BlockoutToolPanel");
}

void SBlockoutToolPanel::SavePanelSettings() const
{
    if (!GConfig)
    {
        return;
    }
    for (const TPair<FString, TSharedPtr<SEditableTextBox>>& Field : PersistedFields)
    {
        if (Field.Value.IsValid())
        {
            GConfig->SetString(BlockoutSettingsSection, *Field.Key,
                *Field.Value->GetText().ToString(), GEditorPerProjectIni);
        }
    }
    for (const TPair<FString, TSharedPtr<SSpinBox<float>>>& Field : PersistedSpinFields)
    {
        if (Field.Value.IsValid())
        {
            GConfig->SetFloat(BlockoutSettingsSection, *Field.Key, Field.Value->GetValue(), GEditorPerProjectIni);
        }
    }
    // Les 2 selecteurs d'asset ne sont pas des SEditableTextBox : sauves a part.
    GConfig->SetString(BlockoutSettingsSection, TEXT("SwapMeshPath"), *SwapMeshPath, GEditorPerProjectIni);
    GConfig->SetString(BlockoutSettingsSection, TEXT("PlanTexturePath"), *PlanTexturePath, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

void SBlockoutToolPanel::LoadPanelSettings()
{
    if (!GConfig)
    {
        return;
    }
    for (TPair<FString, TSharedPtr<SEditableTextBox>>& Field : PersistedFields)
    {
        FString Value;
        if (Field.Value.IsValid()
            && GConfig->GetString(BlockoutSettingsSection, *Field.Key, Value, GEditorPerProjectIni))
        {
            Field.Value->SetText(FText::FromString(Value));
        }
    }
    for (TPair<FString, TSharedPtr<SSpinBox<float>>>& Field : PersistedSpinFields)
    {
        float Value = 0.f;
        if (Field.Value.IsValid()
            && GConfig->GetFloat(BlockoutSettingsSection, *Field.Key, Value, GEditorPerProjectIni))
        {
            Field.Value->SetValue(Value);
        }
    }
    GConfig->GetString(BlockoutSettingsSection, TEXT("SwapMeshPath"), SwapMeshPath, GEditorPerProjectIni);
    GConfig->GetString(BlockoutSettingsSection, TEXT("PlanTexturePath"), PlanTexturePath, GEditorPerProjectIni);
}

TSharedRef<SWidget> SBlockoutToolPanel::MakeCheckboxField(const FText& Label, TSharedPtr<SCheckBox>& OutBox)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.f, 2.f, 6.f, 2.f)
        [
            SAssignNew(OutBox, SCheckBox)
        ]
        + SHorizontalBox::Slot()
        .FillWidth(1.f)
        .VAlign(VAlign_Center)
        .Padding(0.f, 2.f)
        [
            // Meme correctif que MakeField : les libelles de cases a cocher sont longs
            // ("Afficher la verification de pente (en direct)") et etaient tronques.
            SNew(STextBlock)
            .Text(Label)
            .ToolTipText(Label)
            .AutoWrapText(true)
        ];
}

TSharedRef<SWidget> SBlockoutToolPanel::MakeUseCameraButton(FOnClicked OnClicked)
{
    return SNew(SButton)
        .HAlign(HAlign_Center)
        .ToolTipText(LOCTEXT("UseCameraTip", "Remplit Position X/Y/Z avec la position actuelle de la camera du viewport"))
        .OnClicked(OnClicked)
        [
            SNew(STextBlock).Text(LOCTEXT("UseCamera", "📍 Utiliser ma position actuelle"))
        ];
}

TSharedRef<SWidget> SBlockoutToolPanel::MakeSection(TAttribute<FText> Title, TSharedRef<SWidget> Content)
{
    // HeaderContent (plutot que l'argument AreaTitle) : permet un titre LIE a un
    // TAttribute, indispensable pour l'indicateur "actif" des overlays.
    return SNew(SExpandableArea)
        .InitiallyCollapsed(true)
        .Padding(FMargin(4.f, 2.f, 4.f, 8.f))
        .HeaderContent()
        [
            SNew(STextBlock)
            .Text(Title)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        ]
        .BodyContent()
        [
            Content
        ];
}

// Les 3 overlays temps reel peuvent rester actifs alors que leur section est repliee.
// Sans indicateur dans le titre, on aurait un overlay qui tourne (et consomme) sans
// aucun repere visible dans le panneau -- piege introduit par les sections repliables
// lui-meme, donc corrige ici plutot que subi.
FText SBlockoutToolPanel::GetMetricsSectionTitle() const
{
    const bool bActive = OverlayEnabledCheck.IsValid() && OverlayEnabledCheck->IsChecked();
    return bActive
        ? LOCTEXT("MetricsSectionOn",  "Métriques Gameplay (temps réel)   ● actif")
        : LOCTEXT("MetricsSectionOff", "Métriques Gameplay (temps réel)");
}

FText SBlockoutToolPanel::GetFovSectionTitle() const
{
    const bool bActive = FovShowFrustumCheck.IsValid() && FovShowFrustumCheck->IsChecked();
    return bActive
        ? LOCTEXT("FovSectionOn",  "Simulateur FOV / Frustum joueur   ● actif")
        : LOCTEXT("FovSectionOff", "Simulateur FOV / Frustum joueur");
}

FText SBlockoutToolPanel::GetSlopeSectionTitle() const
{
    const bool bActive = SlopeShowCheck.IsValid() && SlopeShowCheck->IsChecked();
    return bActive
        ? LOCTEXT("SlopeSectionOn",  "Vérificateur de pente automatique   ● actif")
        : LOCTEXT("SlopeSectionOff", "Vérificateur de pente automatique");
}

bool SBlockoutToolPanel::GetActiveViewportCameraLocation(FVector& OutLocation)
{
    // GCurrentLevelEditingViewportClient : pointeur global vers le dernier viewport
    // de niveau utilise par l'utilisateur (module LevelEditor).
    if (GCurrentLevelEditingViewportClient)
    {
        OutLocation = GCurrentLevelEditingViewportClient->GetViewLocation();
        return true;
    }
    return false;
}

bool SBlockoutToolPanel::GetActiveViewportCameraTransform(FVector& OutLocation, FRotator& OutRotation)
{
    if (GCurrentLevelEditingViewportClient)
    {
        OutLocation = GCurrentLevelEditingViewportClient->GetViewLocation();
        OutRotation = GCurrentLevelEditingViewportClient->GetViewRotation();
        return true;
    }
    return false;
}

void SBlockoutToolPanel::FillPositionFromCamera(const TSharedPtr<SEditableTextBox>& XBox, const TSharedPtr<SEditableTextBox>& YBox, const TSharedPtr<SEditableTextBox>& ZBox)
{
    // Remplit une fois les champs avec la position actuelle -- ce n'est PAS un
    // suivi permanent : tant que le champ n'est pas re-modifie (par ce bouton ou
    // a la main), "Generer" continuera a utiliser exactement cette valeur figee.
    FVector Loc;
    if (!GetActiveViewportCameraLocation(Loc))
    {
        SetStatus(TEXT("Aucun viewport perspective actif trouve."), true);
        return;
    }
    if (XBox.IsValid()) XBox->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), Loc.X)));
    if (YBox.IsValid()) YBox->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), Loc.Y)));
    if (ZBox.IsValid()) ZBox->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), Loc.Z)));
    SetStatus(FString::Printf(TEXT("Position remplie depuis la camera : (%.0f, %.0f, %.0f)"), Loc.X, Loc.Y, Loc.Z), false);
}

void SBlockoutToolPanel::FillPositionFromCameraSpin(const TSharedPtr<SSpinBox<float>>& XBox, const TSharedPtr<SSpinBox<float>>& YBox, const TSharedPtr<SSpinBox<float>>& ZBox)
{
    // Equivalent de FillPositionFromCamera pour des champs SSpinBox<float> (session 21) --
    // SetValue() direct, pas de formatage texte necessaire.
    FVector Loc;
    if (!GetActiveViewportCameraLocation(Loc))
    {
        SetStatus(TEXT("Aucun viewport perspective actif trouve."), true);
        return;
    }
    if (XBox.IsValid()) XBox->SetValue(Loc.X);
    if (YBox.IsValid()) YBox->SetValue(Loc.Y);
    if (ZBox.IsValid()) ZBox->SetValue(Loc.Z);
    SetStatus(FString::Printf(TEXT("Position remplie depuis la camera : (%.0f, %.0f, %.0f)"), Loc.X, Loc.Y, Loc.Z), false);
}

// ---------------------------------------------------------------------------
// Escalier / Rampe -- metriques joueur + spawn de cube oriente
// ---------------------------------------------------------------------------

void SBlockoutToolPanel::GetPlayerStepMetrics(float& OutCapsuleRadius, float& OutMaxStepHeight, float& OutWalkableFloorAngleDeg, FString& OutSource)
{
    // Repli par defaut : memes constantes que Content/Python/stairs_ramps.py
    // (DEFAULT_CAPSULE_RADIUS/DEFAULT_MAX_STEP_HEIGHT/DEFAULT_WALKABLE_FLOOR_ANGLE) --
    // valeurs standard du Mannequin UE5, ne bloque jamais l'outil.
    OutCapsuleRadius = 34.f;
    OutMaxStepHeight = 45.f;
    OutWalkableFloorAngleDeg = 44.76f;
    OutSource = TEXT("defauts moteur");

    UWorld* World = GetDrawTargetWorld();

    // 1. Joueur reellement en jeu (PIE) -- memes valeurs runtime que ComputeMaxJumpHeight.
    if (World && World->IsGameWorld())
    {
        if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(World, 0))
        {
            if (UCapsuleComponent* Capsule = PlayerChar->GetCapsuleComponent())
            {
                OutCapsuleRadius = Capsule->GetScaledCapsuleRadius();
            }
            if (UCharacterMovementComponent* MC = PlayerChar->GetCharacterMovement())
            {
                OutMaxStepHeight = MC->MaxStepHeight;
                OutWalkableFloorAngleDeg = MC->GetWalkableFloorAngle();
            }
            OutSource = TEXT("PIE (joueur actif)");
            return;
        }
    }

    // 2. Repli : CDO du personnage du projet (valeurs par defaut, hors PIE).
    FString ClassSource;
    UClass* PlayerClass = ResolveProjectCharacterClass(ClassSource);
    if (PlayerClass)
    {
        if (ACharacter* CDO = Cast<ACharacter>(PlayerClass->GetDefaultObject()))
        {
            if (UCapsuleComponent* Capsule = CDO->GetCapsuleComponent())
            {
                OutCapsuleRadius = Capsule->GetScaledCapsuleRadius();
            }
            if (UCharacterMovementComponent* MC = CDO->GetCharacterMovement())
            {
                OutMaxStepHeight = MC->MaxStepHeight;
                OutWalkableFloorAngleDeg = MC->GetWalkableFloorAngle();
            }
            OutSource = ClassSource;
        }
    }
}

float SBlockoutToolPanel::GetPlayerCameraFOV(FString& OutSource)
{
    // Meme repli que GetPlayerStepMetrics/ComputeMaxJumpHeight : joueur PIE actif en
    // priorite, sinon CDO du personnage du projet, sinon defaut moteur -- jamais bloquant.
    UWorld* World = GetDrawTargetWorld();

    if (World && World->IsGameWorld())
    {
        if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(World, 0))
        {
            if (UCameraComponent* Cam = PlayerChar->FindComponentByClass<UCameraComponent>())
            {
                OutSource = TEXT("PIE (joueur actif)");
                return Cam->FieldOfView;
            }
        }
    }

    FString ClassSource;
    UClass* PlayerClass = ResolveProjectCharacterClass(ClassSource);
    if (PlayerClass)
    {
        if (ACharacter* CDO = Cast<ACharacter>(PlayerClass->GetDefaultObject()))
        {
            if (UCameraComponent* Cam = CDO->FindComponentByClass<UCameraComponent>())
            {
                OutSource = ClassSource;
                return Cam->FieldOfView;
            }
        }
    }

    OutSource = TEXT("défaut 90° (caméra introuvable)");
    return 90.f;
}

float SBlockoutToolPanel::GetPlayerCapsuleFullHeight(FString& OutSource)
{
    // Meme repli que GetPlayerStepMetrics/GetPlayerCameraFOV : PIE -> CDO -> defaut.
    // 88 * 2 = 176 UU : capsule standard du Mannequin UE5.
    UWorld* World = GetDrawTargetWorld();

    if (World && World->IsGameWorld())
    {
        if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(World, 0))
        {
            if (UCapsuleComponent* Capsule = PlayerChar->GetCapsuleComponent())
            {
                OutSource = TEXT("PIE (joueur actif)");
                return Capsule->GetScaledCapsuleHalfHeight() * 2.f;
            }
        }
    }

    FString ClassSource;
    UClass* PlayerClass = ResolveProjectCharacterClass(ClassSource);
    if (PlayerClass)
    {
        if (ACharacter* CDO = Cast<ACharacter>(PlayerClass->GetDefaultObject()))
        {
            if (UCapsuleComponent* Capsule = CDO->GetCapsuleComponent())
            {
                OutSource = ClassSource;
                return Capsule->GetScaledCapsuleHalfHeight() * 2.f;
            }
        }
    }

    OutSource = TEXT("défaut 176 UU");
    return 176.f;
}

AActor* SBlockoutToolPanel::SpawnRotatedCube(FVector Location, FRotator Rotation, FVector SizeXYZ, const FString& Label)
{
    // Toujours le monde EDITEUR (jamais PIE, contrairement a GetDrawTargetWorld) --
    // on genere de la geometrie de blockout permanente, pas une preview temps reel.
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return nullptr;
    }

    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!CubeMesh)
    {
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
    if (!Actor)
    {
        return nullptr;
    }

    if (UStaticMeshComponent* SMC = Actor->GetStaticMeshComponent())
    {
        SMC->SetStaticMesh(CubeMesh);
        SMC->SetMobility(EComponentMobility::Static);
    }

    // Cube moteur /Engine/BasicShapes/Cube = 100x100x100 UU a scale (1,1,1) --
    // meme convention deja documentee dans CLAUDE.md pour ce projet
    // (UBlockoutGeometrySubsystem::SpawnScaledCube utilise la meme regle).
    constexpr float EngineCubeSize = 100.f;
    Actor->SetActorScale3D(SizeXYZ / EngineCubeSize);
    Actor->SetActorLabel(Label);

    return Actor;
}

// ---------------------------------------------------------------------------
// Overlay metriques gameplay
// ---------------------------------------------------------------------------

UWorld* SBlockoutToolPanel::GetDrawTargetWorld()
{
    if (!GEditor)
    {
        return nullptr;
    }
    // Priorite a une session PIE active (valeurs et positions reellement en jeu) ;
    // sinon on retombe sur le monde editeur pour une previsualisation statique.
    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
    {
        if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
        {
            return Ctx.World();
        }
    }
    return GEditor->GetEditorWorldContext().World();
}

bool SBlockoutToolPanel::GetFloatProperty(UObject* Obj, const TCHAR* PropName, float& OutValue)
{
    if (!Obj)
    {
        return false;
    }
    if (FFloatProperty* FloatProp = FindFProperty<FFloatProperty>(Obj->GetClass(), PropName))
    {
        OutValue = FloatProp->GetPropertyValue_InContainer(Obj);
        return true;
    }
    return false;
}

float SBlockoutToolPanel::JumpHeightFromMovement(UCharacterMovementComponent* MC)
{
    if (!MC)
    {
        return 0.f;
    }
    const float V = MC->JumpZVelocity;
    const float G = FMath::Abs(MC->GetGravityZ());
    return (G > KINDA_SMALL_NUMBER) ? (V * V) / (2.f * G) : 0.f;
}

float SBlockoutToolPanel::ComputeMaxJumpHeight(FString& OutSource)
{
    UWorld* World = GetDrawTargetWorld();

    // 1. Joueur reellement en jeu (PIE) -- reflete les valeurs runtime exactes.
    if (World && World->IsGameWorld())
    {
        if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(World, 0))
        {
            if (UCharacterMovementComponent* MC = PlayerChar->GetCharacterMovement())
            {
                OutSource = TEXT("PIE (joueur actif)");
                return JumpHeightFromMovement(MC);
            }
        }
    }

    // 2. Repli : CDO du personnage du projet (valeurs par defaut, hors PIE).
    FString ClassSource;
    UClass* PlayerClass = ResolveProjectCharacterClass(ClassSource);
    if (PlayerClass)
    {
        if (ACharacter* CDO = Cast<ACharacter>(PlayerClass->GetDefaultObject()))
        {
            if (UCharacterMovementComponent* MC = CDO->GetCharacterMovement())
            {
                OutSource = ClassSource;
                return JumpHeightFromMovement(MC);
            }
        }
    }

    OutSource = TEXT("joueur introuvable");
    return 0.f;
}


// ---------------------------------------------------------------------------

void SBlockoutToolPanel::SetStatus(const FString& Msg, bool bError)
{
    if (!StatusLabel.IsValid())
    {
        return;
    }
    StatusLabel->SetText(FText::FromString(Msg));
    StatusLabel->SetColorAndOpacity(bError
        ? FSlateColor(FLinearColor(1.f, 0.35f, 0.35f))
        : FSlateColor(FLinearColor(0.4f, 0.9f, 0.4f)));
}

void SBlockoutToolPanel::PushHistory(const FString& Name, const TArray<AActor*>& Actors)
{
    // Rangement automatique dans l'Outliner (session 19) : jusqu'ici tout ce que le
    // panneau generait atterrissait a la racine, illisible au bout de quelques heures
    // de travail reel. Chaque generation va desormais dans son propre sous-dossier
    // Blockout/<Nom> -- SetFolderPath fait apparaitre le dossier tout seul dans
    // l'Outliner, aucune creation explicite necessaire. Ne touche PAS un acteur qui a
    // deja un dossier explicitement assigne avant cet appel (cas de la Duplication en
    // serie, qui range volontairement ses copies dans le MEME dossier que l'acteur
    // source plutot que de toutes les regrouper sous "Blockout/Duplication").
    const FName Folder = *FString::Printf(TEXT("Blockout/%s"), *Name);
    for (AActor* A : Actors)
    {
        if (A && A->GetFolderPath().IsNone())
        {
            A->SetFolderPath(Folder);
        }
    }

    FBlockoutGenerationEntry Entry;
    Entry.Name = Name;
    Entry.Actors.Reserve(Actors.Num());
    for (AActor* A : Actors)
    {
        if (A)
        {
            Entry.Actors.Add(TWeakObjectPtr<AActor>(A));
        }
    }
    GenerationHistory.Add(MoveTemp(Entry));
    RefreshLastGenLabel();
}

void SBlockoutToolPanel::RefreshLastGenLabel()
{
    if (!LastGenLabel.IsValid())
    {
        return;
    }
    if (GenerationHistory.Num() == 0)
    {
        LastGenLabel->SetText(LOCTEXT("NoLastGen", "(rien de généré pour l'instant)"));
        return;
    }

    // UX (session 21) : "Annuler" fait deja GenerationHistory.Pop() -- cliquer plusieurs
    // fois remonte donc DEJA plusieurs generations en arriere (comportement deja present,
    // pas ajoute cette session). Ce qui manquait : la VISIBILITE -- ce label n'affichait
    // que la toute derniere entree, sans aucun moyen de voir ce qu'un 2e ou 3e clic sur
    // "Annuler" est sur le point de supprimer. Affiche desormais les N dernieres entrees,
    // la plus recente en premier (celle que "Annuler" retire au PROCHAIN clic).
    constexpr int32 MaxShown = 5;
    const int32 Count = GenerationHistory.Num();
    const int32 Shown = FMath::Min(Count, MaxShown);

    FString Text = FString::Printf(TEXT("Historique (%d) — \"Annuler\" retire le PREMIER de cette liste :\n"), Count);
    for (int32 i = 0; i < Shown; ++i)
    {
        const FBlockoutGenerationEntry& Entry = GenerationHistory[Count - 1 - i];
        Text += FString::Printf(TEXT("  %d. %s (%d acteur(s))\n"), i + 1, *Entry.Name, Entry.Actors.Num());
    }
    if (Count > MaxShown)
    {
        Text += FString::Printf(TEXT("  … et %d de plus\n"), Count - MaxShown);
    }
    Text.RemoveFromEnd(TEXT("\n"));

    LastGenLabel->SetText(FText::FromString(Text));
}

TArray<AActor*> SBlockoutToolPanel::SnapshotAllActors() const
{
    UEditorActorSubsystem* EAS = GetEAS();
    return EAS ? EAS->GetAllLevelActors() : TArray<AActor*>();
}

TArray<AActor*> SBlockoutToolPanel::DiffNewActors(const TArray<AActor*>& Before) const
{
    TSet<AActor*> BeforeSet(Before);
    TArray<AActor*> NewActors;
    for (AActor* A : SnapshotAllActors())
    {
        if (A && !BeforeSet.Contains(A))
        {
            NewActors.Add(A);
        }
    }
    return NewActors;
}

// ---------------------------------------------------------------------------


#undef LOCTEXT_NAMESPACE
