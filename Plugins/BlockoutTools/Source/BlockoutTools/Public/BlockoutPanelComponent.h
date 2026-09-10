#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BlockoutPanelComponent.generated.h"

/**
 * Un trou perce dans le panneau, decrit par son contour ferme exprime dans le repere
 * (U,V) du panneau (unites Unreal, origine au centre du panneau).
 */
USTRUCT(BlueprintType)
struct FBlockoutPanelHole
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blockout")
	TArray<FVector2D> Points;
};

/**
 * Etat LOGIQUE d'un panneau (mur / sol / plafond) genere par l'Outil Blockout.
 *
 * Le mesh affiche n'est qu'une CONSEQUENCE de cet etat : il est entierement reconstruit
 * a partir du contour et de la liste des trous. C'est ce qui permet de decouper
 * plusieurs fois le meme mur -- sans cet etat, une fois le cube d'origine remplace par
 * un mesh troue, l'outil n'a plus aucun moyen de retrouver ni les dimensions du panneau
 * (l'echelle est remise a 1 par la decoupe) ni les trous deja perces.
 *
 * PORTAGE de LevelDesignTools (Unity) : Assets/LevelDesignTools/RoomPanel.cs.
 *
 * bIsEditorOnly : ce composant vit dans un module Editor. Sans ce drapeau, un niveau
 * cuisine porterait une reference a une classe absente du build -- il est donc retire
 * automatiquement a la cuisson, comme n'importe quel composant d'edition.
 */
UCLASS(ClassGroup = (Blockout), meta = (DisplayName = "Blockout Panel"))
class BLOCKOUTTOOLS_API UBlockoutPanelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBlockoutPanelComponent();

	/** Dimensions reelles du panneau sur X, Y et Z (unites Unreal). L'echelle de l'acteur reste a 1. */
	UPROPERTY(EditAnywhere, Category = "Blockout")
	FVector PanelSize = FVector(100.f, 100.f, 20.f);

	/** Axe local correspondant a la largeur (U) : 0 = X, 1 = Y, 2 = Z. */
	UPROPERTY(EditAnywhere, Category = "Blockout")
	int32 UAxis = 0;

	/** Axe local correspondant a la hauteur (V) : 0 = X, 1 = Y, 2 = Z. */
	UPROPERTY(EditAnywhere, Category = "Blockout")
	int32 VAxis = 1;

	/** Axe local correspondant a l'epaisseur (W) : 0 = X, 1 = Y, 2 = Z. */
	UPROPERTY(EditAnywhere, Category = "Blockout")
	int32 WAxis = 2;

	/**
	 * Contour exterieur du panneau en coordonnees (U,V). VIDE = rectangle deduit de
	 * PanelSize. C'est ce qui permet a un mur rectangulaire ordinaire de devenir un
	 * panneau sans avoir a lui ecrire ses 4 coins.
	 */
	UPROPERTY(EditAnywhere, Category = "Blockout")
	TArray<FVector2D> Outer;

	/** Trous perces, en coordonnees (U,V) locales. */
	UPROPERTY(EditAnywhere, Category = "Blockout")
	TArray<FBlockoutPanelHole> Holes;

	/** Chemin de l'asset UStaticMesh genere pour ce panneau (trace, pour le debug). */
	UPROPERTY(VisibleAnywhere, Category = "Blockout")
	FString GeneratedMeshPath;

	double GetThickness() const;

	/**
	 * Contour exterieur resolu : le contour libre s'il existe, sinon le rectangle
	 * decrit par PanelSize.
	 */
	TArray<FVector2D> ResolveOuterContour() const;

	/** Contours des trous, sous la forme attendue par BlockoutPanelGeometry. */
	TArray<TArray<FVector2D>> ResolveHoleContours() const;
};
