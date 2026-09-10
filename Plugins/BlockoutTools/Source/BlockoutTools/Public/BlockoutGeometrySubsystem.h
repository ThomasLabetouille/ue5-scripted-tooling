#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "BlockoutPanelComponent.h"   // FBlockoutPanelHole (expose aux UFUNCTION de panneau)
#include "BlockoutGeometrySubsystem.generated.h"

class UStaticMesh;

/**
 * Un mur rectangulaire calcule par ComputePlanWallRects -- transform + taille,
 * pas encore un acteur (le spawn reste cote handler Slate, qui utilise deja
 * SpawnScaledRotatedCube). Ajoute le 2026-08-10 (voir
 * Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md) pour rendre testable le noyau
 * pur du Generateur depuis un plan 2D, seul outil du panneau jamais confirme
 * fonctionnel meme une fois.
 */
USTRUCT(BlueprintType)
struct FBlockoutPlanWallRect
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Blockout")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Blockout")
	FVector Size = FVector::ZeroVector;
};

/**
 * Editor subsystem exposing room/corridor generation to Python and Blueprints.
 * All geometry is built from /Engine/BasicShapes/Cube scaled to the required
 * dimensions. SpawnScaledCube is the primitive; GenerateRoom and
 * GenerateCorridor are higher-level helpers that call it with computed params.
 *
 * Etendu le 2026-07-30 (session 21) : la logique de generation d'Escalier/Rampe
 * (calcul du plan + spawn des marches/du plan incline), auparavant privee dans
 * SBlockoutToolPanel (donc invisible a Python -- seul un vrai clic dans l'UI
 * pouvait l'exercer), vit desormais ici sous forme de UFUNCTION. Objectif :
 * rendre la LOGIQUE des outils testable par script (test_blockout_tools.py),
 * independamment du panneau Slate. Le panneau reste l'interface recommandee
 * pour le level designer -- ce sous-systeme est le moteur de calcul qu'il
 * appelle, pas un chemin alternatif a lui proposer.
 */
UCLASS()
class BLOCKOUTTOOLS_API UBlockoutGeometrySubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Spawns one AStaticMeshActor using /Engine/BasicShapes/Cube at the given
	 * Location, scaled to Scale (UU = scale * 100), labelled ActorLabel.
	 * WorldContext may be nullptr -- falls back to the editor world.
	 */
	/**
	 * Trace a angles droits : contraint un point pour que le segment [Last -> resultat]
	 * soit parallele ou perpendiculaire au repere. Expose pour que le test exerce la
	 * MEME fonction que le mode dessin, et pas une reimplementation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	FVector2D SnapToOrthogonalFrame(FVector2D Last, FVector2D Candidate, FVector2D FrameDir,
		FVector2D IncomingDir) const;

	/**
	 * Fermeture d'equerre (Ctrl+F). Le succes sort en PARAMETRE, pas en valeur de
	 * retour : Python ne recupere pas la valeur de retour d'une UFUNCTION qui a des
	 * parametres de sortie (piege deja documente dans CLAUDE.md), le test ne pourrait
	 * donc pas distinguer un refus d'un ajustement.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	void SnapLastPointToCloseFrame(FVector2D First, FVector2D FrameDir, FVector2D BeforeLast,
		FVector2D& OutAdjusted, bool& bOutOk) const;

	UFUNCTION(BlueprintCallable, Category = "Blockout")
	AActor* SpawnScaledCube(UObject* WorldContext, FVector Location, FVector Scale, FString ActorLabel);

	/**
	 * Meme primitive que SpawnScaledCube, avec une rotation. Utilisee pour tout
	 * element non axe-aligne (marches d'escalier, plan incline de rampe...).
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	AActor* SpawnScaledRotatedCube(UObject* WorldContext, FVector Location, FRotator Rotation, FVector Scale, FString ActorLabel);

	/**
	 * Generates floor + 4 walls (+ ceiling, sauf si bNoCeiling) for a rectangular room.
	 * Center   – world-space centre of the room interior.
	 * SizeXY   – interior X and Y extents (Z is ignored).
	 * WallHeight  – interior wall height (Z extent).
	 * WallThickness – thickness applied to every surface.
	 * RoomName – prefix for actor labels (e.g. "Room1_Floor", "Room1_WallN"...).
	 * bNoCeiling – si vrai, le plafond n'est jamais spawn (evite de le spawner
	 *   puis de le detruire aussitot, comme le faisait l'ancienne version cote
	 *   panneau -- meme resultat visible, un aller-retour d'acteur en moins).
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	void GenerateRoom(FVector Center, FVector SizeXY, float WallHeight, float WallThickness, FString RoomName, bool bNoCeiling);

	/**
	 * Generates a corridor (floor + ceiling + 2 side walls) between Start and End.
	 * The corridor is axis-aligned along the Start→End direction.
	 * MODIFIE le 2026-08-10 (voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md item #1) :
	 * retournait void auparavant (les appelants existants faisaient un diff avant/apres
	 * de tous les acteurs du niveau pour recuperer les acteurs crees -- voir
	 * SBlockoutToolPanel::SnapshotAllActors/DiffNewActors). Retourne desormais les 4
	 * acteurs crees directement, necessaire pour que GenerateCurvedTunnel puisse les
	 * collecter segment par segment sans repasser par un diff de niveau entier.
	 * Aucun appelant existant ne dependait du type void (tous ignoraient deja la valeur
	 * de retour) -- changement de signature verifie sans risque de regression.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	TArray<AActor*> GenerateCorridor(FVector Start, FVector End, float Width, float Height, float WallThickness, FString CorridorName);

	/**
	 * Calcule le plan d'un escalier (nombre de marches, hauteur/marche, giron,
	 * longueur totale) a partir d'une hauteur cible et des metriques du joueur --
	 * formule de Blondel, giron minimal replie sur le rayon de capsule. Pure
	 * fonction de calcul (aucun acteur spawn) : c'est la partie la plus utile a
	 * tester automatiquement, car un bug ici produit un escalier qui COMPILE et
	 * se dessine, mais dont les marches ne correspondent pas a la hauteur voulue
	 * ou sont trop etroites pour la capsule du joueur -- invisible a l'oeil sans
	 * mesurer, facile a rater en relecture visuelle seule.
	 * OutWarning est non-vide si le giron a du etre force au minimum (pente trop
	 * raide pour Blondel).
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	void ComputeStaircasePlan(float TotalHeight, float CapsuleRadius, float MaxStepHeight,
		int32& OutNumSteps, float& OutStepHeight, float& OutGoing, float& OutTotalRun, FString& OutWarning);

	/**
	 * Genere un escalier (une box solide par marche, sans trou dessous) le long
	 * de Yaw depuis Start, sur TotalHeight de haut. Appelle ComputeStaircasePlan
	 * en interne puis SpawnScaledRotatedCube par marche. Retourne les acteurs
	 * crees (tableau vide = echec).
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	TArray<AActor*> GenerateStaircase(FVector Start, float Yaw, float TotalHeight, float Width,
		float CapsuleRadius, float MaxStepHeight, FString StairName);

	/**
	 * Calcule le plan d'une rampe (angle, longueur du plan incline, franchissable
	 * ou non) a partir d'une hauteur cible et OPTIONNELLEMENT d'une longueur
	 * horizontale (RunOverride) OU d'un angle (AngleOverrideDeg) impose -- l'angle
	 * a priorite si les deux sont fournis. Passer 0 (ou negatif) pour "non fourni"
	 * (pas de TOptional : signature Blueprint/Python-friendly). Pure fonction de
	 * calcul, memes raisons de tester automatiquement que ComputeStaircasePlan --
	 * une rampe trop raide pour le joueur (OutWalkable=false) peut ne pas se voir
	 * a l'oeil sur un simple screenshot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	void ComputeRampPlan(float TotalHeight, float RunOverride, float AngleOverrideDeg, float MaxWalkableAngleDeg,
		float& OutRun, float& OutAngleDeg, float& OutLength, bool& OutWalkable, FString& OutWarning);

	/**
	 * Genere une rampe (plan incline unique) le long de Yaw depuis Start, sur
	 * TotalHeight de haut. Appelle ComputeRampPlan en interne puis
	 * SpawnScaledRotatedCube. Retourne l'acteur cree (nullptr = echec).
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	AActor* GenerateRamp(FVector Start, float Yaw, float TotalHeight, float Width,
		float RunOverride, float AngleOverrideDeg, float MaxWalkableAngleDeg, FString RampName);

	// ── Cone de vision / Arche de pont / Tunnel courbe -- migres le 2026-08-10
	// (voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md, item #1) depuis les
	// handlers Slate de SBlockoutToolPanel_Generators.cpp, meme demarche que
	// Escalier/Rampe en session 21 : rendre la LOGIQUE testable depuis Python
	// independamment du panneau. Params invalides (voir corps de chaque
	// fonction) -> tableau/points vides, jamais de crash ni de spawn partiel.

	/**
	 * Genere un eventail de NumSegments cubes couvrant AngleDeg autour de Yaw,
	 * a Radius UU de l'Apex, epais de Height. Chaque segment est une corde de
	 * l'arc (meme esprit que les marches d'escalier). Retourne un tableau vide
	 * si Radius/Height <= 0 ou AngleDeg hors de ]0, 360].
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	TArray<AActor*> GenerateVisionCone(FVector Apex, float Yaw, float Radius, float AngleDeg,
		float Height, int32 NumSegments, FString ConeName);

	/**
	 * Genere une arche de pont : 2 piliers (omis si PierHeight <= 0), NumSegments
	 * voussoirs suivant une demi-ellipse (t=0 pied gauche, t=1 pied droit, apex a
	 * t=0.5), et un tablier au sommet. Retourne un tableau vide si Span/Rise/
	 * Thickness/Width/DeckThickness <= 0 ou PierHeight < 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	TArray<AActor*> GenerateBridgeArch(FVector Base, float Yaw, float Span, float Rise,
		float Thickness, float Width, float PierHeight, float DeckThickness,
		int32 NumSegments, FString ArchName);

	/**
	 * Calcule NumSegments+1 points le long d'un arc de cercle de Radius UU,
	 * balayant AngleDeg depuis Start (Yaw = direction initiale). Convention :
	 * Angle > 0 = virage a GAUCHE, Angle < 0 = virage a DROITE (meme sens que
	 * la rotation positive de l'UE, +X vers +Y). Pure fonction de calcul --
	 * c'est la partie du Tunnel courbe explicitement signalee "NON VERIFIEE
	 * VISUELLEMENT" avant cette migration (voir CLAUDE.md) : le sens du virage
	 * est exactement le genre de bug invisible a l'oeil sur un screenshot seul,
	 * donc la partie la plus utile a couvrir par un test automatise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	void ComputeCurvedTunnelPoints(FVector Start, float Yaw, float Radius, float AngleDeg,
		int32 NumSegments, TArray<FVector>& OutPoints);

	/**
	 * Genere un tunnel courbe : appelle ComputeCurvedTunnelPoints puis
	 * GenerateCorridor entre chaque paire de points consecutifs (meme
	 * WallThickness que la Salle/le Couloir par defaut). Retourne un tableau
	 * vide si Radius/Width/Height <= 0 ou AngleDeg ~= 0 (tunnel droit : utiliser
	 * l'outil Couloir a la place).
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	TArray<AActor*> GenerateCurvedTunnel(FVector Start, float Yaw, float Radius, float AngleDeg,
		float Width, float Height, float WallThickness, int32 NumSegments, FString TunnelName);

	// ── Swap intelligent -- migre le 2026-08-10 (voir
	// Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md, item #2). Contrairement aux
	// generateurs ci-dessus, cette action ne CREE pas de geometrie a partir de
	// parametres : elle opere sur des acteurs DEJA PRESENTS dans le niveau
	// (trouves par nom, remplaces par un mesh final). Deja signalee "NON VERIFIE
	// en conditions reelles" avant cette migration -- c'est le seul item du
	// backlog de test juge suffisamment a risque (action destructive) pour
	// justifier l'effort, contrairement a Alignement sur grille/Duplication en
	// serie/Gabarit de reference (simples, bug visible immediatement a l'ecran).

	/**
	 * Trouve tous les acteurs du niveau dont le Label (nom affiche, PAS le nom
	 * interne UObject) contient Identifier. Match par nom plutot que par tag --
	 * choix explicite du panneau (fonctionne immediatement sur tout ce qui est
	 * deja place dans le niveau, y compris les duplications automatiques
	 * "_2"/"_3", sans dependre d'un tag pose a la main). Identifier vide ->
	 * tableau vide (jamais "tout matcher par accident").
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	TArray<AActor*> FindActorsByLabelContains(UObject* WorldContext, FString Identifier);

	/**
	 * Remplace chaque acteur de OldActors par un nouvel AStaticMeshActor utilisant
	 * NewMesh, en conservant position/rotation/tags/label/dossier Outliner/mobilite
	 * du composant racine. L'ancien acteur est detruit AVANT que le nouveau ne
	 * reprenne son label (sinon UE5 suffixe "_2" sur le nouveau). Echelle du
	 * nouveau toujours (1,1,1) -- un kit d'art modulaire est concu a la bonne
	 * taille, l'etirer le deformerait. Entrees nulles dans OldActors ignorees
	 * silencieusement (pas une erreur). Retourne les acteurs reellement crees
	 * (longueur < OldActors.Num() si des entrees etaient nulles).
	 * DESTRUCTIF, aucune transaction ici -- l'appelant (le panneau Slate) est
	 * responsable de son propre FScopedTransaction si un undo est souhaite ;
	 * un appel de test doit nettoyer lui-meme les acteurs retournes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	TArray<AActor*> SwapActorsToMesh(UObject* WorldContext, const TArray<AActor*>& OldActors, UStaticMesh* NewMesh);

	// ── Alignement sur grille / Duplication en serie / Generateur de plan 2D --
	// noyaux purs extraits le 2026-08-10 (voir
	// Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md, "10 outils Slate-only" ->
	// verification reelle du code a montre que 4 de ces outils ont un coeur de
	// calcul pur, contrairement a FOV/Pente/Hauteur libre/Gabarit qui dependent
	// tous d'une sonde de scene ou d'une transform camera reelles (restent
	// Slate-only, testables uniquement a l'ecran -- voir protocole de test
	// manuel). Duplication en serie et Generateur de plan 2D gardent leur appel
	// destructif/spawn cote handler Slate -- seul le calcul est ici.

	/**
	 * Arrondit Position sur une grille de GridSize UU (chaque axe independamment).
	 * bEnabled=false -> retourne Position inchangee (reproduit exactement le
	 * comportement de la case a cocher "Aligner sur la grille" du panneau).
	 * GridSize est force a un minimum de 1 UU pour eviter une division par zero.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	FVector SnapToGrid(FVector Position, float GridSize, bool bEnabled) const;

	/**
	 * Calcule les Count offsets (depuis la source, PAS cumulatifs entre eux --
	 * chacun est mesure depuis la position d'origine) d'une duplication en
	 * serie le long de YawDeg, espaces de Spacing UU. Offset i (1-indexe) =
	 * Dir * (Spacing * i). Count <= 0 -> tableau vide. Le spawn effectif
	 * (UEditorActorSubsystem::DuplicateActor) reste dans le handler Slate,
	 * qui a besoin d'un acteur source et d'un monde reels.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	TArray<FVector> ComputeSeriesOffsets(float YawDeg, float Spacing, int32 Count) const;

	/**
	 * Noyau pur du Generateur depuis un plan 2D : a partir d'un masque de
	 * pixels deja decode (Mask[y*ImgW+x] != 0 => pixel sombre), calcule les
	 * rectangles de murs en repere MONDE (fusion gloutonne en rectangles
	 * maximaux sur une grille sous-echantillonnee de Cell x Cell pixels, memes
	 * formules de placement/inversion d'axe Y que l'ancien handler Slate).
	 * Ne lit AUCUNE texture/fichier (LoadPlanPixels reste cote handler, seule
	 * partie de l'outil qui depend vraiment de l'editeur) et ne spawn aucun
	 * acteur -- seul SpawnScaledRotatedCube par mur reste cote handler.
	 * Mask.Num() != ImgW*ImgH, ou ImgW/ImgH/Cell <= 0 -> OutWalls vide.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout")
	void ComputePlanWallRects(const TArray<uint8>& Mask, int32 ImgW, int32 ImgH, int32 Cell,
		float UUPerPixel, float WallHeight, float PosX, float PosY, float PosZ,
		TArray<FBlockoutPlanWallRect>& OutWalls) const;

	// ═══════════════════════════════════════════════════════════════════════
	// Panneaux (dessin libre / decoupe) -- pont vers le noyau pur
	//
	// BlockoutPanelGeometry ne depend d'AUCUNE API moteur, ce qui le rend rapide a
	// exercer mais invisible depuis Python. Ces UFUNCTION sont le seul pont : elles
	// ne contiennent aucune logique, elles appellent le noyau et convertissent les
	// types. C'est ce qui permet a test_blockout_panels.py de verifier des PROPRIETES
	// (aire triangulee = aire du contour moins celle des trous, prisme ferme, pas de
	// T-jonction...) sur des milliers de contours tires au hasard, comme le harnais
	// C# de la version Unity dont cet outil est le portage.
	// ═══════════════════════════════════════════════════════════════════════

	/**
	 * Triangule un contour perce de trous. Renvoie les sommets a plat :
	 * 3 sommets consecutifs = 1 triangle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout|Panneau")
	TArray<FVector2D> TriangulatePanelFace(const TArray<FVector2D>& Outer, const TArray<FBlockoutPanelHole>& Holes);

	/**
	 * Assemble le maillage complet d'un panneau (2 faces + chants). Renvoie false si la
	 * surface est degeneree. Sorties en FVector/FVector2D (les UFUNCTION ne transportent
	 * pas FVector3f) -- le vrai maillage reste en simple precision.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout|Panneau")
	bool BuildPanelMeshForTest(const TArray<FVector2D>& Outer, float Thickness,
		const TArray<FBlockoutPanelHole>& Holes, int32 UAxis, int32 VAxis, int32 WAxis,
		TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector2D>& OutUVs);

	/** Contour ferme sans sommet duplique ni arete qui se croise. */
	UFUNCTION(BlueprintCallable, Category = "Blockout|Panneau")
	bool IsSimpleContour(const TArray<FVector2D>& Points);

	/** Aire signee (positive = sens trigonometrique). */
	UFUNCTION(BlueprintCallable, Category = "Blockout|Panneau")
	float ContourSignedArea(const TArray<FVector2D>& Loop);

	/** Point strictement a l'interieur du contour (lancer de rayon). */
	UFUNCTION(BlueprintCallable, Category = "Blockout|Panneau")
	bool PointInContour(FVector2D Point, const TArray<FVector2D>& Polygon);

	/** Deux contours qui se croisent ou dont l'un contient l'autre. */
	UFUNCTION(BlueprintCallable, Category = "Blockout|Panneau")
	bool ContoursOverlap(const TArray<FVector2D>& A, const TArray<FVector2D>& B);

	/** "Inner" strictement interieur a "Outer", a plus de Margin de son bord. */
	UFUNCTION(BlueprintCallable, Category = "Blockout|Panneau")
	bool ContourContainsWithMargin(const TArray<FVector2D>& Outer, const TArray<FVector2D>& Inner, float Margin);


	/**
	 * Cree un asset UStaticMesh de panneau depuis un script, dans PackageFolder
	 * (vide = /Game/Blockout/GeneratedMeshes). Existe pour rendre TESTABLE le chemin
	 * d'ecriture d'asset, qui n'etait exerce que par un vrai clic dans le viewport --
	 * c'est precisement la ou le crash du 2026-08-26 est passe inapercu.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout|Panneau")
	UStaticMesh* CreatePanelMeshAsset(const TArray<FVector2D>& Outer, float Thickness,
		const TArray<FBlockoutPanelHole>& Holes, const FString& AssetBaseName,
		const FString& PackageFolder, FString& OutError);

	/**
	 * Vrai si le UStaticMesh est structurellement sain : au moins un LOD, et chaque
	 * source model (LOD + HiRes) reellement initialise.
	 *
	 * Sonde NON DESTRUCTIVE : elle s'appuie sur FStaticMeshSourceModel::
	 * IsSourceModelInitialized(), la seule facon de tester le sous-objet de bulk data
	 * SANS declencher le check() du moteur. Un mesh dont ce sous-objet est nul ne se
	 * signale autrement qu'en faisant tomber l'editeur au prochain autosave, plusieurs
	 * dizaines de secondes apres la cause -- voir le bug du 2026-08-26 (asset cree a
	 * l'interieur d'une transaction, puis Ctrl+Z).
	 *
	 * Renvoie une chaine VIDE si le mesh est sain, sinon la raison. Volontairement pas
	 * un bool avec parametre de sortie : la liaison Python d'Unreal replie ce couple en
	 * "tuple ou None", ce qui rend une sonde de sante ambigue a lire depuis un test.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blockout|Panneau")
	FString GetPanelMeshHealthProblem(UStaticMesh* Mesh);

};
