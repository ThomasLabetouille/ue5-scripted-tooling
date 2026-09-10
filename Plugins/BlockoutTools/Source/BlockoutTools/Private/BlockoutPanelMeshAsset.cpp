#include "BlockoutPanelMeshAsset.h"
#include "BlockoutPanelGeometry.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"

#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "CoreGlobals.h"                 // GUndo
#include "Misc/ITransaction.h"
#include "UObject/UObjectHash.h"         // ForEachObjectWithOuter
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlockoutPanelMesh, Log, All);

namespace
{
	/**
	 * Nom de package libre : <Folder>/<Base>, puis <Base>_1, <Base>_2... On evite ainsi
	 * d'ecraser silencieusement le mesh d'un panneau existant, ce qui deformerait un
	 * acteur deja pose ailleurs dans le niveau.
	 */
	FString BlkFindFreePackageName(const FString& Folder, const FString& BaseName)
	{
		FString Candidate = Folder / BaseName;
		if (!FPackageName::DoesPackageExist(Candidate) && !FindPackage(nullptr, *Candidate))
		{
			return Candidate;
		}

		for (int32 Suffix = 1; Suffix < 10000; ++Suffix)
		{
			Candidate = FString::Printf(TEXT("%s/%s_%d"), *Folder, *BaseName, Suffix);
			if (!FPackageName::DoesPackageExist(Candidate) && !FindPackage(nullptr, *Candidate))
			{
				return Candidate;
			}
		}

		return FString();
	}

	/**
	 * Conversion listes brutes -> FMeshDescription.
	 *
	 * AUCUN sommet n'est partage entre deux triangles : chaque coin a son propre
	 * FVertexID. C'est deliberé. Le panneau est emis recto ET verso (deux triangles
	 * exactement superposes, d'enroulements opposes) ; si les sommets etaient partages,
	 * le calcul de normales du moteur les moyennerait et rendrait une normale nulle sur
	 * toute la surface. Les normales sont donc posees explicitement ici, avec la
	 * convention du moteur (systeme gaucher, enroulement anti-horaire :
	 * Normal = Cross(P2 - P0, P1 - P0), cf. StaticMeshOperations.cpp), et le
	 * recalcul de normales est desactive au build.
	 */
	bool BlkFillMeshDescription(const FBlockoutPanelMesh& PanelMesh, FMeshDescription& MeshDesc)
	{
		if (PanelMesh.Triangles.Num() < 3) return false;

		FStaticMeshAttributes Attributes(MeshDesc);
		Attributes.Register();

		TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector2f> InstanceUVs = Attributes.GetVertexInstanceUVs();
		TPolygonGroupAttributesRef<FName> GroupNames = Attributes.GetPolygonGroupMaterialSlotNames();

		InstanceUVs.SetNumChannels(1);

		const int32 NumTriangles = PanelMesh.TriangleCount();
		MeshDesc.ReserveNewVertices(NumTriangles * 3);
		MeshDesc.ReserveNewVertexInstances(NumTriangles * 3);
		MeshDesc.ReserveNewTriangles(NumTriangles);
		MeshDesc.ReserveNewPolygonGroups(1);

		const FPolygonGroupID GroupID = MeshDesc.CreatePolygonGroup();
		GroupNames[GroupID] = FName(TEXT("BlockoutPanel"));

		int32 EmittedTriangles = 0;

		for (int32 T = 0; T + 2 < PanelMesh.Triangles.Num(); T += 3)
		{
			const int32 Index[3] = { PanelMesh.Triangles[T], PanelMesh.Triangles[T + 1], PanelMesh.Triangles[T + 2] };
			if (!PanelMesh.Vertices.IsValidIndex(Index[0]) ||
				!PanelMesh.Vertices.IsValidIndex(Index[1]) ||
				!PanelMesh.Vertices.IsValidIndex(Index[2]))
			{
				continue;
			}

			const FVector3f P0 = PanelMesh.Vertices[Index[0]];
			const FVector3f P1 = PanelMesh.Vertices[Index[1]];
			const FVector3f P2 = PanelMesh.Vertices[Index[2]];

			const FVector3f Normal = FVector3f::CrossProduct(P2 - P0, P1 - P0).GetSafeNormal();
			if (Normal.IsNearlyZero())
			{
				continue;   // triangle degenere : ne rien emettre plutot qu'une normale nulle
			}

			FVertexInstanceID Corners[3];
			for (int32 K = 0; K < 3; ++K)
			{
				const FVertexID VertexID = MeshDesc.CreateVertex();
				VertexPositions[VertexID] = PanelMesh.Vertices[Index[K]];

				const FVertexInstanceID InstanceID = MeshDesc.CreateVertexInstance(VertexID);
				InstanceNormals[InstanceID] = Normal;
				InstanceUVs.Set(InstanceID, 0, PanelMesh.UVs.IsValidIndex(Index[K])
					? PanelMesh.UVs[Index[K]]
					: FVector2f::ZeroVector);

				Corners[K] = InstanceID;
			}

			MeshDesc.CreateTriangle(GroupID, Corners);
			++EmittedTriangles;
		}

		return EmittedTriangles > 0;
	}
}

namespace BlockoutPanelMeshAsset
{

const TCHAR* DefaultPackageFolder()
{
	return TEXT("/Game/Blockout/GeneratedMeshes");
}

UStaticMesh* CreateStaticMeshAsset(
	const FBlockoutPanelMesh& PanelMesh,
	const FString& PackageFolder,
	const FString& BaseAssetName,
	UMaterialInterface* Material,
	FString& OutError)
{
	OutError.Reset();

	if (PanelMesh.IsEmpty())
	{
		OutError = TEXT("maillage vide (contour degenere ou auto-intersectant)");
		return nullptr;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Un asset ne doit JAMAIS etre enregistre dans une transaction d'undo.
	//
	// Bug reel du 2026-08-26 : les assets etaient crees a l'interieur du
	// FScopedTransaction du mode Dessin. Les sous-objets
	// UStaticMeshDescriptionBulkData d'un UStaticMesh sont RF_Transactional ; un
	// Ctrl+Z les ramenait a leur etat d'avant la transaction -- c'est-a-dire
	// inexistants. Le mesh survivait avec un sous-objet nul, et le premier autosave
	// qui le touchait faisait tomber l'editeur sur
	// `check(StaticMeshDescriptionBulkData != nullptr)`, 56 secondes plus tard, sans
	// aucun lien apparent avec l'undo.
	//
	// GUndo a nullptr desactive l'enregistrement : SaveToTransactionBuffer est garde
	// par `if (GUndo && bIsTransactional && ...)`. C'est une ceinture ; les bretelles
	// sont plus bas (ClearFlags) et chez l'appelant, qui construit desormais les
	// assets AVANT d'ouvrir sa transaction.
	TGuardValue<ITransaction*> SuppressTransactionRecording(GUndo, nullptr);

	const FString Folder = PackageFolder.IsEmpty() ? FString(DefaultPackageFolder()) : PackageFolder;
	const FString SafeBase = BaseAssetName.IsEmpty() ? TEXT("SM_BlockoutPanel") : BaseAssetName;

	const FString PackageName = BlkFindFreePackageName(Folder, SafeBase);
	if (PackageName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("impossible de trouver un nom libre sous %s"), *Folder);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("CreatePackage a echoue pour %s"), *PackageName);
		return nullptr;
	}
	Package->FullyLoad();

	const FString AssetName = FPackageName::GetShortName(PackageName);

	UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("NewObject<UStaticMesh> a echoue pour %s"), *PackageName);
		return nullptr;
	}

	// Pas d'InitResources() ici : UStaticMesh::Build() initialise les ressources de
	// rendu lui-meme, et l'appeler sur un mesh encore sans donnees peut assert.
	Mesh->SetLightingGuid();

	FStaticMeshSourceModel& SourceModel = Mesh->AddSourceModel();
	// Normales posees a la main (voir BlkFillMeshDescription) : les recalculer
	// moyennerait recto et verso et donnerait une normale nulle.
	SourceModel.BuildSettings.bRecomputeNormals = false;
	SourceModel.BuildSettings.bRecomputeTangents = true;
	SourceModel.BuildSettings.bUseMikkTSpace = true;
	SourceModel.BuildSettings.bRemoveDegenerates = true;
	// Pas d'UV de lightmap : ce sont des volumes de blockout, remplaces par de l'art
	// final avant tout bake. Les generer sur un maillage recto-verso produit surtout
	// des avertissements de chevauchement.
	SourceModel.BuildSettings.bGenerateLightmapUVs = false;

	FMeshDescription* MeshDesc = Mesh->CreateMeshDescription(0);
	if (!MeshDesc || !BlkFillMeshDescription(PanelMesh, *MeshDesc))
	{
		OutError = TEXT("conversion vers FMeshDescription impossible (aucun triangle valide)");
		return nullptr;
	}
	Mesh->CommitMeshDescription(0);

	UMaterialInterface* AppliedMaterial = Material;
	if (!AppliedMaterial)
	{
		AppliedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	Mesh->GetStaticMaterials().Add(FStaticMaterial(AppliedMaterial, TEXT("BlockoutPanel"), TEXT("BlockoutPanel")));

	// Collision : un mur perce d'une porte doit se TRAVERSER par la porte. Une collision
	// simple (boite englobante) reboucherait le trou -- le joueur se cognerait dans une
	// ouverture visiblement ouverte, et le navmesh la considererait pleine.
	// Pose AVANT Build() : c'est Build() qui cuisine les donnees de collision, le regler
	// apres coup obligerait a tout invalider et recuire.
	Mesh->CreateBodySetup();
	if (UBodySetup* BodySetup = Mesh->GetBodySetup())
	{
		BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	}

	Mesh->Build(/*bInSilent=*/true);
	Mesh->PostEditChange();

	// Bretelles : meme si un jour cette fonction etait rappelee depuis une
	// transaction ouverte, l'asset et ses sous-objets ne seront plus candidats a
	// l'undo. Un asset du Content Browser n'a rien a faire dans la pile d'annulation
	// du niveau -- Ctrl+Z doit retirer des ACTEURS, pas defaire un fichier sur disque.
	Mesh->ClearFlags(RF_Transactional);
	// Surcharge par defaut = EGetObjectsFlags::IncludeNestedObjects. La variante a
	// bool est depreciee en 5.8 : ne pas la reintroduire.
	ForEachObjectWithOuter(Mesh, [](UObject* Inner)
	{
		Inner->ClearFlags(RF_Transactional);
	});

	FAssetRegistryModule::AssetCreated(Mesh);
	Package->MarkPackageDirty();

	const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	SaveArgs.bSlowTask = false;

	if (!UPackage::SavePackage(Package, Mesh, *FileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("l'asset %s a ete cree mais n'a pas pu etre ecrit sur disque"), *PackageName);
		UE_LOG(LogBlockoutPanelMesh, Warning, TEXT("BlockoutPanelMeshAsset: %s"), *OutError);
		// L'asset existe en memoire : on le renvoie quand meme, avec l'erreur remontee.
		return Mesh;
	}

	UE_LOG(LogBlockoutPanelMesh, Log, TEXT("BlockoutPanelMeshAsset: %s ecrit (%d triangles)"),
		*PackageName, PanelMesh.TriangleCount());

	return Mesh;
}

}   // namespace BlockoutPanelMeshAsset
