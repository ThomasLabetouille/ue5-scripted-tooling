#pragma once

#include "CoreMinimal.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#include "BlockoutCutMode.generated.h"

class AActor;
class UStaticMesh;
class UBlockoutPanelComponent;

/**
 * Surface visee par la decoupe : tout ce dont l'interaction a besoin, resolu une fois.
 *
 * Porte de CutSurface (RoomBuilderWindow.cs). Deux cas couverts sans distinction : une boite
 * brute (le mesh porte une taille unitaire ou non, l'echelle du transform porte les dimensions)
 * et un panneau deja decoupe (le mesh est a taille reelle, echelle 1).
 */
struct FBlockoutCutSurface
{
	AActor* Actor = nullptr;

	/** Present si l'acteur a deja ete decoupe : son repere et ses trous font foi. */
	UBlockoutPanelComponent* Panel = nullptr;

	/** Taille du panneau en unites monde (taille du mesh multipliee par l'echelle). */
	FVector PanelSize = FVector::ZeroVector;

	/** Centre du mesh dans l'espace local de l'acteur (non nul si le mesh n'est pas centre). */
	FVector LocalCenter = FVector::ZeroVector;

	FVector Scale = FVector::OneVector;

	/** Repere du panneau : U et V dans le plan de la face, W dans l'epaisseur. */
	int32 UAxis = 0;
	int32 VAxis = 1;
	int32 WAxis = 2;

	/** Demi-epaisseur, en unites monde. */
	double HalfW = 0.0;

	/** Contour exterieur en coordonnees (U,V), unites monde. */
	TArray<FVector2D> Outer;

	bool IsValid() const { return Actor != nullptr && Outer.Num() >= 3; }
};

/**
 * Mode d'edition "Decoupe" : percer une ouverture dans un mur, un sol ou un plafond.
 *
 * PORTAGE de LevelDesignTools (Unity), mode Cut de RoomBuilderWindow. On survole une surface,
 * on clique le contour de l'ouverture SUR cette surface, on ferme, le trou est perce.
 *
 * POURQUOI LE MESH N'EST PAS LA SOURCE DE VERITE. UBlockoutPanelComponent garde la description
 * logique du panneau -- contour, epaisseur, repere, liste des trous -- et le maillage en est
 * regenere. C'est ce qui permet de percer vingt ouvertures dans un meme mur sans degradation,
 * d'en retirer une, ou d'annuler. La version Unity a fait ce choix apres avoir constate qu'un
 * mesh deja troue ne permet plus de retrouver ni les dimensions d'origine ni les trous
 * precedents.
 *
 * La cible se choisit AU CURSEUR, pas dans l'Outliner, et se verrouille au premier point : le
 * contour doit rester sur une seule surface. Sortir du survol ne vide pas la cible memorisee.
 */
UCLASS(Transient)
class BLOCKOUTTOOLS_API UBlockoutCutMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	UBlockoutCutMode();

	static const FEditorModeID EM_BlockoutCut;

	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool UsesToolkits() const override { return false; }
	virtual bool ShowModeWidgets() const override { return false; }
	virtual bool ShouldDrawWidget() const override { return false; }
	virtual bool UsesPropertyWidgets() const override { return false; }
	virtual bool AllowsViewportDragTool() const override { return false; }

	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport,
		FKey Key, EInputEvent Event) override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport,
		int32 MouseX, int32 MouseY) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy,
		const FViewportClick& Click) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport,
		FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport,
		const FSceneView* View, FCanvas* Canvas) override;

	/**
	 * La cible est-elle decoupable ? `OutReason` porte le motif de refus.
	 *
	 * Le motif n'est pas du confort : c'est lui qui distingue "refuse pour une bonne raison"
	 * de "refuse tout". Un booleen seul ne le dirait pas.
	 */
	static bool IsValidCutTarget(AActor* Target, FString& OutReason);

	/** Resout le repere, la taille et le contour exterieur de la surface visee. */
	static bool TryDescribeCutSurface(AActor* Target, FBlockoutCutSurface& OutSurface);

private:
	/** Points du contour de l'ouverture, en coordonnees monde, sur la face visee. */
	TArray<FVector> CutPoints;

	/** Cible verrouillee des le premier point pose. */
	TWeakObjectPtr<AActor> CutTarget;

	/** Dernier acteur survole, meme invalide (sert a afficher le refus). */
	TWeakObjectPtr<AActor> HoverActor;

	FVector HoverPoint = FVector::ZeroVector;
	bool bHasHoverPoint = false;
	bool bHoverNearFirstPoint = false;

	FString StatusMessage;

	void ResetCutting();

	/** Projette un rayon sur la face tournee vers la camera. Faux si hors contour ou dans un trou. */
	static bool TryProjectOnSurface(const FBlockoutCutSurface& Surface,
		const FVector& RayOrigin, const FVector& RayDirection,
		FVector& OutWorldPoint, FVector2D& OutUV);

	/**
	 * Vrai si le mesh EST sa boite englobante : chaque triangle repose entierement sur l'une
	 * des six faces de cette boite.
	 *
	 * Le raisonnement par SOMMET ne suffit pas -- porte tel quel du bug Unity : le cylindre
	 * passait, tous ses sommets etant poses sur les plans du haut et du bas, et le decouper
	 * l'aurait remplace par une boite. Raisonner par TRIANGLE tranche : la paroi d'un cylindre
	 * relie le plan du haut a celui du bas sans reposer sur aucun des deux, alors qu'une face
	 * de boite, meme subdivisee, reste dans son plan.
	 */
	static bool IsBoxMesh(UStaticMesh* Mesh);

	/** Valide et applique la decoupe. Ne modifie rien si une validation echoue. */
	void PerformCut();

	/** Regenere le maillage d'un panneau a partir de son etat logique. */
	static bool RebuildPanelMesh(AActor* Target, UBlockoutPanelComponent* Panel, FString& OutError);

	void UpdateHover(FEditorViewportClient* ViewportClient, const FVector& RayOrigin,
		const FVector& RayDirection, const FIntPoint& CursorPos);
};
