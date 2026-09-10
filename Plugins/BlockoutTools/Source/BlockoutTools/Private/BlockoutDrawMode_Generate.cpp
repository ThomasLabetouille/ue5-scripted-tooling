#include "BlockoutDrawMode.h"

#include "BlockoutDrawSettings.h"
#include "BlockoutPanelGeometry.h"
#include "BlockoutPanelComponent.h"
#include "BlockoutPanelMeshAsset.h"
#include "BlockoutGeometrySubsystem.h"

#include "Editor.h"
#include "ScopedTransaction.h"

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "BlockoutDrawMode"

DEFINE_LOG_CATEGORY_STATIC(LogBlockoutDrawGen, Log, All);

// BlockoutDrawMode_Generate.cpp -- construction des acteurs a la validation du dessin.
// Separe de BlockoutDrawMode.cpp (interaction viewport) pour la meme raison que le
// panneau Slate est eclate en 7 .cpp : garder chaque fichier lisible.

namespace
{
	/** Le cube d'Unreal mesure 100 uu de cote a l'echelle 1. */
	constexpr double BlkCubeSizeUU = 100.0;
}

UStaticMesh* UBlockoutDrawMode::BuildPanelMeshAsset(const FString& PanelName,
	const TArray<FVector2D>& LocalContour, double Thickness, FString& OutError)
{
	OutError.Reset();

	// Le panneau est un contour plat extrude : U = X local, V = Y local, W = Z local
	// (l'epaisseur d'une dalle). Aucun trou a la creation -- la decoupe viendra les
	// ajouter plus tard sur le meme composant.
	const TArray<TArray<FVector2D>> NoHoles;
	FBlockoutPanelMesh PanelMesh;
	if (!BlockoutPanelGeometry::BuildPanelMesh(LocalContour, Thickness, /*U*/0, /*V*/1, /*W*/2, NoHoles, PanelMesh))
	{
		OutError = TEXT("triangulation impossible (contour degenere ou auto-intersectant)");
		return nullptr;
	}

	const UBlockoutDrawSettings* Settings = UBlockoutDrawSettings::Get();
	const FString RoomName = (Settings && !Settings->RoomName.IsEmpty()) ? Settings->RoomName : TEXT("Salle_Dessin");

	FString MeshError;
	UStaticMesh* Mesh = BlockoutPanelMeshAsset::CreateStaticMeshAsset(
		PanelMesh,
		BlockoutPanelMeshAsset::DefaultPackageFolder(),
		FString::Printf(TEXT("SM_%s_%s"), *RoomName, *PanelName),
		nullptr,
		MeshError);

	if (!Mesh)
	{
		OutError = MeshError.IsEmpty() ? TEXT("creation de l'asset mesh impossible") : MeshError;
		return nullptr;
	}

	return Mesh;
}

AActor* UBlockoutDrawMode::SpawnPanelActor(UStaticMesh* PanelMesh, const FString& PanelName,
	const TArray<FVector2D>& LocalContour, const FVector& WorldOrigin,
	double LocalZ, double Thickness, FString& OutError)
{
	OutError.Reset();

	if (!PanelMesh)
	{
		OutError = TEXT("aucun mesh fourni");
		return nullptr;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutError = TEXT("aucun monde d'edition");
		return nullptr;
	}

	const UBlockoutDrawSettings* Settings = UBlockoutDrawSettings::Get();
	const FString RoomName = (Settings && !Settings->RoomName.IsEmpty()) ? Settings->RoomName : TEXT("Salle_Dessin");
	UStaticMesh* Mesh = PanelMesh;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector SpawnLocation = WorldOrigin + FVector(0.0, 0.0, LocalZ);
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator, Params);

	if (!Actor)
	{
		OutError = TEXT("SpawnActor a echoue");
		return nullptr;
	}

	if (UStaticMeshComponent* SMC = Actor->GetStaticMeshComponent())
	{
		SMC->SetStaticMesh(Mesh);
	}
	Actor->SetActorScale3D(FVector::OneVector);   // le mesh est deja a la bonne taille
	Actor->SetActorLabel(FString::Printf(TEXT("%s_%s"), *RoomName, *PanelName));

	// Etat LOGIQUE du panneau : c'est lui, pas le mesh, qui permettra de re-decouper
	// ce sol plus tard sans avoir a redeviner ses dimensions ni ses trous.
	double MinU = TNumericLimits<double>::Max(), MaxU = TNumericLimits<double>::Lowest();
	double MinV = TNumericLimits<double>::Max(), MaxV = TNumericLimits<double>::Lowest();
	for (const FVector2D& P : LocalContour)
	{
		MinU = FMath::Min(MinU, P.X);
		MaxU = FMath::Max(MaxU, P.X);
		MinV = FMath::Min(MinV, P.Y);
		MaxV = FMath::Max(MaxV, P.Y);
	}

	UBlockoutPanelComponent* Panel = NewObject<UBlockoutPanelComponent>(Actor);
	Panel->PanelSize = FVector(MaxU - MinU, MaxV - MinV, Thickness);
	Panel->UAxis = 0;
	Panel->VAxis = 1;
	Panel->WAxis = 2;
	Panel->Outer = LocalContour;
	Panel->GeneratedMeshPath = Mesh->GetPathName();
	Actor->AddInstanceComponent(Panel);
	Panel->RegisterComponent();

	return Actor;
}

bool UBlockoutDrawMode::GenerateRoom()
{
	UBlockoutDrawSettings* Settings = UBlockoutDrawSettings::Get();
	if (!Settings)
	{
		StatusMessage = TEXT("Reglages introuvables.");
		return false;
	}

	const int32 Count = ContourPoints.Num();
	if (Count < 3)
	{
		StatusMessage = TEXT("Il faut au moins 3 points.");
		return false;
	}

	const double Thickness = FMath::Max((double)Settings->WallThickness, 1.0);
	const double Height = SignedHeight;
	if (FMath::Abs(Height) < 1.0)
	{
		StatusMessage = TEXT("Hauteur nulle : rien a generer.");
		return false;
	}

	// Centroide : pivot de la salle generee, au niveau du plan de dessin.
	FVector Centroid = FVector::ZeroVector;
	for (const FVector& P : ContourPoints)
	{
		Centroid += P;
	}
	Centroid /= (double)Count;
	Centroid.Z = GroundZ();

	TArray<FVector2D> LocalContour;
	LocalContour.Reserve(Count);
	for (const FVector& P : ContourPoints)
	{
		LocalContour.Add(FVector2D(P.X - Centroid.X, P.Y - Centroid.Y));
	}

	// Refuser AVANT de creer quoi que ce soit. Un contour auto-intersectant produit une
	// surface aberrante : mieux vaut le dire que generer un sol impossible a comprendre.
	if (!BlockoutPanelGeometry::IsSimplePolygon(LocalContour))
	{
		StatusMessage = TEXT("Contour auto-intersectant : deplace un point, les aretes se croisent.");
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UBlockoutGeometrySubsystem* Geometry = GEditor ? GEditor->GetEditorSubsystem<UBlockoutGeometrySubsystem>() : nullptr;
	if (!World || !Geometry)
	{
		StatusMessage = TEXT("Editeur indisponible (monde ou sous-systeme manquant).");
		return false;
	}

	const FString RoomName = Settings->RoomName.IsEmpty() ? TEXT("Salle_Dessin") : Settings->RoomName;

	TArray<FString> Errors;

	// ─────────────────────────────────────────────────────────────────────────
	// ETAPE 1 -- les assets, AVANT toute transaction.
	//
	// Ecrire un asset dans une transaction d'undo est une faute : un UStaticMesh porte
	// des sous-objets RF_Transactional (UStaticMeshDescriptionBulkData) qu'un Ctrl+Z
	// ramene a leur etat d'avant, c'est-a-dire inexistants. Le mesh reste en memoire
	// avec un sous-objet nul et l'editeur tombe au premier autosave, longtemps apres
	// l'undo et sans rapport visible avec lui (bug du 2026-08-26).
	//
	// Consequence assumee : un Ctrl+Z retire les ACTEURS, pas les assets. Un mesh
	// orphelin peut rester dans /Game/Blockout/GeneratedMeshes -- c'est la meme limite
	// que cote Unity, et c'est de loin le moindre mal.
	UStaticMesh* FloorMesh = nullptr;
	UStaticMesh* CeilingMesh = nullptr;

	if (Settings->bAddFloor)
	{
		FString Error;
		FloorMesh = BuildPanelMeshAsset(TEXT("Sol"), LocalContour, Thickness, Error);
		if (!FloorMesh) Errors.Add(FString::Printf(TEXT("sol (%s)"), *Error));
	}
	if (Settings->bAddCeiling)
	{
		FString Error;
		CeilingMesh = BuildPanelMeshAsset(TEXT("Plafond"), LocalContour, Thickness, Error);
		if (!CeilingMesh) Errors.Add(FString::Printf(TEXT("plafond (%s)"), *Error));
	}

	// ─────────────────────────────────────────────────────────────────────────
	// ETAPE 2 -- les acteurs, dans une seule transaction : Ctrl+Z retire la salle.
	FScopedTransaction Transaction(LOCTEXT("BlockoutDrawGenerate", "Dessiner une salle de blockout"));

	TArray<AActor*> Created;

	// ── Murs : un cube par arete du contour ───────────────────────────────────
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector& A = ContourPoints[i];
		const FVector& B = ContourPoints[(i + 1) % Count];

		FVector Edge = B - A;
		Edge.Z = 0.0;
		const double Length = Edge.Size2D();
		if (Length < 1.0)
		{
			continue;   // deux points confondus : segment ignore
		}

		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Edge.Y, Edge.X));
		const FVector Mid((A.X + B.X) * 0.5, (A.Y + B.Y) * 0.5, GroundZ() + Height * 0.5);

		// Le cube local : X = longueur du mur, Y = epaisseur, Z = hauteur.
		const FVector Scale(Length / BlkCubeSizeUU, Thickness / BlkCubeSizeUU, FMath::Abs(Height) / BlkCubeSizeUU);

		AActor* Wall = Geometry->SpawnScaledRotatedCube(
			nullptr, Mid, FRotator(0.f, (float)Yaw, 0.f), Scale,
			FString::Printf(TEXT("%s_Mur_%02d"), *RoomName, i));

		if (Wall)
		{
			Created.Add(Wall);
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("mur %d"), i));
		}
	}

	// ── Sol : face SUPERIEURE au niveau du plan de dessin (on marche a Z = GroundZ) ──
	if (FloorMesh)
	{
		FString Error;
		if (AActor* Floor = SpawnPanelActor(FloorMesh, TEXT("Sol"), LocalContour, Centroid, -Thickness * 0.5, Thickness, Error))
		{
			Created.Add(Floor);
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("sol (%s)"), *Error));
		}
	}

	// ── Plafond : face INFERIEURE au sommet de l'extrusion ────────────────────
	if (CeilingMesh)
	{
		FString Error;
		if (AActor* Ceiling = SpawnPanelActor(CeilingMesh, TEXT("Plafond"), LocalContour, Centroid, Height + Thickness * 0.5, Thickness, Error))
		{
			Created.Add(Ceiling);
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("plafond (%s)"), *Error));
		}
	}

	if (Created.Num() == 0)
	{
		StatusMessage = TEXT("Aucun acteur cree -- voir l'Output Log.");
		UE_LOG(LogBlockoutDrawGen, Warning, TEXT("BlockoutDrawMode: generation vide (%s)"), *FString::Join(Errors, TEXT(", ")));
		return false;
	}

	// Rangement Outliner : ne jamais ecraser un dossier deja assigne (regle du panneau).
	const FName FolderPath(*FString::Printf(TEXT("Blockout/%s"), *RoomName));
	for (AActor* Actor : Created)
	{
		if (Actor && Actor->GetFolderPath().IsNone())
		{
			Actor->SetFolderPath(FolderPath);
		}
	}

	GEditor->SelectNone(false, true);
	for (AActor* Actor : Created)
	{
		GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/false);
	}
	GEditor->NoteSelectionChange();

	Settings->SaveConfig();

	StatusMessage = Errors.Num() == 0
		? FString::Printf(TEXT("'%s' generee : %d acteurs. Reclique pour une nouvelle salle."), *RoomName, Created.Num())
		: FString::Printf(TEXT("'%s' generee partiellement (%d acteurs). Echecs : %s"), *RoomName, Created.Num(), *FString::Join(Errors, TEXT(", ")));

	UE_LOG(LogBlockoutDrawGen, Log, TEXT("BlockoutDrawMode: %s"), *StatusMessage);

	ResetDrawing();
	return Errors.Num() == 0;
}

#undef LOCTEXT_NAMESPACE
