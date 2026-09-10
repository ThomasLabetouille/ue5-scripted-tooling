#include "BlockoutCutMode.h"
#include "BlockoutPanelComponent.h"
#include "BlockoutPanelGeometry.h"

#include "EditorViewportClient.h"
#include "SceneView.h"
#include "PrimitiveDrawingUtils.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "BlockoutCutMode"

DEFINE_LOG_CATEGORY_STATIC(LogBlockoutCutMode, Log, All);

const FEditorModeID UBlockoutCutMode::EM_BlockoutCut = TEXT("EM_BlockoutCut");

namespace
{
	/** Rayon d'accroche, en pixels, pour fermer le contour en cliquant sur le 1er point. */
	constexpr double BlkCutClosePointScreenDistance = 12.0;

	/** Portee du rayon de selection sous le curseur. */
	constexpr double BlkCutPickDistance = 500000.0;

	/**
	 * Distance ecran entre un point monde et le curseur.
	 * Reprise telle quelle du mode dessin -- meme besoin, meme piege : un point derriere la
	 * camera n'est jamais "proche du curseur".
	 */
	bool ScreenDistanceTo(FEditorViewportClient* ViewportClient, const FVector& WorldPoint,
		const FIntPoint& CursorPos, double& OutPixels)
	{
		if (!ViewportClient || !ViewportClient->Viewport)
		{
			return false;
		}

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			ViewportClient->Viewport,
			ViewportClient->GetScene(),
			ViewportClient->EngineShowFlags)
			.SetRealtimeUpdate(ViewportClient->IsRealtime()));

		FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
		if (!View)
		{
			return false;
		}

		FVector2D Pixel;
		if (!View->WorldToPixel(WorldPoint, Pixel))
		{
			return false;
		}

		OutPixels = FVector2D::Distance(Pixel, FVector2D(CursorPos.X, CursorPos.Y));
		return true;
	}
}

UBlockoutCutMode::UBlockoutCutMode()
{
	Info = FEditorModeInfo(
		EM_BlockoutCut,
		LOCTEXT("BlockoutCutModeName", "Decoupe Blockout"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.MeshPaintMode"),
		true,
		501);
}

void UBlockoutCutMode::Enter()
{
	Super::Enter();
	ResetCutting();
	StatusMessage = TEXT("Survole un mur, un sol ou un plafond, puis clique le contour de l'ouverture.");
}

void UBlockoutCutMode::Exit()
{
	ResetCutting();
	Super::Exit();
}

void UBlockoutCutMode::ResetCutting()
{
	CutPoints.Reset();
	CutTarget = nullptr;
	HoverActor = nullptr;
	bHasHoverPoint = false;
	bHoverNearFirstPoint = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Survol
// ═══════════════════════════════════════════════════════════════════════════

void UBlockoutCutMode::UpdateHover(FEditorViewportClient* ViewportClient,
	const FVector& RayOrigin, const FVector& RayDirection, const FIntPoint& CursorPos)
{
	bHasHoverPoint = false;
	bHoverNearFirstPoint = false;

	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	// La cible se cherche tant qu'aucun point n'est pose ; ensuite elle est VERROUILLEE, le
	// contour devant rester sur une seule surface.
	//
	// Ecart assume vs. Unity : la selection passe par un trace de collision et non par le
	// picking de rendu (HandleUtility.PickGameObject). Le resultat est le meme sur de la
	// geometrie solide, et cela evite de dependre des proxys de hit, dont l'API varie.
	if (CutPoints.Num() == 0)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BlockoutCutPick), true);
		if (World->LineTraceSingleByChannel(Hit, RayOrigin,
				RayOrigin + RayDirection * BlkCutPickDistance, ECC_Visibility, Params))
		{
			HoverActor = Hit.GetActor();

			// La cible memorisee n'est remplacee que par une surface VALIDE : quitter l'objet du
			// curseur ne doit pas la vider, sinon on perdrait la selection en bougeant la souris.
			FString Reason;
			if (IsValidCutTarget(Hit.GetActor(), Reason))
			{
				CutTarget = Hit.GetActor();
			}
		}
		else
		{
			HoverActor = nullptr;
		}
	}

	AActor* const Candidate = CutTarget.Get();
	FBlockoutCutSurface Surface;
	if (!TryDescribeCutSurface(Candidate, Surface))
	{
		return;
	}

	FVector2D UV;
	if (!TryProjectOnSurface(Surface, RayOrigin, RayDirection, HoverPoint, UV))
	{
		return;
	}
	bHasHoverPoint = true;

	if (CutPoints.Num() >= 3)
	{
		double Pixels = 0.0;
		if (ScreenDistanceTo(ViewportClient, CutPoints[0], CursorPos, Pixels))
		{
			bHoverNearFirstPoint = (Pixels <= BlkCutClosePointScreenDistance);
		}
	}
}

bool UBlockoutCutMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport,
	int32 MouseX, int32 MouseY)
{
	if (!ViewportClient)
	{
		return false;
	}

	const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
	UpdateHover(ViewportClient, Cursor.GetOrigin(), Cursor.GetDirection(), FIntPoint(MouseX, MouseY));

	ViewportClient->Invalidate(false, false);
	return false;   // on ne consomme pas : la navigation de camera reste normale
}

// ═══════════════════════════════════════════════════════════════════════════
// Clics et touches
// ═══════════════════════════════════════════════════════════════════════════

bool UBlockoutCutMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy,
	const FViewportClick& Click)
{
	if (!InViewportClient || Click.GetKey() != EKeys::LeftMouseButton || Click.IsAltDown())
	{
		return false;
	}

	AActor* const Candidate = CutTarget.Get();
	FBlockoutCutSurface Surface;
	if (!TryDescribeCutSurface(Candidate, Surface))
	{
		FString Reason;
		IsValidCutTarget(HoverActor.Get(), Reason);
		StatusMessage = Reason.IsEmpty()
			? TEXT("Aucune surface decoupable sous le curseur.")
			: FString::Printf(TEXT("Decoupe impossible : %s"), *Reason);
		InViewportClient->Invalidate(false, false);
		return true;
	}

	FVector WorldPoint;
	FVector2D UV;
	if (!TryProjectOnSurface(Surface, Click.GetOrigin(), Click.GetDirection(), WorldPoint, UV))
	{
		StatusMessage = TEXT("Point hors de la surface, ou dans un trou deja perce.");
		InViewportClient->Invalidate(false, false);
		return true;
	}

	if (bHoverNearFirstPoint && CutPoints.Num() >= 3)
	{
		PerformCut();
	}
	else
	{
		CutPoints.Add(WorldPoint);
		StatusMessage = FString::Printf(
			TEXT("%d point(s). Clique le 1er point ou Entree pour percer."), CutPoints.Num());
	}

	InViewportClient->Invalidate(false, false);
	return true;   // consomme : pas de selection normale pendant la decoupe
}

bool UBlockoutCutMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport,
	FKey Key, EInputEvent Event)
{
	if (Event != IE_Pressed)
	{
		return false;
	}

	if ((Key == EKeys::Enter || Key == EKeys::Virtual_Accept) && CutPoints.Num() >= 3)
	{
		PerformCut();
		if (ViewportClient) { ViewportClient->Invalidate(false, false); }
		return true;
	}

	if (Key == EKeys::BackSpace && CutPoints.Num() > 0)
	{
		CutPoints.Pop();
		StatusMessage = FString::Printf(TEXT("%d point(s)."), CutPoints.Num());
		if (ViewportClient) { ViewportClient->Invalidate(false, false); }
		return true;
	}

	if (Key == EKeys::Escape)
	{
		// Meme convention que le mode dessin : un 1er Echap efface le contour, un 2e quitte.
		if (CutPoints.Num() > 0)
		{
			CutPoints.Reset();
			StatusMessage = TEXT("Contour efface. Echap a nouveau pour quitter le mode.");
			if (ViewportClient) { ViewportClient->Invalidate(false, false); }
			return true;
		}
		return false;
	}

	return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Affichage
// ═══════════════════════════════════════════════════════════════════════════

void UBlockoutCutMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	Super::Render(View, Viewport, PDI);
	if (!PDI)
	{
		return;
	}

	AActor* const Candidate = CutTarget.Get();
	FBlockoutCutSurface Surface;
	const bool bHasSurface = TryDescribeCutSurface(Candidate, Surface);

	// Contour de la surface visee, et trous deja perces.
	if (bHasSurface)
	{
		const FTransform Xform = Surface.Actor->GetActorTransform();

		auto ToWorld = [&](const FVector2D& UV) -> FVector
		{
			FVector Local = Surface.LocalCenter;
			Local[Surface.UAxis] += UV.X / FMath::Max(FMath::Abs(Surface.Scale[Surface.UAxis]), 1e-6);
			Local[Surface.VAxis] += UV.Y / FMath::Max(FMath::Abs(Surface.Scale[Surface.VAxis]), 1e-6);
			Local[Surface.WAxis] += Surface.HalfW / FMath::Max(FMath::Abs(Surface.Scale[Surface.WAxis]), 1e-6);
			return Xform.TransformPosition(Local);
		};

		const FLinearColor OutlineColor = (CutPoints.Num() == 0)
			? FLinearColor(0.2f, 1.f, 0.4f) : FLinearColor(0.4f, 0.8f, 1.f);
		for (int32 i = 0; i < Surface.Outer.Num(); ++i)
		{
			const FVector A = ToWorld(Surface.Outer[i]);
			const FVector B = ToWorld(Surface.Outer[(i + 1) % Surface.Outer.Num()]);
			PDI->DrawLine(A, B, OutlineColor, SDPG_Foreground, 2.f);
		}

		if (Surface.Panel)
		{
			for (const FBlockoutPanelHole& Hole : Surface.Panel->Holes)
			{
				if (Hole.Points.Num() < 3) { continue; }
				for (int32 i = 0; i < Hole.Points.Num(); ++i)
				{
					const FVector2D P0(Hole.Points[i].X * Surface.Scale[Surface.UAxis],
									   Hole.Points[i].Y * Surface.Scale[Surface.VAxis]);
					const FVector2D& Nxt = Hole.Points[(i + 1) % Hole.Points.Num()];
					const FVector2D P1(Nxt.X * Surface.Scale[Surface.UAxis],
									   Nxt.Y * Surface.Scale[Surface.VAxis]);
					PDI->DrawLine(ToWorld(P0), ToWorld(P1),
						FLinearColor(1.f, 0.55f, 0.1f), SDPG_Foreground, 1.5f);
				}
			}
		}
	}

	// Points poses et segments.
	const FLinearColor PointColor(0.f, 1.f, 1.f);
	for (const FVector& P : CutPoints)
	{
		DrawWireSphere(PDI, P, PointColor, 4.f, 8, SDPG_Foreground, 1.5f);
	}
	for (int32 i = 0; i + 1 < CutPoints.Num(); ++i)
	{
		PDI->DrawLine(CutPoints[i], CutPoints[i + 1], PointColor, SDPG_Foreground, 2.f);
	}

	// Segment de previsualisation vers le curseur, vert si le clic ferme le contour.
	if (bHasHoverPoint && CutPoints.Num() > 0)
	{
		const FVector Target = bHoverNearFirstPoint ? CutPoints[0] : HoverPoint;
		const FLinearColor PreviewColor = bHoverNearFirstPoint
			? FLinearColor(0.f, 1.f, 0.f) : FLinearColor(0.f, 1.f, 1.f, 0.6f);
		PDI->DrawLine(CutPoints.Last(), Target, PreviewColor, SDPG_Foreground, 1.5f);
	}
}

void UBlockoutCutMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport,
	const FSceneView* View, FCanvas* Canvas)
{
	Super::DrawHUD(ViewportClient, Viewport, View, Canvas);
	if (!Canvas || !GEngine || !GEngine->GetSmallFont())
	{
		return;
	}

	const TCHAR* Lines[] = {
		TEXT("DECOUPE BLOCKOUT"),
		TEXT("Clic gauche : poser un point de l'ouverture, sur la surface survolee"),
		TEXT("Clic sur le 1er point, ou Entree : percer"),
		TEXT("Retour arriere : retirer le dernier point   |   Echap : effacer, puis quitter"),
	};

	float Y = 48.f;
	for (const TCHAR* Line : Lines)
	{
		FCanvasTextItem Item(FVector2D(24.f, Y), FText::FromString(Line),
			GEngine->GetSmallFont(), FLinearColor::White);
		Item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Item);
		Y += 16.f;
	}

	if (!StatusMessage.IsEmpty())
	{
		FCanvasTextItem Item(FVector2D(24.f, Y + 6.f), FText::FromString(StatusMessage),
			GEngine->GetSmallFont(), FLinearColor(1.f, 0.85f, 0.2f));
		Item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Item);
	}
}

#undef LOCTEXT_NAMESPACE
