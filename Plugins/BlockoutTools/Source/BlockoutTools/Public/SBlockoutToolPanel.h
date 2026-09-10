#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/SlateDelegates.h"
#include "UObject/WeakObjectPtr.h"
#include "Containers/Ticker.h"
#include "Templates/Function.h"

class SEditableTextBox;
class SCheckBox;
class STextBlock;
class SWidget;
class AActor;
class UCharacterMovementComponent;
class UEditorActorSubsystem;
struct FAssetData;
template <typename NumericType> class SSpinBox;

/**
 * Une generation = un clic sur un des 3 boutons "Generer...". On garde une
 * reference DIRECTE aux acteurs crees (pas une recherche par nom) pour
 * qu'Annuler/Selectionner ne touchent jamais que CE clic precis, meme si
 * plusieurs generations partagent le meme nom (ex: "Salle_1" genere
 * plusieurs fois sans changer le champ Nom).
 */
struct FBlockoutGenerationEntry
{
    FString Name;
    TArray<TWeakObjectPtr<AActor>> Actors;
};

/**
 * Un point deja calcule par le Verificateur de pente (session 11, optimisation) --
 * mis en cache pour pouvoir REDESSINER sans refaire la sonde physique tant que la
 * camera n'a pas assez bouge (la sonde/le trace est la partie couteuse, le dessin
 * du quad colore ne l'est pas).
 */
struct FSlopeSample
{
    FVector DrawCenter = FVector::ZeroVector;
    FVector Normal = FVector::UpVector;
    FColor Color = FColor::Green;
};

/**
 * Panneau no-code pour le level designer : genere des salles/couloirs/dalles
 * de blockout via UBlockoutGeometrySubsystem, avec un historique permettant
 * d'annuler ou de re-selectionner la derniere generation, et un overlay
 * temps reel montrant des metriques de gameplay (hauteur de saut max,
 * portee des ennemis) pour valider le level design.
 * Docke a cote du viewport (Tools -> Outil Blockout), utilisable pendant
 * que le level designer travaille dans l'editeur -- aucun script/Blueprint
 * a toucher.
 */
class SBlockoutToolPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SBlockoutToolPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SBlockoutToolPanel();

private:
    // Raccourci vers l'EditorActorSubsystem, utilise par plusieurs des .cpp qui
    // implementent cette classe (fichier eclate en 7 .cpp, session 22 -- voir
    // CLAUDE.md). Definition UNIQUE dans SBlockoutToolPanel.cpp : avant le
    // 2026-08-10, chaque .cpp avait sa propre copie dans un namespace anonyme,
    // ce qui compilait tant que chaque fichier restait sa propre unite de
    // traduction mais cassait des qu'Unreal Build Accelerator regroupait
    // plusieurs de ces .cpp dans un seul blob Unity (Module.BlockoutTools.cpp)
    // -- erreur C2084 "a deja un corps", jamais detectee avant un vrai rebuild
    // complet. Ne pas re-dupliquer cette fonction dans un .cpp individuel.
    static UEditorActorSubsystem* GetEAS();

    // Salle
    // UX (session 21) : champs NUMERIQUES passes en SSpinBox<float> (molette/glisser,
    // saisie invalide impossible) -- dette UX identifiee des la session 14, jamais
    // traitee jusqu'ici. Seuls Salle/Couloir/Dalle sont convertis cette session (~20
    // champs, voir GAME_MEMORY.md pour la portee et la liste de ce qui reste a faire) --
    // RoomNameBox reste un SEditableTextBox (texte libre, pas numerique).
    TSharedPtr<SEditableTextBox> RoomNameBox;
    TSharedPtr<SSpinBox<float>>  RoomPosXBox;
    TSharedPtr<SSpinBox<float>>  RoomPosYBox;
    TSharedPtr<SSpinBox<float>>  RoomPosZBox;
    TSharedPtr<SSpinBox<float>>  RoomSizeXBox;
    TSharedPtr<SSpinBox<float>>  RoomSizeYBox;
    TSharedPtr<SSpinBox<float>>  RoomHeightBox;
    TSharedPtr<SCheckBox>        RoomNoCeilingCheck;

    // Couloir
    TSharedPtr<SEditableTextBox> CorrNameBox;
    TSharedPtr<SSpinBox<float>>  CorrStartXBox;
    TSharedPtr<SSpinBox<float>>  CorrStartYBox;
    TSharedPtr<SSpinBox<float>>  CorrEndXBox;
    TSharedPtr<SSpinBox<float>>  CorrEndYBox;
    TSharedPtr<SSpinBox<float>>  CorrPosZBox;
    TSharedPtr<SSpinBox<float>>  CorrWidthBox;
    TSharedPtr<SSpinBox<float>>  CorrHeightBox;

    // Dessin libre (mode d'edition viewport UBlockoutDrawMode, portage de l'outil
    // Unity LevelDesignTools). Les VALEURS vivent dans UBlockoutDrawSettings (partagees
    // avec le mode) ; ces champs ne sont que la saisie, recopiee a l'activation.
    TSharedPtr<SEditableTextBox> DrawNameBox;
    TSharedPtr<SSpinBox<float>>  DrawGroundZBox;
    TSharedPtr<SSpinBox<float>>  DrawHeightBox;
    TSharedPtr<SSpinBox<float>>  DrawThicknessBox;
    TSharedPtr<SCheckBox>        DrawFloorCheck;
    TSharedPtr<SCheckBox>        DrawCeilingCheck;

    // Dalle / plancher intermediaire (mezzanine, grenier...)
    TSharedPtr<SEditableTextBox> SlabNameBox;
    TSharedPtr<SSpinBox<float>>  SlabPosXBox;
    TSharedPtr<SSpinBox<float>>  SlabPosYBox;
    TSharedPtr<SSpinBox<float>>  SlabPosZBox;
    TSharedPtr<SSpinBox<float>>  SlabSizeXBox;
    TSharedPtr<SSpinBox<float>>  SlabSizeYBox;
    TSharedPtr<SSpinBox<float>>  SlabThicknessBox;

    // Escalier
    TSharedPtr<SEditableTextBox> StairNameBox;
    TSharedPtr<SEditableTextBox> StairPosXBox;
    TSharedPtr<SEditableTextBox> StairPosYBox;
    TSharedPtr<SEditableTextBox> StairPosZBox;
    TSharedPtr<SEditableTextBox> StairYawBox;
    TSharedPtr<SEditableTextBox> StairHeightBox;
    TSharedPtr<SEditableTextBox> StairWidthBox;

    // Rampe
    TSharedPtr<SEditableTextBox> RampNameBox;
    TSharedPtr<SEditableTextBox> RampPosXBox;
    TSharedPtr<SEditableTextBox> RampPosYBox;
    TSharedPtr<SEditableTextBox> RampPosZBox;
    TSharedPtr<SEditableTextBox> RampYawBox;
    TSharedPtr<SEditableTextBox> RampHeightBox;
    TSharedPtr<SEditableTextBox> RampWidthBox;
    TSharedPtr<SEditableTextBox> RampRunBox;
    TSharedPtr<SEditableTextBox> RampAngleBox;

    // Cone de vision IA -- secteur angulaire plat (eventail de segments), marqueur
    // de blockout pour visualiser/poser le champ de vision d'un ennemi dans le niveau.
    TSharedPtr<SEditableTextBox> ConeNameBox;
    TSharedPtr<SEditableTextBox> ConePosXBox;
    TSharedPtr<SEditableTextBox> ConePosYBox;
    TSharedPtr<SEditableTextBox> ConePosZBox;
    TSharedPtr<SEditableTextBox> ConeYawBox;
    TSharedPtr<SEditableTextBox> ConeRadiusBox;
    TSharedPtr<SEditableTextBox> ConeAngleBox;
    TSharedPtr<SEditableTextBox> ConeHeightBox;
    TSharedPtr<SEditableTextBox> ConeSegmentsBox;

    // Arche de pont -- 2 piliers + voussoirs suivant une demi-ellipse + tablier (deck).
    TSharedPtr<SEditableTextBox> ArchNameBox;
    TSharedPtr<SEditableTextBox> ArchPosXBox;
    TSharedPtr<SEditableTextBox> ArchPosYBox;
    TSharedPtr<SEditableTextBox> ArchPosZBox;
    TSharedPtr<SEditableTextBox> ArchYawBox;
    TSharedPtr<SEditableTextBox> ArchSpanBox;
    TSharedPtr<SEditableTextBox> ArchRiseBox;
    TSharedPtr<SEditableTextBox> ArchThicknessBox;
    TSharedPtr<SEditableTextBox> ArchWidthBox;
    TSharedPtr<SEditableTextBox> ArchPierHeightBox;
    TSharedPtr<SEditableTextBox> ArchDeckThicknessBox;
    TSharedPtr<SEditableTextBox> ArchSegmentsBox;

    // Tunnel courbe -- suite de couloirs droits (GenerateCorridor) chaines le long
    // d'un arc de cercle, meme mecanisme que le Couloir existant, juste en boucle.
    TSharedPtr<SEditableTextBox> TunnelNameBox;
    TSharedPtr<SEditableTextBox> TunnelPosXBox;
    TSharedPtr<SEditableTextBox> TunnelPosYBox;
    TSharedPtr<SEditableTextBox> TunnelPosZBox;
    TSharedPtr<SEditableTextBox> TunnelYawBox;
    TSharedPtr<SEditableTextBox> TunnelRadiusBox;
    TSharedPtr<SEditableTextBox> TunnelAngleBox;
    TSharedPtr<SEditableTextBox> TunnelWidthBox;
    TSharedPtr<SEditableTextBox> TunnelHeightBox;
    TSharedPtr<SEditableTextBox> TunnelSegmentsBox;

    // Swap intelligent -- remplace toutes les boites de blockout dont le NOM (Label)
    // contient l'identifiant saisi par un mesh d'art final, en conservant
    // position/rotation, tags, nom et dossier de l'Outliner. Le mesh garde sa taille
    // NATIVE (scale 1) : un kit d'art modulaire est concu a la bonne taille et ne doit
    // pas etre etire (choix confirme par Thomas session 12).
    TSharedPtr<SEditableTextBox> SwapIdentifierBox;
    TSharedPtr<STextBlock>       SwapInfoLabel;
    FString SwapMeshPath;   // asset choisi via le selecteur (SObjectPropertyEntryBox)

    // Generateur depuis un plan 2D -- lit une image (Texture2D importee OU fichier PNG
    // sur disque, les deux supportes) et erige des murs la ou l'image est sombre.
    // Les pixels sombres sont regroupes en grands rectangles (fusion gloutonne) pour
    // produire quelques dizaines d'acteurs au lieu de plusieurs milliers.
    TSharedPtr<SEditableTextBox> PlanNameBox;
    FString PlanTexturePath;                       // asset choisi via le selecteur
    TSharedPtr<SEditableTextBox> PlanFilePathBox;  // ou chemin d'un .png sur disque
    TSharedPtr<SEditableTextBox> PlanPosXBox;
    TSharedPtr<SEditableTextBox> PlanPosYBox;
    TSharedPtr<SEditableTextBox> PlanPosZBox;
    TSharedPtr<SEditableTextBox> PlanScaleBox;        // UU par pixel
    TSharedPtr<SEditableTextBox> PlanWallHeightBox;
    TSharedPtr<SEditableTextBox> PlanCellPixelsBox;   // resolution d'echantillonnage
    TSharedPtr<SEditableTextBox> PlanThresholdBox;    // seuil de noir (0-255)
    TSharedPtr<SCheckBox>        PlanFloorCheck;
    TSharedPtr<STextBlock>       PlanInfoLabel;

    TSharedPtr<STextBlock> StatusLabel;
    TSharedPtr<STextBlock> LastGenLabel;

    // Overlay metriques gameplay (temps reel)
    TSharedPtr<SCheckBox>  OverlayEnabledCheck;
    TSharedPtr<STextBlock> MetricsInfoLabel;
    // Tag identifiant les ennemis dans le projet HOTE (defaut "Enemy"). Saisissable :
    // ce plugin est generique et ne connait pas les conventions de nommage du projet
    // dans lequel il est installe.
    TSharedPtr<SEditableTextBox> MetricsEnemyTagBox;
    FTSTicker::FDelegateHandle OverlayTickerHandle;
    // Throttle (session 11) : GetAllActorsWithTag() + plusieurs DrawDebug par ennemi --
    // deja peu couteux, mais throttle par coherence/marge de securite avec les 2 autres
    // overlays plutot que de tourner a 60 fps sans limite.
    float GameplayOverlayAccumTime = 0.f;

    // Simulateur FOV / Frustum joueur -- deplace la camera du viewport editeur a
    // une position/rotation donnee (avec le vrai FOV du joueur), et/ou dessine le
    // frustum en lignes de debug persistantes (meme ticker que l'overlay metriques).
    TSharedPtr<SEditableTextBox> FovPosXBox;
    TSharedPtr<SEditableTextBox> FovPosYBox;
    TSharedPtr<SEditableTextBox> FovPosZBox;
    TSharedPtr<SEditableTextBox> FovYawBox;
    TSharedPtr<SEditableTextBox> FovPitchBox;
    TSharedPtr<SEditableTextBox> FovOverrideBox;
    TSharedPtr<SEditableTextBox> FovDistanceBox;
    TSharedPtr<SEditableTextBox> FovAspectBox;
    TSharedPtr<SCheckBox>        FovShowFrustumCheck;
    TSharedPtr<STextBlock>       FovInfoLabel;
    // Throttle (session 11) : deja tres peu couteux (9 DrawDebug, aucune trace physique)
    // mais throttle par coherence avec les 2 autres overlays.
    float FovOverlayAccumTime = 0.f;

    // Verificateur de pente automatique -- grille de traces verticales, colore chaque
    // point touche selon l'angle de pente (Vert = marchable par le vrai joueur, Orange
    // = jusqu'au seuil "vehicule" configurable, Rouge = au-dela). PLUS AUCUN champ de
    // zone (retour Thomas session 8, "je veux seulement cocher la case") -- la grille
    // suit automatiquement la camera du viewport editeur.
    // Seuil VERT -> ORANGE. Laisse VIDE = utilise l'angle marchable reel du personnage.
    // Ajoute en session 17 : tant qu'il n'etait pas reglable, aucune valeur saisie par
    // l'utilisateur ne pouvait faire disparaitre le vert (voir GAME_MEMORY session 17).
    TSharedPtr<SEditableTextBox> SlopeWalkableAngleBox;
    TSharedPtr<SCheckBox>        SlopeShowCheck;
    TSharedPtr<STextBlock>       SlopeInfoLabel;
    // Throttle (session 10) : ce overlay dessine plusieurs centaines de quads
    // translucides -- ne PAS le redessiner a chaque frame (60 fps), seulement toutes
    // les SlopeOverlayInterval secondes, pour reduire la charge GPU soutenue.
    float SlopeOverlayAccumTime = 0.f;
    // Optimisation (session 11) : la PARTIE COUTEUSE, c'est la sonde physique (441
    // SweepSingleByObjectType par recalcul), pas le dessin du quad -- si la camera n'a
    // pas assez bouge depuis le dernier recalcul, on saute la sonde entierement et on se
    // contente de redessiner les resultats deja en cache (quasi gratuit).
    TArray<FSlopeSample> SlopeCachedSamples;
    FVector SlopePrevCameraLoc = FVector::ZeroVector;
    bool bSlopePrevLocValid = false;
    // FIX session 16 : le camera-gating ci-dessus rendait le champ "Angle max vehicule"
    // SANS EFFET tant que la camera ne bougeait pas (le seuil n'etait relu que dans la
    // branche de recalcul). On memorise donc la derniere valeur utilisee pour forcer un
    // recalcul des qu'elle change, et un compteur force un rafraichissement periodique
    // pour refleter aussi une geometrie modifiee entre-temps (nouvelle salle generee...).
    float SlopeLastWalkableAngle = -1.f;
    int32 SlopeTicksSinceRecompute = 0;

    // Verificateur de hauteur libre (session 18) -- pendant du verificateur de pente :
    // sonde le sol, puis le plafond au-dessus, et signale en ROUGE tout endroit ou la
    // hauteur libre est inferieure a la capsule du joueur (couloir/arche infranchissable,
    // classique du blockout et invisible a l'oeil en vue de dessus).
    TSharedPtr<SEditableTextBox> ClearanceMinBox;   // vide = hauteur reelle de la capsule
    TSharedPtr<SCheckBox>        ClearanceShowCheck;
    TSharedPtr<STextBlock>       ClearanceInfoLabel;
    TArray<FSlopeSample> ClearanceCachedSamples;
    FVector ClearancePrevCameraLoc = FVector::ZeroVector;
    bool bClearancePrevLocValid = false;
    float ClearanceLastMin = -1.f;
    int32 ClearanceTicksSinceRecompute = 0;
    float ClearanceOverlayAccumTime = 0.f;

    // Historique des generations (chacune garde ses PROPRES acteurs), pour Annuler / Selectionner.
    TArray<FBlockoutGenerationEntry> GenerationHistory;

    FReply OnGenerateRoomClicked();
    FReply OnGenerateCorridorClicked();
    FReply OnGenerateSlabClicked();
    // Dessin libre : bascule le viewport dans UBlockoutDrawMode (le clic dans le
    // viewport n'est captable que depuis un mode d'edition, pas depuis ce panneau).
    FReply OnStartFreehandDrawClicked();
    FReply OnStopFreehandDrawClicked();
    // Decoupe : meme raison que le dessin -- percer demande de cliquer DANS le viewport,
    // sur une face precise, ce que seul un mode d'edition peut capter.
    FReply OnStartCutModeClicked();
    FReply OnStopCutModeClicked();
    FReply OnUseCameraForDrawPlaneClicked();
    FReply OnGenerateStaircaseClicked();
    FReply OnGenerateRampClicked();
    FReply OnGenerateVisionConeClicked();
    FReply OnGenerateBridgeArchClicked();
    FReply OnGenerateCurvedTunnelClicked();
    FReply OnUndoClicked();
    // UX (session 21) : OnUndoClicked() faisait deja GenerationHistory.Pop() -- un clic
    // repete remontait donc DEJA plusieurs generations en arriere (pas juste la derniere,
    // contrairement a ce qu'une note UX anterieure laissait supposer sans avoir relu le
    // code). Ce qui manquait reellement : la VISIBILITE (RefreshLastGenLabel n'affichait
    // que la toute derniere entree, aucun moyen de voir ce qu'on s'apprete a annuler en
    // remontant) et un moyen de tout vider d'un coup en fin de session de test. Les deux
    // sont ajoutes ici plutot qu'un vrai undo a acces arbitraire (ex. annuler la 3e sans
    // toucher aux 2 plus recentes) -- non fait, portee volontairement limitee cette session.
    FReply OnUndoAllClicked();
    FReply OnSelectLastClicked();

    FReply OnUseCameraForRoomClicked();
    FReply OnUseCameraForCorridorClicked();
    FReply OnUseCameraForSlabClicked();
    FReply OnUseCameraForStaircaseClicked();
    FReply OnUseCameraForRampClicked();
    FReply OnUseCameraForVisionConeClicked();
    FReply OnUseCameraForBridgeArchClicked();
    FReply OnUseCameraForCurvedTunnelClicked();
    FReply OnUseCameraForFovClicked();
    FReply OnJumpToFovViewClicked();

    // Generateur depuis un plan 2D
    FReply OnGenerateFromPlanClicked();
    FReply OnUseCameraForPlanClicked();
    FString GetPlanTexturePath() const;
    void OnPlanTextureChanged(const FAssetData& AssetData);
    // Remplit OutMask (1 octet par pixel : 1 = mur) depuis la texture choisie ou le
    // fichier PNG. Retourne false + OutError explicite en cas d'echec.
    bool LoadPlanPixels(TArray<uint8>& OutMask, int32& OutW, int32& OutH,
                        FString& OutSourceName, FString& OutError) const;

    // Swap intelligent
    FReply OnSwapCountClicked();
    FReply OnSwapReplaceClicked();
    FString GetSwapMeshPath() const;
    void OnSwapMeshChanged(const FAssetData& AssetData);
    // Cherche par NOM (Label contient l'identifiant) -- couvre les duplications
    // automatiques ("_2", "_3"...) sans dependre d'un tag pose a la main.
    TArray<AActor*> FindActorsMatchingSwapIdentifier(FString& OutIdentifier) const;

    void SetStatus(const FString& Msg, bool bError);
    void PushHistory(const FString& Name, const TArray<AActor*>& Actors);
    void RefreshLastGenLabel();

    // Capture l'ensemble des acteurs du niveau AVANT une generation, puis calcule
    // la difference APRES -- c'est la seule facon fiable de savoir "quels acteurs
    // vient de creer CE clic precis", independamment du nom choisi par l'utilisateur.
    TArray<AActor*> SnapshotAllActors() const;
    TArray<AActor*> DiffNewActors(const TArray<AActor*>& Before) const;

    static float ParseFloat(const TSharedPtr<SEditableTextBox>& Box, float Default);
    // Equivalent pour un champ deja converti en SSpinBox<float> (session 21) -- GetValue()
    // direct, pas de parsing de texte (une SSpinBox ne peut de toute facon pas contenir une
    // saisie invalide, contrairement a un SEditableTextBox).
    static float ParseSpinFloat(const TSharedPtr<SSpinBox<float>>& Box, float Default);
    static bool GetActiveViewportCameraLocation(FVector& OutLocation);
    static bool GetActiveViewportCameraTransform(FVector& OutLocation, FRotator& OutRotation);
    void FillPositionFromCamera(const TSharedPtr<SEditableTextBox>& XBox, const TSharedPtr<SEditableTextBox>& YBox, const TSharedPtr<SEditableTextBox>& ZBox);
    void FillPositionFromCameraSpin(const TSharedPtr<SSpinBox<float>>& XBox, const TSharedPtr<SSpinBox<float>>& YBox, const TSharedPtr<SSpinBox<float>>& ZBox);

    // Simulateur FOV -- FOV reel du joueur (meme repli PIE -> CDO du personnage du projet ->
    // defaut moteur que GetPlayerStepMetrics/ComputeMaxJumpHeight) + dessin du frustum.
    static float GetPlayerCameraFOV(FString& OutSource);
    void DrawFovFrustumOverlay();

    // Verificateur de pente -- grille de traces verticales + DrawDebugSolidPlane colore
    // par point touche (reutilise GetPlayerStepMetrics() pour le seuil "Vert").
    void DrawSlopeCheckOverlay();
    void DrawClearanceOverlay();
    FText GetClearanceSectionTitle() const;
    // Hauteur totale de la capsule du joueur (meme repli PIE -> CDO -> defaut moteur).
    static float GetPlayerCapsuleFullHeight(FString& OutSource);

    // Escalier / Rampe -- metriques joueur (meme repli PIE -> CDO du personnage du projet
    // -> defauts moteur que ComputeMaxJumpHeight, pour rester coherent avec le
    // reste du panneau) + calcul du plan (nombre de marches, giron, angle) porte
    // depuis stairs_ramps.py (Content/Python/), verifie dans cette session.
    static void GetPlayerStepMetrics(float& OutCapsuleRadius, float& OutMaxStepHeight, float& OutWalkableFloorAngleDeg, FString& OutSource);
    AActor* SpawnRotatedCube(FVector Location, FRotator Rotation, FVector SizeXYZ, const FString& Label);

    // Overlay metriques
    bool TickOverlay(float DeltaTime);
    void DrawGameplayOverlay();
    static class UWorld* GetDrawTargetWorld();
    static float JumpHeightFromMovement(UCharacterMovementComponent* MC);
    static float ComputeMaxJumpHeight(FString& OutSource);
    static bool GetFloatProperty(UObject* Obj, const TCHAR* PropName, float& OutValue);

    // OnEnterGenerate (UX, session 21) : si fourni, appuyer sur Entree dans ce champ
    // declenche directement le bouton "Generer" de sa section, sans obliger un clic
    // souris -- friction identifiee des la session 14 ("Entree dans un champ = Generer"),
    // jamais faite jusqu'ici. Optionnel/nullptr par defaut : ne change rien pour les
    // sections qui ne l'utilisent pas encore (Cone/Arche/Tunnel/Plan2D/Swap/Gabarit/
    // Grille/Duplication -- voir GAME_MEMORY.md pour la liste de ce qui reste a cabler).
    TSharedRef<SWidget> MakeField(const FText& Label, TSharedPtr<SEditableTextBox>& OutBox, const FString& DefaultValue,
        TFunction<FReply()> OnEnterGenerate = nullptr);
    // Equivalent SSpinBox<float> de MakeField (UX, session 21 -- dette identifiee session
    // 14 : molette/glisser, pas de saisie invalide possible, contrairement a un champ
    // texte). Delta = increment par cran de molette. MinValue/MaxValue optionnels (non
    // fournis = pas de borne, comme un champ texte libre). RESERVE aux champs dont la
    // valeur est TOUJOURS un nombre -- ne jamais l'utiliser pour un champ dont le texte
    // VIDE a un sens special ("laisser vide = auto/non fourni", ex. RampAngleBox,
    // SlopeWalkableAngleBox, ClearanceMinBox) : une SSpinBox a toujours une valeur
    // numerique, elle ne peut pas representer "vide".
    TSharedRef<SWidget> MakeSpinField(const FText& Label, TSharedPtr<SSpinBox<float>>& OutBox, float DefaultValue,
        float Delta = 1.f, TOptional<float> MinValue = TOptional<float>(), TOptional<float> MaxValue = TOptional<float>(),
        TFunction<FReply()> OnEnterGenerate = nullptr);
    TSharedRef<SWidget> MakeCheckboxField(const FText& Label, TSharedPtr<SCheckBox>& OutBox);
    TSharedRef<SWidget> MakeUseCameraButton(FOnClicked OnClicked);

    // UX (session 15) : chaque outil est desormais dans une section repliable, toutes
    // fermees au demarrage -- le panneau compte 11 outils et faisait un seul long
    // scroll ingerable. Le titre est un TAttribute pour permettre aux 3 overlays
    // temps reel d'afficher un indicateur "actif" meme quand leur section est repliee
    // (sinon un overlay coche puis repli tournerait sans aucun repere visible).
    TSharedRef<SWidget> MakeSection(TAttribute<FText> Title, TSharedRef<SWidget> Content);
    FText GetMetricsSectionTitle() const;
    FText GetFovSectionTitle() const;
    FText GetSlopeSectionTitle() const;

    // ── Persistance des champs entre sessions d'editeur (session 17) ──
    // Tous les champs texte revenaient a leur valeur par defaut a chaque redemarrage de
    // l'editeur : friction quotidienne la plus penible du panneau. Les valeurs sont
    // desormais ecrites dans GEditorPerProjectIni a la fermeture du panneau et relues a
    // sa construction. Les CASES A COCHER sont volontairement EXCLUES : un overlay qui
    // se rallumerait tout seul au demarrage de l'editeur (et consommerait) serait une
    // mauvaise surprise, pas un confort.
    TArray<TPair<FString, TSharedPtr<SEditableTextBox>>> PersistedFields;
    // Champs SSpinBox<float> (session 21) : liste separee, meme mecanisme de persistance
    // (cle = libelle + occurrence) mais un getter/setter different (GetValue/SetValue au
    // lieu de GetText/SetText) -- voir SavePanelSettings/LoadPanelSettings.
    TArray<TPair<FString, TSharedPtr<SSpinBox<float>>>> PersistedSpinFields;
    void RegisterPersistedField(const FText& Label, const TSharedPtr<SEditableTextBox>& Box);
    void RegisterPersistedSpinField(const FText& Label, const TSharedPtr<SSpinBox<float>>& Box);
    void SavePanelSettings() const;
    void LoadPanelSettings();

    // ── Gabarit de reference (session 18) ──
    // Pose au sol, sous la camera, un reperes physique aux dimensions REELLES du joueur
    // (capsule) + un marqueur a sa hauteur de saut max : permet de juger une echelle ou
    // la franchissabilite d'un rebord a l'oeil, sans lancer le jeu.
    TSharedPtr<STextBlock> GaugeInfoLabel;
    FReply OnSpawnReferenceGaugeClicked();

    // ── Alignement sur grille (session 19) ──
    // Le bouton camera remplit les positions avec des valeurs brutes (ex 1237.42 UU) --
    // rien n'est aligne sur rien. Sans consequence tant que c'est du blockout, mais des
    // que le Swap intelligent remplace ces boites par un kit d'art modulaire, les meshes
    // ne se raccordent plus entre eux (joints ouverts/chevauchements). Applique a la
    // POSITION D'ORIGINE de chaque generateur (le point que le designer saisit/recupere
    // a la camera), jamais a chaque sous-acteur genere individuellement -- arrondir
    // separement chaque marche d'escalier ou chaque voussoir d'arche casserait la
    // geometrie relative de la forme.
    TSharedPtr<SEditableTextBox> GridSizeBox;
    TSharedPtr<SCheckBox>        GridSnapCheck;
    FReply OnSnapSelectionToGridClicked();
    FVector SnapPositionIfEnabled(float X, float Y, float Z) const;

    // ── Duplication en serie (session 19) ──
    // Duplique le (ou les) acteur(s) SELECTIONNE(S) N fois le long d'une direction,
    // espaces regulierement -- couvre l'usage quotidien "poser un alignement de piliers/
    // segments de couloir/palissade", fait aujourd'hui un par un a la main. Les copies
    // sont ajoutees a l'historique (Annuler les retire) et rangees dans le meme dossier
    // Outliner que l'acteur source.
    TSharedPtr<SEditableTextBox> DupCountBox;
    TSharedPtr<SEditableTextBox> DupSpacingBox;
    TSharedPtr<SEditableTextBox> DupDirYawBox;
    TSharedPtr<STextBlock>       DupInfoLabel;
    FReply OnDuplicateInSeriesClicked();
};
