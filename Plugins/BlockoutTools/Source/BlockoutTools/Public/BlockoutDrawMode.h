#pragma once

#include "CoreMinimal.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#include "BlockoutDrawMode.generated.h"

class AActor;
class UStaticMesh;

/** Etape courante du dessin. */
UENUM()
enum class EBlockoutDrawStage : uint8
{
	/** On pose les points du contour au sol. */
	Contour,
	/** Le contour est ferme : la hauteur suit la souris jusqu'au clic de validation. */
	Height,
};

/**
 * Mode d'edition "Dessin libre de salle".
 *
 * PORTAGE de LevelDesignTools (Unity) : le mode Dessin libre de RoomBuilderWindow.
 * On clique un contour dans le viewport, on le ferme, la boite apparait et sa hauteur
 * SUIT LA SOURIS jusqu'au clic de validation -- regler une hauteur revient a saisir la
 * geometrie plutot qu'a taper un nombre puis a verifier le resultat.
 *
 * Pourquoi un UEdMode et pas un bouton de plus dans le panneau Slate : capter un clic
 * DANS le viewport (et empecher la selection normale pendant ce temps) n'est possible
 * que depuis un mode d'edition. Le panneau reste l'endroit ou l'on regle les valeurs
 * (UBlockoutDrawSettings) ; le mode ne fait que les lire.
 *
 * UBaseLegacyWidgetEdMode plutot que UEdMode nu : c'est lui qui implemente
 * ILegacyEdModeViewportInterface, donc InputKey / MouseMove / HandleClick / Render /
 * DrawHUD. UEdMode seul n'a aucun de ces points d'entree.
 */
UCLASS(Transient)
class BLOCKOUTTOOLS_API UBlockoutDrawMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	/** Identifiant du mode, utilise par GLevelEditorModeTools().ActivateMode(). */
	static const FEditorModeID EM_BlockoutDraw;

	UBlockoutDrawMode();

	// ── UEdMode ───────────────────────────────────────────────────────────────
	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool UsesToolkits() const override;

	// ── ILegacyEdModeWidgetInterface ──────────────────────────────────────────
	virtual bool ShowModeWidgets() const override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool UsesPropertyWidgets() const override;
	virtual bool AllowsViewportDragTool() const override;

	// ── ILegacyEdModeViewportInterface ────────────────────────────────────────
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 MouseX, int32 MouseY) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;

private:
	/** Etape courante. */
	EBlockoutDrawStage Stage = EBlockoutDrawStage::Contour;

	/** Points du contour, en coordonnees monde, tous a l'altitude du plan de dessin. */
	TArray<FVector> ContourPoints;

	/** Dernier point survole sur le plan de dessin (previsualisation du segment en cours). */
	FVector HoverPoint = FVector::ZeroVector;
	bool bHasHoverPoint = false;

	/** La contrainte d'angle droit s'applique-t-elle au point survole ? (affichage HUD) */
	bool bOrthoActive = false;

	/** Vrai si le curseur est assez pres du 1er point pour fermer le contour d'un clic. */
	bool bHoverNearFirstPoint = false;

	/** Hauteur signee en cours de reglage (positive = vers le haut). */
	double SignedHeight = 300.0;

	/** Centre du contour, au niveau du plan de dessin : pivot du reglage de hauteur. */
	FVector HeightPivot = FVector::ZeroVector;

	/** Message affiche en surimpression (erreur de contour, resultat de generation). */
	FString StatusMessage;

	// ── Interne ───────────────────────────────────────────────────────────────
	double GroundZ() const;

	/** Intersection d'un rayon avec le plan de dessin. */
	bool ProjectOnGround(const FVector& Origin, const FVector& Direction, FVector& OutPoint) const;

	/** Distance a l'ecran, en pixels, entre un point monde et une position curseur. */
	static bool ScreenDistanceTo(FEditorViewportClient* ViewportClient, const FVector& WorldPoint,
		const FIntPoint& CursorPos, double& OutPixels);

	/**
	 * `bOrthoConstraint` (Ctrl) contraint le point survole a un angle droit par rapport
	 * au premier segment. Sans effet tant que le contour compte moins de 2 points : le
	 * repere n'existe pas encore.
	 */
	void UpdateHover(FEditorViewportClient* ViewportClient, const FVector& RayOrigin,
		const FVector& RayDirection, const FIntPoint& CursorPos, bool bOrthoConstraint);
	void UpdateHeightFromRay(FEditorViewportClient* ViewportClient, const FVector& RayOrigin, const FVector& RayDirection, bool bSnapToGrid);

	/** Passe a l'etape de reglage de hauteur. Refuse un contour invalide, en disant pourquoi. */
	bool BeginHeightStage();

	/**
	 * Ctrl+F : ajuste le dernier point pour que la fermeture retombe d'equerre, puis
	 * ferme le contour. Renvoie false sans rien modifier si l'ajustement n'a pas de
	 * sens (moins de 4 points, murs paralleles, resultat degenere).
	 */
	bool CloseSquareWithLastPoint();

	void ResetDrawing();

	/** Construit les acteurs. Renvoie false et remplit StatusMessage en cas d'echec. */
	bool GenerateRoom();

	/**
	 * Construit et sauvegarde l'asset UStaticMesh d'un panneau.
	 *
	 * SEPARE de la pose de l'acteur, et appele AVANT l'ouverture de la transaction :
	 * un asset cree dans une transaction est ramene a son etat d'avant par un Ctrl+Z,
	 * ce qui laisse un UStaticMesh avec un sous-objet de bulk data nul et fait tomber
	 * l'editeur au premier autosave (bug du 2026-08-26).
	 */
	UStaticMesh* BuildPanelMeshAsset(const FString& PanelName, const TArray<FVector2D>& LocalContour,
		double Thickness, FString& OutError);

	/** Pose l'acteur d'un panneau sur un mesh DEJA construit, avec son etat logique. */
	AActor* SpawnPanelActor(UStaticMesh* PanelMesh, const FString& PanelName,
		const TArray<FVector2D>& LocalContour, const FVector& WorldOrigin,
		double LocalZ, double Thickness, FString& OutError);
};
