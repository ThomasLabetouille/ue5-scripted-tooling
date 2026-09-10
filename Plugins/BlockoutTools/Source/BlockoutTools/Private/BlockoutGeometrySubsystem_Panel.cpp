#include "BlockoutGeometrySubsystem.h"
#include "BlockoutPanelGeometry.h"
#include "BlockoutPanelMeshAsset.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSourceData.h"

// BlockoutGeometrySubsystem_Panel.cpp -- pont Python/Blueprint vers le noyau pur
// BlockoutPanelGeometry. AUCUNE logique ici : uniquement de la conversion de types.
// Toute regle geometrique doit rester dans BlockoutPanelGeometry.cpp, ou elle est
// testable sans editeur.

namespace
{
	TArray<TArray<FVector2D>> BlkPanelHolesToContours(const TArray<FBlockoutPanelHole>& Holes)
	{
		TArray<TArray<FVector2D>> Contours;
		Contours.Reserve(Holes.Num());
		for (const FBlockoutPanelHole& Hole : Holes)
		{
			if (Hole.Points.Num() >= 3)
			{
				Contours.Add(Hole.Points);
			}
		}
		return Contours;
	}
}

TArray<FVector2D> UBlockoutGeometrySubsystem::TriangulatePanelFace(
	const TArray<FVector2D>& Outer, const TArray<FBlockoutPanelHole>& Holes)
{
	return BlockoutPanelGeometry::TriangulateFace(Outer, BlkPanelHolesToContours(Holes));
}

bool UBlockoutGeometrySubsystem::BuildPanelMeshForTest(
	const TArray<FVector2D>& Outer, float Thickness, const TArray<FBlockoutPanelHole>& Holes,
	int32 UAxis, int32 VAxis, int32 WAxis,
	TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector2D>& OutUVs)
{
	OutVertices.Reset();
	OutTriangles.Reset();
	OutUVs.Reset();

	FBlockoutPanelMesh Mesh;
	if (!BlockoutPanelGeometry::BuildPanelMesh(Outer, (double)Thickness,
		FMath::Clamp(UAxis, 0, 2), FMath::Clamp(VAxis, 0, 2), FMath::Clamp(WAxis, 0, 2),
		BlkPanelHolesToContours(Holes), Mesh))
	{
		return false;
	}

	OutVertices.Reserve(Mesh.Vertices.Num());
	for (const FVector3f& V : Mesh.Vertices)
	{
		OutVertices.Add(FVector(V));
	}

	OutUVs.Reserve(Mesh.UVs.Num());
	for (const FVector2f& UV : Mesh.UVs)
	{
		OutUVs.Add(FVector2D(UV));
	}

	OutTriangles = Mesh.Triangles;
	return true;
}

bool UBlockoutGeometrySubsystem::IsSimpleContour(const TArray<FVector2D>& Points)
{
	return BlockoutPanelGeometry::IsSimplePolygon(Points);
}

float UBlockoutGeometrySubsystem::ContourSignedArea(const TArray<FVector2D>& Loop)
{
	return (float)BlockoutPanelGeometry::SignedArea(Loop);
}

bool UBlockoutGeometrySubsystem::PointInContour(FVector2D Point, const TArray<FVector2D>& Polygon)
{
	return BlockoutPanelGeometry::PointInPolygon(Point, Polygon);
}

bool UBlockoutGeometrySubsystem::ContoursOverlap(const TArray<FVector2D>& A, const TArray<FVector2D>& B)
{
	return BlockoutPanelGeometry::PolygonsOverlap(A, B);
}

bool UBlockoutGeometrySubsystem::ContourContainsWithMargin(
	const TArray<FVector2D>& Outer, const TArray<FVector2D>& Inner, float Margin)
{
	return BlockoutPanelGeometry::ContainsWithMargin(Outer, Inner, (double)Margin);
}

// ═══════════════════════════════════════════════════════════════════════════
// Ecriture d'asset et sonde de sante -- ajoutes le 2026-08-26 apres le crash
// "StaticMeshDescriptionBulkData != nullptr". Le chemin d'ecriture d'asset
// n'etait exerce que par un vrai clic dans le viewport : aucun test ne pouvait
// voir qu'un Ctrl+Z le laissait dans un etat qui tuerait l'editeur plus tard.
// ═══════════════════════════════════════════════════════════════════════════

UStaticMesh* UBlockoutGeometrySubsystem::CreatePanelMeshAsset(
	const TArray<FVector2D>& Outer, float Thickness, const TArray<FBlockoutPanelHole>& Holes,
	const FString& AssetBaseName, const FString& PackageFolder, FString& OutError)
{
	OutError.Reset();

	FBlockoutPanelMesh Mesh;
	if (!BlockoutPanelGeometry::BuildPanelMesh(Outer, (double)Thickness, 0, 1, 2,
		BlkPanelHolesToContours(Holes), Mesh))
	{
		OutError = TEXT("triangulation impossible (contour degenere ou auto-intersectant)");
		return nullptr;
	}

	return BlockoutPanelMeshAsset::CreateStaticMeshAsset(
		Mesh,
		PackageFolder.IsEmpty() ? FString(BlockoutPanelMeshAsset::DefaultPackageFolder()) : PackageFolder,
		AssetBaseName.IsEmpty() ? TEXT("SM_BlockoutPanel") : AssetBaseName,
		nullptr,
		OutError);
}

FString UBlockoutGeometrySubsystem::GetPanelMeshHealthProblem(UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return TEXT("mesh nul");
	}

	const int32 NumLods = Mesh->GetNumSourceModels();
	if (NumLods <= 0)
	{
		return TEXT("aucun source model");
	}

	for (int32 Index = 0; Index < NumLods; ++Index)
	{
		if (!Mesh->GetSourceModel(Index).IsSourceModelInitialized())
		{
			return FString::Printf(
				TEXT("source model %d non initialise (StaticMeshDescriptionBulkData nul) -- "
				     "l'editeur tomberait au prochain autosave"), Index);
		}
	}

	if (!Mesh->GetHiResSourceModel().IsSourceModelInitialized())
	{
		return TEXT("source model HiRes (Nanite) non initialise -- "
		            "interroge par le calcul de cle DDC a chaque build/PostLoad");
	}

	return FString();
}

FVector2D UBlockoutGeometrySubsystem::SnapToOrthogonalFrame(FVector2D Last, FVector2D Candidate,
	FVector2D FrameDir, FVector2D IncomingDir) const
{
	return BlockoutPanelGeometry::SnapToOrthogonalFrame(Last, Candidate, FrameDir, IncomingDir);
}

void UBlockoutGeometrySubsystem::SnapLastPointToCloseFrame(FVector2D First, FVector2D FrameDir,
	FVector2D BeforeLast, FVector2D& OutAdjusted, bool& bOutOk) const
{
	OutAdjusted = BeforeLast;
	bOutOk = BlockoutPanelGeometry::SnapLastPointToCloseFrame(First, FrameDir, BeforeLast, OutAdjusted);
}
