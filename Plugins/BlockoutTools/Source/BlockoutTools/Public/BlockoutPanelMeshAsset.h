#pragma once

#include "CoreMinimal.h"

struct FBlockoutPanelMesh;
class UStaticMesh;
class UMaterialInterface;

/**
 * Ecriture d'un FBlockoutPanelMesh (listes brutes, sans API moteur) vers un vrai asset
 * UStaticMesh sur disque.
 *
 * C'est la SEULE frontiere entre le noyau geometrique et Unreal. Tout ce qui touche a
 * un package, a un FMeshDescription ou a un BodySetup vit ici, exactement comme cote
 * Unity ou RoomBuilderWindow gardait pour lui l'ecriture du Mesh, la cuisson du
 * collider et l'undo.
 *
 * Pourquoi un ASSET et pas un maillage en memoire : un mesh cree a la volee n'est pas
 * serialise avec le niveau. Il survit a la session en cours, puis disparait a la
 * reouverture. La version Unity a eu exactement ce probleme (voir SaveMeshAsset).
 */
namespace BlockoutPanelMeshAsset
{
	/**
	 * Cree un asset UStaticMesh a partir du maillage fourni, sous
	 * <PackageFolder>/<BaseAssetName> (suffixe automatiquement si le nom est deja pris),
	 * et le sauvegarde sur disque.
	 *
	 * Le materiau est facultatif : nullptr = materiau de base d'Unreal, celui deja
	 * utilise par les cubes de blockout.
	 *
	 * Renvoie nullptr et remplit OutError en cas d'echec (maillage vide, package
	 * impossible a creer, sauvegarde refusee).
	 */
	BLOCKOUTTOOLS_API UStaticMesh* CreateStaticMeshAsset(
		const FBlockoutPanelMesh& PanelMesh,
		const FString& PackageFolder,
		const FString& BaseAssetName,
		UMaterialInterface* Material,
		FString& OutError);

	/** Dossier par defaut des meshes generes par l'outil. */
	BLOCKOUTTOOLS_API const TCHAR* DefaultPackageFolder();
}
