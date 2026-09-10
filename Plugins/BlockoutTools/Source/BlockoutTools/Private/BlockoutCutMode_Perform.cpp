#include "BlockoutCutMode.h"
#include "BlockoutPanelGeometry.h"
#include "BlockoutPanelComponent.h"
#include "BlockoutPanelMeshAsset.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "BlockoutCutMode"

DEFINE_LOG_CATEGORY_STATIC(LogBlockoutCut, Log, All);

namespace
{
	/** Marge exigee entre le contour du trou et le bord du panneau. Valeur portee d'Unity. */
	constexpr double BlkCutMargin = 0.001;

	/** Tolerance du test de boite, relative a la taille du mesh. */
	constexpr double BlkBoxTolerance = 1e-3;

	UStaticMeshComponent* FindMeshComponent(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// Validation de la cible
// ═══════════════════════════════════════════════════════════════════════════

bool UBlockoutCutMode::IsBoxMesh(UStaticMesh* Mesh)
{
	if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
	{
		return false;
	}

	const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
	const int32 NumVerts = (int32)Positions.GetNumVertices();
	if (NumVerts == 0)
	{
		return false;
	}

	const FBox Bounds = Mesh->GetBoundingBox();
	const FVector Extent = Bounds.GetExtent();
	const double Tol = FMath::Max(Extent.GetMax(), 1.0) * BlkBoxTolerance;

	FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
	const int32 NumIndices = Indices.Num();
	if (NumIndices < 3)
	{
		return false;
	}

	// Test PAR TRIANGLE, pas par sommet.
	//
	// Porte tel quel du bug rencontre cote Unity : un cylindre passe le test par sommets, tous
	// les siens etant poses sur les plans du haut et du bas ; le decouper l'aurait remplace par
	// une boite. Une face de boite, meme subdivisee, reste entierement dans SON plan -- la paroi
	// d'un cylindre, elle, relie deux plans sans reposer sur aucun.
	for (int32 i = 0; i + 2 < NumIndices; i += 3)
	{
		const FVector A(Positions.VertexPosition(Indices[i]));
		const FVector B(Positions.VertexPosition(Indices[i + 1]));
		const FVector C(Positions.VertexPosition(Indices[i + 2]));

		bool bOnSomeFace = false;
		for (int32 Axis = 0; Axis < 3 && !bOnSomeFace; ++Axis)
		{
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const double Plane = (Side == 0) ? Bounds.Min[Axis] : Bounds.Max[Axis];
				if (FMath::Abs(A[Axis] - Plane) <= Tol &&
					FMath::Abs(B[Axis] - Plane) <= Tol &&
					FMath::Abs(C[Axis] - Plane) <= Tol)
				{
					bOnSomeFace = true;
					break;
				}
			}
		}

		if (!bOnSomeFace)
		{
			return false;
		}
	}

	return true;
}

bool UBlockoutCutMode::IsValidCutTarget(AActor* Target, FString& OutReason)
{
	OutReason.Reset();

	if (!Target)
	{
		OutReason = TEXT("aucune cible sous le curseur.");
		return false;
	}

	// Un panneau deja decoupe est toujours une cible valable : son etat logique fait foi.
	if (Target->FindComponentByClass<UBlockoutPanelComponent>() != nullptr)
	{
		return true;
	}

	UStaticMeshComponent* const Comp = FindMeshComponent(Target);
	if (!Comp || !Comp->GetStaticMesh())
	{
		OutReason = TEXT("cet objet n'a pas de maillage statique (ce n'est ni un mur, ni un sol, ni un plafond).");
		return false;
	}

	if (IsBoxMesh(Comp->GetStaticMesh()))
	{
		return true;
	}

	OutReason = FString::Printf(
		TEXT("le maillage \"%s\" n'a pas la forme d'une boite et ne porte pas de Blockout Panel. ")
		TEXT("La decoupe reconstruit un panneau plat extrude : appliquee a une forme quelconque, ")
		TEXT("elle la remplacerait par une boite trouee."),
		*Comp->GetStaticMesh()->GetName());
	return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Description de la surface
// ═══════════════════════════════════════════════════════════════════════════

bool UBlockoutCutMode::TryDescribeCutSurface(AActor* Target, FBlockoutCutSurface& OutSurface)
{
	OutSurface = FBlockoutCutSurface();

	FString Ignored;
	if (!IsValidCutTarget(Target, Ignored))
	{
		return false;
	}

	UStaticMeshComponent* const Comp = FindMeshComponent(Target);
	if (!Comp || !Comp->GetStaticMesh())
	{
		return false;
	}

	OutSurface.Actor = Target;
	OutSurface.Panel = Target->FindComponentByClass<UBlockoutPanelComponent>();
	OutSurface.Scale = Target->GetActorScale3D();

	// Taille du panneau dans le monde = taille du mesh multipliee par l'echelle du transform.
	// La formule couvre les deux cas sans distinction : boite brute (echelle portant les
	// dimensions) et panneau deja decoupe (mesh a taille reelle, echelle 1 -- ou remise a
	// l'echelle a la main entre deux decoupes, auquel cas on la "cuit").
	FVector MeshSize;
	if (OutSurface.Panel)
	{
		MeshSize = OutSurface.Panel->PanelSize;
		OutSurface.LocalCenter = FVector::ZeroVector;
	}
	else
	{
		const FBox Box = Comp->GetStaticMesh()->GetBoundingBox();
		MeshSize = Box.GetSize();
		OutSurface.LocalCenter = Box.GetCenter();
	}

	OutSurface.PanelSize = MeshSize * OutSurface.Scale;

	if (OutSurface.Panel)
	{
		// Repere conserve tel quel : les trous deja perces y sont exprimes.
		OutSurface.UAxis = OutSurface.Panel->UAxis;
		OutSurface.VAxis = OutSurface.Panel->VAxis;
		OutSurface.WAxis = OutSurface.Panel->WAxis;
		OutSurface.Outer = OutSurface.Panel->ResolveOuterContour();

		// Le contour stocke est en unites du panneau : le remettre a l'echelle courante.
		for (FVector2D& P : OutSurface.Outer)
		{
			P.X *= OutSurface.Scale[OutSurface.UAxis];
			P.Y *= OutSurface.Scale[OutSurface.VAxis];
		}
	}
	else
	{
		BlockoutPanelGeometry::GetFlatAxes(OutSurface.PanelSize,
			OutSurface.UAxis, OutSurface.VAxis, OutSurface.WAxis);
		OutSurface.Outer = BlockoutPanelGeometry::RectangleContour(
			OutSurface.PanelSize, OutSurface.UAxis, OutSurface.VAxis);
	}

	OutSurface.HalfW = FMath::Abs(OutSurface.PanelSize[OutSurface.WAxis]) * 0.5;
	return OutSurface.IsValid();
}

// ═══════════════════════════════════════════════════════════════════════════
// Projection d'un rayon sur la face visible
// ═══════════════════════════════════════════════════════════════════════════

bool UBlockoutCutMode::TryProjectOnSurface(const FBlockoutCutSurface& Surface,
	const FVector& RayOrigin, const FVector& RayDirection,
	FVector& OutWorldPoint, FVector2D& OutUV)
{
	OutWorldPoint = FVector::ZeroVector;
	OutUV = FVector2D::ZeroVector;

	if (!Surface.Actor)
	{
		return false;
	}

	const FTransform Xform = Surface.Actor->GetActorTransform();

	FVector AxisVec = FVector::ZeroVector;
	AxisVec[Surface.WAxis] = 1.f;
	const FVector Normal = Xform.TransformVectorNoScale(AxisVec).GetSafeNormal();

	// La face tournee vers la camera : celle dont la normale s'oppose au rayon.
	const double Side = (FVector::DotProduct(Normal, RayDirection) < 0.0) ? 1.0 : -1.0;

	FVector LocalFace = Surface.LocalCenter;
	const double ScaleW = FMath::Max(FMath::Abs(Surface.Scale[Surface.WAxis]), 1e-6);
	LocalFace[Surface.WAxis] += Side * Surface.HalfW / ScaleW;
	const FVector FacePoint = Xform.TransformPosition(LocalFace);

	const FVector PlaneNormal = Normal * Side;
	const double Denom = FVector::DotProduct(PlaneNormal, RayDirection);
	if (FMath::Abs(Denom) < 1e-8)
	{
		return false;
	}
	const double Distance = FVector::DotProduct(FacePoint - RayOrigin, PlaneNormal) / Denom;
	if (Distance <= 0.0)
	{
		return false;
	}

	OutWorldPoint = RayOrigin + RayDirection * Distance;

	const FVector LocalHit = Xform.InverseTransformPosition(OutWorldPoint) - Surface.LocalCenter;
	OutUV = FVector2D(LocalHit[Surface.UAxis] * Surface.Scale[Surface.UAxis],
					  LocalHit[Surface.VAxis] * Surface.Scale[Surface.VAxis]);

	if (!BlockoutPanelGeometry::PointInPolygon(OutUV, Surface.Outer))
	{
		return false;
	}

	// Cliquer DANS un trou deja perce n'a pas de sens : il n'y a plus de matiere.
	if (Surface.Panel)
	{
		for (const FBlockoutPanelHole& Hole : Surface.Panel->Holes)
		{
			if (Hole.Points.Num() < 3)
			{
				continue;
			}
			TArray<FVector2D> Scaled;
			Scaled.Reserve(Hole.Points.Num());
			for (const FVector2D& P : Hole.Points)
			{
				Scaled.Add(FVector2D(P.X * Surface.Scale[Surface.UAxis],
									 P.Y * Surface.Scale[Surface.VAxis]));
			}
			if (BlockoutPanelGeometry::PointInPolygon(OutUV, Scaled))
			{
				return false;
			}
		}
	}

	return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Reconstruction du maillage
// ═══════════════════════════════════════════════════════════════════════════

bool UBlockoutCutMode::RebuildPanelMesh(AActor* Target, UBlockoutPanelComponent* Panel,
	FString& OutError)
{
	OutError.Reset();

	UStaticMeshComponent* const Comp = FindMeshComponent(Target);
	if (!Comp || !Panel)
	{
		OutError = TEXT("panneau ou composant de maillage introuvable.");
		return false;
	}

	FBlockoutPanelMesh Mesh;
	const double Thickness = FMath::Abs(Panel->PanelSize[Panel->WAxis]);
	if (!BlockoutPanelGeometry::BuildPanelMesh(
			Panel->ResolveOuterContour(), Thickness,
			Panel->UAxis, Panel->VAxis, Panel->WAxis,
			Panel->ResolveHoleContours(), Mesh) || Mesh.IsEmpty())
	{
		OutError = TEXT("la triangulation du panneau a echoue.");
		return false;
	}

	UMaterialInterface* const Material = Comp->GetMaterial(0);
	FString AssetError;
	UStaticMesh* const NewMesh = BlockoutPanelMeshAsset::CreateStaticMeshAsset(
		Mesh, BlockoutPanelMeshAsset::DefaultPackageFolder(),
		Target->GetActorNameOrLabel() + TEXT("_Panel"), Material, AssetError);

	if (!NewMesh)
	{
		OutError = AssetError.IsEmpty() ? TEXT("creation de l'asset de maillage impossible.") : AssetError;
		return false;
	}

	Comp->Modify();
	Comp->SetStaticMesh(NewMesh);

	// Collision complexe utilisee comme simple : une collision simple reboucherait le trou et
	// l'on ne pourrait pas traverser la porte que l'on vient de percer.
	Comp->SetCollisionProfileName(TEXT("BlockAll"));
	Comp->BodyInstance.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// La decoupe
// ═══════════════════════════════════════════════════════════════════════════

void UBlockoutCutMode::PerformCut()
{
	AActor* const Target = CutTarget.Get();

	FString Reason;
	if (!IsValidCutTarget(Target, Reason))
	{
		StatusMessage = FString::Printf(TEXT("Decoupe annulee : %s"), *Reason);
		UE_LOG(LogBlockoutCut, Warning, TEXT("%s"), *StatusMessage);
		return;
	}

	if (CutPoints.Num() < 3)
	{
		StatusMessage = TEXT("Decoupe annulee : il faut au moins 3 points.");
		return;
	}

	FBlockoutCutSurface Surface;
	if (!TryDescribeCutSurface(Target, Surface))
	{
		StatusMessage = TEXT("Decoupe annulee : la cible n'est plus exploitable.");
		return;
	}

	const FTransform Xform = Target->GetActorTransform();

	// Contour du trou en coordonnees (U,V) du panneau, en unites monde.
	//
	// InverseTransformPosition rend des coordonnees en espace MESH ; les remultiplier par
	// l'echelle donne bien une position en unites monde, coherente avec le contour exterieur.
	// Sans cela le trou est "echelle" fois trop petit et ramene au centre -- bug reel cote Unity.
	TArray<FVector2D> HolePoints;
	HolePoints.Reserve(CutPoints.Num());
	for (const FVector& WorldPoint : CutPoints)
	{
		const FVector Local = Xform.InverseTransformPosition(WorldPoint) - Surface.LocalCenter;
		HolePoints.Add(FVector2D(Local[Surface.UAxis] * Surface.Scale[Surface.UAxis],
								 Local[Surface.VAxis] * Surface.Scale[Surface.VAxis]));
	}

	if (!BlockoutPanelGeometry::IsSimplePolygon(HolePoints))
	{
		StatusMessage = TEXT("Le contour se recoupe lui-meme (ou deux points sont confondus). Redessine un contour simple.");
		return;
	}

	if (!BlockoutPanelGeometry::ContainsWithMargin(Surface.Outer, HolePoints, BlkCutMargin))
	{
		StatusMessage = TEXT("Le contour sort de la cible ou en touche le bord. Redessine-le entierement a l'interieur.");
		return;
	}

	// Trous deja perces, reexprimes dans l'echelle courante.
	TArray<TArray<FVector2D>> PreviousHoles;
	if (Surface.Panel)
	{
		for (const FBlockoutPanelHole& Hole : Surface.Panel->Holes)
		{
			if (Hole.Points.Num() < 3)
			{
				continue;
			}
			TArray<FVector2D> Scaled;
			Scaled.Reserve(Hole.Points.Num());
			for (const FVector2D& P : Hole.Points)
			{
				Scaled.Add(FVector2D(P.X * Surface.Scale[Surface.UAxis],
									 P.Y * Surface.Scale[Surface.VAxis]));
			}
			PreviousHoles.Add(MoveTemp(Scaled));
		}
	}

	for (const TArray<FVector2D>& Previous : PreviousHoles)
	{
		if (BlockoutPanelGeometry::PolygonsOverlap(HolePoints, Previous))
		{
			StatusMessage = TEXT("Ce contour chevauche un trou deja perce. Dessine une ouverture separee, ou retire d'abord l'existante.");
			return;
		}
	}

	// ── Tout est valide : a partir d'ici seulement, on modifie ────────────────
	const FScopedTransaction Transaction(LOCTEXT("BlockoutCutHole", "Percer une ouverture"));

	Target->Modify();

	UBlockoutPanelComponent* Panel = Surface.Panel;
	if (!Panel)
	{
		Panel = NewObject<UBlockoutPanelComponent>(Target, UBlockoutPanelComponent::StaticClass(),
			TEXT("BlockoutPanel"), RF_Transactional);
		Target->AddInstanceComponent(Panel);
		Panel->RegisterComponent();
	}
	Panel->Modify();

	Panel->PanelSize = Surface.PanelSize;
	Panel->UAxis = Surface.UAxis;
	Panel->VAxis = Surface.VAxis;
	Panel->WAxis = Surface.WAxis;
	Panel->Outer = Surface.Outer;
	Panel->Holes.Reset();
	for (const TArray<FVector2D>& Previous : PreviousHoles)
	{
		FBlockoutPanelHole H;
		H.Points = Previous;
		Panel->Holes.Add(MoveTemp(H));
	}
	{
		FBlockoutPanelHole H;
		H.Points = HolePoints;
		Panel->Holes.Add(MoveTemp(H));
	}

	// Le maillage genere est deja a la taille reelle : laisser l'ancienne echelle en place
	// multiplierait les dimensions une seconde fois au rendu -- le mur "grossit" et devient
	// invisible de l'interieur. Et comme le maillage genere est centre sur le pivot, si l'ancien
	// ne l'etait pas on deplace le pivot d'autant, pour que rien ne bouge a l'ecran.
	if (!Surface.LocalCenter.IsNearlyZero())
	{
		const FVector Offset = Xform.GetRotation().RotateVector(Surface.LocalCenter * Surface.Scale);
		Target->SetActorLocation(Target->GetActorLocation() + Offset);
	}
	Target->SetActorScale3D(FVector::OneVector);

	FString RebuildError;
	if (!RebuildPanelMesh(Target, Panel, RebuildError))
	{
		StatusMessage = FString::Printf(TEXT("Reconstruction impossible : %s"), *RebuildError);
		UE_LOG(LogBlockoutCut, Warning, TEXT("%s"), *StatusMessage);
		// La transaction est annulee par l'appelant via Ctrl+Z ; on ne laisse pas un etat a moitie
		// applique passer pour un succes.
		return;
	}

	StatusMessage = FString::Printf(TEXT("Ouverture percee (%d trou(s) sur ce panneau)."),
		Panel->Holes.Num());
	UE_LOG(LogBlockoutCut, Log, TEXT("%s"), *StatusMessage);

	CutPoints.Reset();
	bHasHoverPoint = false;
	bHoverNearFirstPoint = false;
}

#undef LOCTEXT_NAMESPACE
