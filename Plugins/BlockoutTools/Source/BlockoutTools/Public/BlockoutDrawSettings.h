#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BlockoutDrawSettings.generated.h"

/**
 * Reglages du mode "Dessin libre", partages entre le panneau Outil Blockout (ou on les
 * saisit) et UBlockoutDrawMode (qui les lit pendant le dessin dans le viewport).
 *
 * Sauvegardes dans EditorPerProjectUserSettings : le level designer retrouve son
 * epaisseur de mur et sa hauteur d'etage d'une session a l'autre, comme pour les autres
 * champs du panneau (voir la persistance GEditorPerProjectIni de SBlockoutToolPanel).
 *
 * Toutes les longueurs sont en unites Unreal (cm).
 */
UCLASS(config = EditorPerProjectUserSettings)
class BLOCKOUTTOOLS_API UBlockoutDrawSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Prefixe des acteurs generes et du dossier cree dans l'Outliner. */
	UPROPERTY(config, EditAnywhere, Category = "Dessin libre")
	FString RoomName = TEXT("Salle_Dessin");

	/** Altitude du plan de dessin. Les points cliques s'y posent. */
	UPROPERTY(config, EditAnywhere, Category = "Dessin libre")
	float GroundZ = 0.f;

	/** Hauteur d'extrusion, reglee a la souris apres fermeture du contour. */
	UPROPERTY(config, EditAnywhere, Category = "Dessin libre")
	float Height = 300.f;

	/** Epaisseur des murs, du sol et du plafond. */
	UPROPERTY(config, EditAnywhere, Category = "Dessin libre")
	float WallThickness = 20.f;

	/** Extrusion vers le haut (true) ou vers le bas (false). */
	UPROPERTY(config, EditAnywhere, Category = "Dessin libre")
	bool bExtrudeUp = true;

	UPROPERTY(config, EditAnywhere, Category = "Dessin libre")
	bool bAddFloor = true;

	UPROPERTY(config, EditAnywhere, Category = "Dessin libre")
	bool bAddCeiling = false;

	/** Instance unique, chargee depuis le .ini du projet. */
	static UBlockoutDrawSettings* Get();
};
