#pragma once

#include "CoreMinimal.h"

/**
 * Noyau geometrique du "panneau" -- un contour plat quelconque, extrude sur une
 * epaisseur, eventuellement perce de trous.
 *
 * PORTAGE de LevelDesignTools (Unity) : Assets/Editor/PolygonTriangulator.cs +
 * PanelMeshBuilder.cs. La contrainte qui a fait la valeur de la version Unity est
 * reprise telle quelle : ce fichier ne touche AUCUNE API d'editeur (ni UStaticMesh,
 * ni FMeshDescription, ni GEditor, ni acteur). Il ne depend que de Core (FVector2D,
 * FMath). C'est ce qui permet de l'exercer sur des milliers de contours par script
 * sans ouvrir de viewport -- cf. Content/Python/test_blockout_panels.py.
 *
 * UNITES : tout est en unites Unreal (cm). La version Unity travaillait en metres
 * avec Epsilon = 1e-5 ; 1e-3 UU vaut exactement la meme tolerance physique.
 */

/**
 * Le maillage d'un panneau, sous forme de listes brutes plutot que de
 * FMeshDescription : c'est ce qui permet de le construire et de le verifier
 * hors editeur. La conversion vers un UStaticMesh vit dans BlockoutPanelMeshAsset.
 */
struct FBlockoutPanelMesh
{
	TArray<FVector3f> Vertices;
	TArray<FVector2f> UVs;
	TArray<int32> Triangles;

	int32 TriangleCount() const { return Triangles.Num() / 3; }
	bool IsEmpty() const { return Triangles.Num() == 0; }

	void Reset()
	{
		Vertices.Reset();
		UVs.Reset();
		Triangles.Reset();
	}
};

namespace BlockoutPanelGeometry
{
	/** Tolerance geometrique, en unites Unreal (cm). */
	constexpr double Epsilon = 1e-3;

	/**
	 * Repere du panneau : U et V portent la surface, W l'epaisseur (l'axe le plus
	 * court). 0 = X, 1 = Y, 2 = Z.
	 */
	BLOCKOUTTOOLS_API void GetFlatAxes(const FVector& Size, int32& OutUAxis, int32& OutVAxis, int32& OutWAxis);

	/** Contour rectangulaire deduit des dimensions (panneau non dessine a main levee). */
	BLOCKOUTTOOLS_API TArray<FVector2D> RectangleContour(const FVector& Size, int32 UAxis, int32 VAxis);

	/**
	 * Triangule un contour exterieur perce d'un nombre quelconque de trous simples
	 * et disjoints (portes, fenetres... percees dans un meme mur).
	 *
	 * Methode : decomposition en tranches horizontales. Les frontieres des tranches
	 * sont toutes les ordonnees de sommets ; a l'interieur d'une tranche aucun sommet
	 * ne se trouve, donc chaque region interieure y est un simple trapeze, obtenu en
	 * triant les aretes qui la traversent et en les appariant deux a deux (pair/impair).
	 *
	 * Volontairement plus terre-a-terre qu'un ear-clipping avec ponts entre les trous :
	 * le pontage produit un polygone "faiblement simple" sur lequel l'ear-clipping
	 * echoue des que deux trous sont proches ou alignes, alors que la decomposition en
	 * tranches ne peut pas echouer. Elle genere un peu plus de triangles, ce qui n'a
	 * aucune importance pour un mur de blockout.
	 *
	 * Renvoie les sommets a plat : 3 sommets consecutifs = 1 triangle.
	 */
	BLOCKOUTTOOLS_API TArray<FVector2D> TriangulateFace(const TArray<FVector2D>& Outer, const TArray<TArray<FVector2D>>& Holes);

	/**
	 * Construit le maillage complet : les deux faces, la tranche du contour exterieur
	 * et celle de chaque trou. Renvoie false si la surface est degeneree -- l'appelant
	 * doit alors renoncer plutot que d'ecrire un mesh vide.
	 */
	BLOCKOUTTOOLS_API bool BuildPanelMesh(const TArray<FVector2D>& Outer, double Thickness,
		int32 UAxis, int32 VAxis, int32 WAxis, const TArray<TArray<FVector2D>>& Holes,
		FBlockoutPanelMesh& OutMesh);

	// ── Validation de contours ────────────────────────────────────────────────

	/**
	 * Vrai si le contour ferme est simple : au moins 3 sommets, aucun sommet duplique
	 * et aucune paire d'aretes non adjacentes qui se croisent. Un contour
	 * auto-intersectant produit une surface aberrante : mieux vaut le refuser en amont
	 * et dire pourquoi a l'utilisateur.
	 */
	BLOCKOUTTOOLS_API bool IsSimplePolygon(const TArray<FVector2D>& Points);

	/** Vrai si le point est a l'interieur du contour ferme (lancer de rayon). */
	BLOCKOUTTOOLS_API bool PointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon);

	/**
	 * Vrai si deux contours fermes se chevauchent : aretes qui se croisent, ou l'un
	 * entierement contenu dans l'autre. Sert a refuser un trou qui empieterait sur un
	 * trou deja perce (la decomposition en tranches suppose des trous disjoints).
	 */
	BLOCKOUTTOOLS_API bool PolygonsOverlap(const TArray<FVector2D>& A, const TArray<FVector2D>& B);

	/**
	 * Vrai si "Inner" est entierement contenu dans "Outer", sans le toucher ni s'en
	 * approcher a moins de "Margin". Affleurer le bord du panneau ferait fusionner le
	 * trou avec le contour exterieur, et le chant genere autour du trou ferait double
	 * emploi avec celui du panneau.
	 */
	BLOCKOUTTOOLS_API bool ContainsWithMargin(const TArray<FVector2D>& Outer, const TArray<FVector2D>& Inner, double Margin);

	/** Distance du point au BORD du contour ferme (pas a son interieur). */
	BLOCKOUTTOOLS_API double DistanceToLoop(const FVector2D& P, const TArray<FVector2D>& Loop);

	/** Aire signee du contour ferme (positive si sens trigonometrique). */
	BLOCKOUTTOOLS_API double SignedArea(const TArray<FVector2D>& Loop);

	/**
	 * Contraint "Candidate" pour que le segment [Last -> resultat] soit parallele OU
	 * perpendiculaire a "FrameDir". Sert au trace a angles droits (Ctrl en mode dessin).
	 *
	 * Le repere est donne par le PREMIER segment du contour, pas par le precedent : un
	 * batiment peut donc etre trace en biais, et tous ses angles restent a 90 degres.
	 * Contraindre par rapport au segment precedent donnerait le meme resultat en theorie,
	 * mais chaque snap arrondirait la direction et l'erreur s'accumulerait le long du
	 * contour -- au bout de vingt murs le dernier ne serait plus d'aplomb.
	 *
	 * On garde la composante DOMINANTE : le segment suit celui des deux axes vers lequel
	 * la souris est le plus partie, ce qui est le geste attendu. Un repere degenere
	 * (direction nulle) laisse le candidat intact plutot que de renvoyer un point faux.
	 */
	BLOCKOUTTOOLS_API FVector2D SnapToOrthogonalFrame(const FVector2D& Last,
		const FVector2D& Candidate, const FVector2D& FrameDir, const FVector2D& IncomingDir);

	/**
	 * Deplace le DERNIER point pose pour que la fermeture retombe d'equerre (Ctrl+F).
	 *
	 * Le point est mis a l'intersection de deux droites : celle qui passe par
	 * l'avant-dernier point le long de l'axe U du repere, et celle qui passe par le
	 * premier point le long de l'axe V. Les deux angles que ce point commande -- le
	 * sien et celui du premier point -- sont alors droits par construction.
	 *
	 * Ce sont bien les axes du REPERE qui servent, pas la direction du dernier mur
	 * tracee a la souris : celle-ci n'est d'equerre que si Ctrl etait maintenu, et la
	 * conserver rendait un point qui ne fermait rien d'equerre des que le trace etait
	 * libre. La position ou l'utilisateur avait pose le dernier point n'entre donc pas
	 * dans le calcul -- c'est le sens de "ajuster pour fermer".
	 *
	 * Sur un contour a 4 points traces d'equerre, cela revient exactement a
	 * P0 + (P2 - P1) -- le coin qui complete le rectangle.
	 *
	 * Renvoie false plutot qu'un point faux si les droites sont paralleles, si le
	 * repere ou le dernier mur est degenere, ou si l'ajustement ecraserait un mur a
	 * une longueur nulle. L'appelant doit alors laisser le contour intact.
	 */
	BLOCKOUTTOOLS_API bool SnapLastPointToCloseFrame(const FVector2D& First, const FVector2D& FrameDir,
		const FVector2D& BeforeLast, FVector2D& OutAdjusted);
}
