#include "BlockoutDrawMode.h"

#include "BlockoutDrawSettings.h"
#include "BlockoutPanelGeometry.h"
#include "BlockoutPanelComponent.h"
#include "BlockoutPanelMeshAsset.h"
#include "BlockoutGeometrySubsystem.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "SceneView.h"
#include "PrimitiveDrawingUtils.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"

#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "BlockoutDrawMode"

DEFINE_LOG_CATEGORY_STATIC(LogBlockoutDraw, Log, All);

const FEditorModeID UBlockoutDrawMode::EM_BlockoutDraw = TEXT("EM_BlockoutDraw");

namespace
{
	/** Rayon d'accroche, en pixels, pour fermer le contour en cliquant sur le 1er point. */
	constexpr double BlkClosePointScreenDistance = 12.0;

	/** En dessous, deux clics sont consideres comme le meme point. */
	constexpr double BlkMinSegmentLength = 1.0;   // 1 cm
}

UBlockoutDrawMode::UBlockoutDrawMode()
{
	Info = FEditorModeInfo(
		EM_BlockoutDraw,
		LOCTEXT("BlockoutDrawModeName", "Dessin Blockout"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.MeshPaintMode"),
		true,
		500);
}

// ═══════════════════════════════════════════════════════════════════════════
// Cycle de vie
// ═══════════════════════════════════════════════════════════════════════════

void UBlockoutDrawMode::Enter()
{
	Super::Enter();
	ResetDrawing();
	StatusMessage = TEXT("Clic gauche : poser un point du contour.");
	UE_LOG(LogBlockoutDraw, Log, TEXT("BlockoutDrawMode: entree (plan Z = %.0f)"), GroundZ());
}

void UBlockoutDrawMode::Exit()
{
	ResetDrawing();
	UE_LOG(LogBlockoutDraw, Log, TEXT("BlockoutDrawMode: sortie"));
	Super::Exit();
}

bool UBlockoutDrawMode::UsesToolkits() const
{
	// Les reglages vivent dans le panneau Outil Blockout (UBlockoutDrawSettings) --
	// pas de second endroit ou saisir la meme hauteur de mur.
	return false;
}

bool UBlockoutDrawMode::ShowModeWidgets() const      { return false; }
bool UBlockoutDrawMode::ShouldDrawWidget() const     { return false; }
bool UBlockoutDrawMode::UsesPropertyWidgets() const  { return false; }
bool UBlockoutDrawMode::AllowsViewportDragTool() const { return false; }

void UBlockoutDrawMode::ResetDrawing()
{
	Stage = EBlockoutDrawStage::Contour;
	ContourPoints.Reset();
	bHasHoverPoint = false;
	bHoverNearFirstPoint = false;
	HeightPivot = FVector::ZeroVector;

	if (const UBlockoutDrawSettings* Settings = UBlockoutDrawSettings::Get())
	{
		SignedHeight = Settings->bExtrudeUp ? Settings->Height : -Settings->Height;
	}
}

double UBlockoutDrawMode::GroundZ() const
{
	const UBlockoutDrawSettings* Settings = UBlockoutDrawSettings::Get();
	return Settings ? (double)Settings->GroundZ : 0.0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Geometrie d'interaction
// ═══════════════════════════════════════════════════════════════════════════

bool UBlockoutDrawMode::ProjectOnGround(const FVector& Origin, const FVector& Direction, FVector& OutPoint) const
{
	if (FMath::Abs(Direction.Z) < 1e-6)
	{
		return false;   // rayon parallele au plan de dessin
	}

	const double T = (GroundZ() - Origin.Z) / Direction.Z;
	if (T <= 0.0)
	{
		return false;   // le plan est DERRIERE la camera
	}

	OutPoint = Origin + Direction * T;
	OutPoint.Z = GroundZ();
	return true;
}

bool UBlockoutDrawMode::ScreenDistanceTo(FEditorViewportClient* ViewportClient, const FVector& WorldPoint,
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
		return false;   // point derriere la camera : jamais "proche du curseur"
	}

	OutPixels = FVector2D::Distance(Pixel, FVector2D(CursorPos.X, CursorPos.Y));
	return true;
}

void UBlockoutDrawMode::UpdateHover(FEditorViewportClient* ViewportClient,
	const FVector& RayOrigin, const FVector& RayDirection, const FIntPoint& CursorPos,
	bool bOrthoConstraint)
{
	bHasHoverPoint = ProjectOnGround(RayOrigin, RayDirection, HoverPoint);

	// Angles droits (Ctrl). Le repere est le PREMIER segment : il faut donc au moins
	// deux points poses. Sur le premier segment il n'y a rien a quoi s'aligner, on
	// laisse la souris libre -- c'est ce trace-la qui fixe l'orientation du batiment.
	bOrthoActive = false;
	if (bHasHoverPoint && bOrthoConstraint && ContourPoints.Num() >= 2)
	{
		const FVector2D Frame(ContourPoints[1].X - ContourPoints[0].X,
		                      ContourPoints[1].Y - ContourPoints[0].Y);
		const FVector2D Last(ContourPoints.Last().X, ContourPoints.Last().Y);

		// Direction du segment qui ARRIVE sur le dernier point : sert a refuser un
		// mur qui reviendrait sur lui-meme.
		const int32 N = ContourPoints.Num();
		const FVector2D Incoming(ContourPoints[N - 1].X - ContourPoints[N - 2].X,
		                         ContourPoints[N - 1].Y - ContourPoints[N - 2].Y);

		const FVector2D Snapped = BlockoutPanelGeometry::SnapToOrthogonalFrame(
			Last, FVector2D(HoverPoint.X, HoverPoint.Y), Frame, Incoming);

		// Z inchange : le contour vit sur le plan de dessin, la contrainte est 2D.
		HoverPoint.X = Snapped.X;
		HoverPoint.Y = Snapped.Y;
		bOrthoActive = true;
	}

	bHoverNearFirstPoint = false;
	if (bHasHoverPoint && ContourPoints.Num() >= 3)
	{
		double Pixels = 0.0;
		if (ScreenDistanceTo(ViewportClient, ContourPoints[0], CursorPos, Pixels))
		{
			bHoverNearFirstPoint = Pixels <= BlkClosePointScreenDistance;
		}
	}
}

void UBlockoutDrawMode::UpdateHeightFromRay(FEditorViewportClient* ViewportClient,
	const FVector& RayOrigin, const FVector& RayDirection, bool bSnapToGrid)
{
	if (!ViewportClient)
	{
		return;
	}

	// Plan VERTICAL passant par le centre du contour et faisant face a la camera : le
	// curseur y glisse naturellement de haut en bas quel que soit l'angle de vue. Une
	// simple mesure du deplacement vertical A L'ECRAN dependrait du zoom et donnerait
	// une sensibilite differente a chaque distance de camera.
	FVector Facing = ViewportClient->GetViewRotation().Vector();
	Facing.Z = 0.0;
	if (Facing.SizeSquared() < 1e-6)
	{
		Facing = FVector::ForwardVector;   // vue plongeante a la verticale
	}
	Facing.Normalize();

	const FVector PlaneNormal = -Facing;
	const double Denom = FVector::DotProduct(PlaneNormal, RayDirection);
	if (FMath::Abs(Denom) < 1e-6)
	{
		return;
	}

	const double T = FVector::DotProduct(PlaneNormal, HeightPivot - RayOrigin) / Denom;
	if (T <= 0.0)
	{
		return;
	}

	const FVector Hit = RayOrigin + RayDirection * T;
	double Signed = Hit.Z - GroundZ();

	if (bSnapToGrid && GEditor)
	{
		const double Step = (double)GEditor->GetGridSize();
		if (Step > 1e-4)
		{
			Signed = FMath::RoundToDouble(Signed / Step) * Step;
		}
	}

	// On garde le SIGNE : tirer sous le plan de dessin extrude vers le bas.
	SignedHeight = (FMath::Abs(Signed) < 1.0)
		? (Signed >= 0.0 ? 1.0 : -1.0)
		: Signed;

	if (UBlockoutDrawSettings* Settings = UBlockoutDrawSettings::Get())
	{
		Settings->Height = (float)FMath::Abs(SignedHeight);
		Settings->bExtrudeUp = SignedHeight >= 0.0;
	}
}

bool UBlockoutDrawMode::CloseSquareWithLastPoint()
{
	// Quatre points au minimum : a trois, "tous les angles droits" n'existe pas -- un
	// triangle ne peut pas en avoir plus d'un. Refuser est plus honnete que de poser
	// un point qui ne fermerait rien d'equerre.
	if (ContourPoints.Num() < 4)
	{
		StatusMessage = FString::Printf(
			TEXT("Ctrl+F : il faut au moins 4 points (%d pose(s))."), ContourPoints.Num());
		return false;
	}

	const int32 N = ContourPoints.Num();
	const FVector2D Frame(ContourPoints[1].X - ContourPoints[0].X,
	                      ContourPoints[1].Y - ContourPoints[0].Y);

	FVector2D Ajuste;
	const bool bOk = BlockoutPanelGeometry::SnapLastPointToCloseFrame(
		FVector2D(ContourPoints[0].X, ContourPoints[0].Y),
		Frame,
		FVector2D(ContourPoints[N - 2].X, ContourPoints[N - 2].Y),
		Ajuste);

	if (!bOk)
	{
		StatusMessage = TEXT("Ctrl+F : fermeture d'equerre impossible (repere ou resultat "
		                     "degenere). Contour inchange.");
		return false;
	}

	// On travaille sur une COPIE : si le contour ajuste s'auto-intersectait, le
	// rendre a l'utilisateur serait pire que de refuser -- il serait refuse plus tard,
	// a la generation, apres qu'il a deja regle la hauteur.
	TArray<FVector> Essai = ContourPoints;
	Essai[N - 1].X = Ajuste.X;
	Essai[N - 1].Y = Ajuste.Y;   // Z inchange : le contour vit sur le plan de dessin

	TArray<FVector2D> Plat;
	Plat.Reserve(Essai.Num());
	for (const FVector& P : Essai)
	{
		Plat.Add(FVector2D(P.X, P.Y));
	}
	if (!BlockoutPanelGeometry::IsSimplePolygon(Plat))
	{
		StatusMessage = TEXT("Ctrl+F : l'ajustement croiserait le contour. Contour inchange.");
		return false;
	}

	ContourPoints = MoveTemp(Essai);
	return BeginHeightStage();
}

bool UBlockoutDrawMode::BeginHeightStage()
{
	if (ContourPoints.Num() < 3)
	{
		StatusMessage = TEXT("Il faut au moins 3 points pour fermer un contour.");
		return false;
	}

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& P : ContourPoints)
	{
		Centroid += P;
	}
	Centroid /= (double)ContourPoints.Num();
	Centroid.Z = GroundZ();

	HeightPivot = Centroid;
	Stage = EBlockoutDrawStage::Height;
	StatusMessage = TEXT("Hauteur : bouge la souris, clic gauche pour valider. Ctrl = grille, Echap = revenir au contour.");
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Entrees
// ═══════════════════════════════════════════════════════════════════════════

bool UBlockoutDrawMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 MouseX, int32 MouseY)
{
	if (!ViewportClient)
	{
		return false;
	}

	const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();

	if (Stage == EBlockoutDrawStage::Contour)
	{
		UpdateHover(ViewportClient, Cursor.GetOrigin(), Cursor.GetDirection(),
			FIntPoint(MouseX, MouseY), ViewportClient->IsCtrlPressed());
	}
	else
	{
		UpdateHeightFromRay(ViewportClient, Cursor.GetOrigin(), Cursor.GetDirection(), ViewportClient->IsCtrlPressed());
	}

	ViewportClient->Invalidate(false, false);
	return false;   // on ne consomme pas : la navigation de camera doit rester normale
}

bool UBlockoutDrawMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (!InViewportClient || Click.GetKey() != EKeys::LeftMouseButton || Click.IsAltDown())
	{
		return false;
	}

	if (Stage == EBlockoutDrawStage::Contour)
	{
		UpdateHover(InViewportClient, Click.GetOrigin(), Click.GetDirection(),
			Click.GetClickPos(), Click.IsControlDown());

		if (!bHasHoverPoint)
		{
			StatusMessage = TEXT("Le plan de dessin n'est pas visible sous le curseur.");
			return true;
		}

		if (bHoverNearFirstPoint)
		{
			BeginHeightStage();
			return true;
		}

		// Deux clics au meme endroit produiraient une arete de longueur nulle : le
		// contour resterait "simple" au sens strict mais le mur correspondant serait
		// degenere, et la triangulation emettrait des triangles plats.
		if (ContourPoints.Num() > 0 &&
			FVector::Dist2D(ContourPoints.Last(), HoverPoint) < BlkMinSegmentLength)
		{
			StatusMessage = TEXT("Point ignore : trop proche du precedent.");
			return true;
		}

		ContourPoints.Add(HoverPoint);
		StatusMessage = FString::Printf(TEXT("%d point(s). Clic sur le 1er point (ou Entree) pour fermer."), ContourPoints.Num());
		InViewportClient->Invalidate(false, false);
		return true;
	}

	// Etape hauteur : le clic valide.
	UpdateHeightFromRay(InViewportClient, Click.GetOrigin(), Click.GetDirection(), Click.IsControlDown());
	GenerateRoom();
	InViewportClient->Invalidate(false, false);
	return true;
}

bool UBlockoutDrawMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (Event != IE_Pressed)
	{
		return false;
	}

	// Le clic gauche n'est PAS traite ici : le renvoyer comme consomme empecherait
	// ProcessClick de s'executer, et donc HandleClick de recevoir quoi que ce soit.
	if (Key == EKeys::LeftMouseButton)
	{
		return false;
	}

	if (Stage == EBlockoutDrawStage::Contour)
	{
		if (Key == EKeys::Enter)
		{
			BeginHeightStage();
			if (ViewportClient) ViewportClient->Invalidate(false, false);
			return true;
		}

		// Ctrl+F : fermeture d'equerre. Le F seul reste la touche "cadrer sur la
		// selection" de l'editeur -- on ne le consomme pas.
		if (Key == EKeys::F && ViewportClient && ViewportClient->IsCtrlPressed())
		{
			CloseSquareWithLastPoint();
			ViewportClient->Invalidate(false, false);
			return true;
		}

		if (Key == EKeys::BackSpace || Key == EKeys::Delete)
		{
			if (ContourPoints.Num() > 0)
			{
				ContourPoints.Pop();
				StatusMessage = FString::Printf(TEXT("%d point(s)."), ContourPoints.Num());
				if (ViewportClient) ViewportClient->Invalidate(false, false);
			}
			return true;
		}

		if (Key == EKeys::Escape)
		{
			if (ContourPoints.Num() > 0)
			{
				// Premier Echap : on efface le contour. Second Echap : on quitte le mode.
				ResetDrawing();
				StatusMessage = TEXT("Contour efface. Echap a nouveau pour quitter le mode.");
				if (ViewportClient) ViewportClient->Invalidate(false, false);
				return true;
			}
			return false;   // laisse l'editeur repasser en mode Selection
		}

		return false;
	}

	// Etape hauteur.
	if (Key == EKeys::Enter)
	{
		GenerateRoom();
		if (ViewportClient) ViewportClient->Invalidate(false, false);
		return true;
	}

	if (Key == EKeys::Escape)
	{
		// Retour a l'edition du contour, sans rien perdre.
		Stage = EBlockoutDrawStage::Contour;
		StatusMessage = TEXT("Retour au contour.");
		if (ViewportClient) ViewportClient->Invalidate(false, false);
		return true;
	}

	return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Affichage
// ═══════════════════════════════════════════════════════════════════════════

void UBlockoutDrawMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	Super::Render(View, Viewport, PDI);

	if (!PDI || !View)
	{
		return;
	}

	const FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
	auto HandleRadius = [&ViewOrigin](const FVector& P)
	{
		return FMath::Max(3.0, FVector::Dist(ViewOrigin, P) * 0.006);
	};

	const int32 Count = ContourPoints.Num();

	// Points deja poses.
	for (int32 i = 0; i < Count; ++i)
	{
		const FLinearColor Color = (i == 0 && bHoverNearFirstPoint) ? FLinearColor::Green : FLinearColor(1.f, 0.85f, 0.2f);
		DrawWireSphere(PDI, ContourPoints[i], Color, HandleRadius(ContourPoints[i]), 12, SDPG_Foreground, 1.5f);
	}

	if (Stage == EBlockoutDrawStage::Contour)
	{
		for (int32 i = 0; i + 1 < Count; ++i)
		{
			PDI->DrawLine(ContourPoints[i], ContourPoints[i + 1], FLinearColor(1.f, 0.85f, 0.2f), SDPG_Foreground, 2.f);
		}

		// Segment en cours : vers le curseur, ou vers le 1er point si on est dessus.
		if (bHasHoverPoint && Count > 0)
		{
			const FVector Target = bHoverNearFirstPoint ? ContourPoints[0] : HoverPoint;
			const FLinearColor Color = bHoverNearFirstPoint ? FLinearColor::Green : FLinearColor(1.f, 1.f, 0.f, 0.6f);
			PDI->DrawLine(ContourPoints.Last(), Target, Color, SDPG_Foreground, 1.5f);
		}
		return;
	}

	// Etape hauteur : previsualisation du prisme.
	if (Count < 3)
	{
		return;
	}

	const double Ground = GroundZ();
	const FLinearColor EdgeColor(1.f, 0.85f, 0.2f, 0.9f);
	const FLinearColor RiserColor(1.f, 0.85f, 0.2f, 0.5f);

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector A = ContourPoints[i];
		const FVector B = ContourPoints[(i + 1) % Count];

		const FVector A0(A.X, A.Y, Ground);
		const FVector B0(B.X, B.Y, Ground);
		const FVector A1(A.X, A.Y, Ground + SignedHeight);
		const FVector B1(B.X, B.Y, Ground + SignedHeight);

		PDI->DrawLine(A0, B0, EdgeColor, SDPG_Foreground, 2.f);
		PDI->DrawLine(A1, B1, EdgeColor, SDPG_Foreground, 2.f);
		PDI->DrawLine(A0, A1, RiserColor, SDPG_Foreground, 1.f);
	}
}

void UBlockoutDrawMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	Super::DrawHUD(ViewportClient, Viewport, View, Canvas);

	if (!Canvas || !GEngine)
	{
		return;
	}

	UFont* Font = GEngine->GetSmallFont();
	if (!Font)
	{
		return;
	}

	auto DrawLineOfText = [Canvas, Font](const FString& Text, float X, float Y, const FLinearColor& Color)
	{
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
		Item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Item);
	};

	float Y = 24.f;
	DrawLineOfText(TEXT("Dessin Blockout"), 24.f, Y, FLinearColor(1.f, 0.85f, 0.2f));
	Y += 18.f;

	if (Stage == EBlockoutDrawStage::Contour)
	{
		DrawLineOfText(TEXT("Clic gauche : poser un point   |   Clic sur le 1er point ou Entree : fermer"), 24.f, Y, FLinearColor::White);
		Y += 16.f;
		DrawLineOfText(TEXT("Retour arriere : annuler le dernier point   |   Echap : effacer / quitter"), 24.f, Y, FLinearColor::White);
		Y += 16.f;
		DrawLineOfText(
			ContourPoints.Num() >= 4
				? TEXT("Ctrl+F : ajuster le dernier point et fermer d'equerre")
				: TEXT("Ctrl+F : fermer d'equerre -- a partir de 4 points"),
			24.f, Y,
			ContourPoints.Num() >= 4 ? FLinearColor(0.4f, 1.f, 0.4f) : FLinearColor::Gray);
		Y += 16.f;
		DrawLineOfText(
			ContourPoints.Num() >= 2
				? (bOrthoActive ? TEXT("Ctrl : angles droits -- ACTIF")
				                : TEXT("Ctrl (maintenu) : angles droits"))
				: TEXT("Ctrl : angles droits -- des le 3e point (le 1er segment donne l'orientation)"),
			24.f, Y, bOrthoActive ? FLinearColor(0.4f, 1.f, 0.4f) : FLinearColor::Gray);
		Y += 16.f;
		DrawLineOfText(FString::Printf(TEXT("Plan de dessin Z = %.0f   |   Points : %d"), GroundZ(), ContourPoints.Num()), 24.f, Y, FLinearColor::Gray);
		Y += 16.f;
	}
	else
	{
		DrawLineOfText(TEXT("Souris : regler la hauteur   |   Clic gauche ou Entree : valider   |   Ctrl : grille   |   Echap : revenir"), 24.f, Y, FLinearColor::White);
		Y += 16.f;
		DrawLineOfText(
			FString::Printf(TEXT("Hauteur : %.0f uu (%s)"), FMath::Abs(SignedHeight), SignedHeight >= 0.0 ? TEXT("vers le haut") : TEXT("vers le bas")),
			24.f, Y, FLinearColor(0.4f, 1.f, 0.4f));
		Y += 16.f;
	}

	if (!StatusMessage.IsEmpty())
	{
		DrawLineOfText(StatusMessage, 24.f, Y, FLinearColor(0.7f, 0.85f, 1.f));
	}
}

#undef LOCTEXT_NAMESPACE
