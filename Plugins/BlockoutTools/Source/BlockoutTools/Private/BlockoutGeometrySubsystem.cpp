#include "BlockoutGeometrySubsystem.h"

#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Math/UnrealMathUtility.h"
#include "EngineUtils.h"                    // TActorIterator (FindActorsByLabelContains)
#include "Subsystems/EditorActorSubsystem.h" // SwapActorsToMesh (DestroyActor)


// ---------------------------------------------------------------------------
// Internal helper
// ---------------------------------------------------------------------------

static UWorld* RG_GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

// ---------------------------------------------------------------------------
// SpawnScaledCube
// ---------------------------------------------------------------------------

AActor* UBlockoutGeometrySubsystem::SpawnScaledCube(
	UObject* WorldContext, FVector Location, FVector Scale, FString ActorLabel)
{
	// Resolve world: prefer explicit context, fall back to editor world.
	UWorld* World = nullptr;
	if (WorldContext)
	{
		World = GEngine->GetWorldFromContextObject(
			WorldContext, EGetWorldErrorMode::LogAndReturnNull);
	}
	if (!World)
	{
		World = RG_GetEditorWorld();
	}
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("BlockoutTools::SpawnScaledCube - cannot resolve a valid World"));
		return nullptr;
	}

	// /Engine/BasicShapes/Cube is always present in any UE5 installation.
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("BlockoutTools::SpawnScaledCube - failed to load /Engine/BasicShapes/Cube"));
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), Location, FRotator::ZeroRotator, Params);

	if (Actor)
	{
		Actor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
		Actor->SetActorScale3D(Scale);
		Actor->SetActorLabel(ActorLabel);  // editor-only; safe in Editor module
	}

	return Actor;
}

// ---------------------------------------------------------------------------
// SpawnScaledRotatedCube
// ---------------------------------------------------------------------------

AActor* UBlockoutGeometrySubsystem::SpawnScaledRotatedCube(
	UObject* WorldContext, FVector Location, FRotator Rotation, FVector Scale, FString ActorLabel)
{
	UWorld* World = nullptr;
	if (WorldContext)
	{
		World = GEngine->GetWorldFromContextObject(
			WorldContext, EGetWorldErrorMode::LogAndReturnNull);
	}
	if (!World)
	{
		World = RG_GetEditorWorld();
	}
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("BlockoutTools::SpawnScaledRotatedCube - cannot resolve a valid World"));
		return nullptr;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("BlockoutTools::SpawnScaledRotatedCube - failed to load /Engine/BasicShapes/Cube"));
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), Location, Rotation, Params);

	if (Actor)
	{
		if (UStaticMeshComponent* SMC = Actor->GetStaticMeshComponent())
		{
			SMC->SetStaticMesh(CubeMesh);
			SMC->SetMobility(EComponentMobility::Static);
		}
		Actor->SetActorScale3D(Scale);
		Actor->SetActorLabel(ActorLabel);
	}

	return Actor;
}

// ---------------------------------------------------------------------------
// GenerateRoom
// ---------------------------------------------------------------------------

void UBlockoutGeometrySubsystem::GenerateRoom(
	FVector Center, FVector SizeXY, float WallHeight, float WallThickness, FString RoomName, bool bNoCeiling)
{
	// /Engine/BasicShapes/Cube is 100 x 100 x 100 UU at scale (1,1,1).
	// => scale component = desired_dimension_UU / 100.
	constexpr float S = 100.f;

	const float HX = SizeXY.X * 0.5f;    // half interior width
	const float HY = SizeXY.Y * 0.5f;    // half interior depth
	const float HT = WallThickness * 0.5f;
	const float HH = WallHeight * 0.5f;

	// ── Floor (top face at Center.Z) ──────────────────────────────────────
	SpawnScaledCube(nullptr,
		Center + FVector(0.f, 0.f, -HT),
		FVector(SizeXY.X / S, SizeXY.Y / S, WallThickness / S),
		RoomName + TEXT("_Floor"));

	// ── Ceiling (bottom face at Center.Z + WallHeight) -- jamais spawn si
	// bNoCeiling (evite un spawn-puis-destroy immediat cote appelant) ──────
	if (!bNoCeiling)
	{
		SpawnScaledCube(nullptr,
			Center + FVector(0.f, 0.f, WallHeight + HT),
			FVector(SizeXY.X / S, SizeXY.Y / S, WallThickness / S),
			RoomName + TEXT("_Ceiling"));
	}

	// ── North wall (−Y face) ──────────────────────────────────────────────
	SpawnScaledCube(nullptr,
		Center + FVector(0.f, -(HY + HT), HH),
		FVector(SizeXY.X / S, WallThickness / S, WallHeight / S),
		RoomName + TEXT("_WallN"));

	// ── South wall (+Y face) ──────────────────────────────────────────────
	SpawnScaledCube(nullptr,
		Center + FVector(0.f, HY + HT, HH),
		FVector(SizeXY.X / S, WallThickness / S, WallHeight / S),
		RoomName + TEXT("_WallS"));

	// ── West wall (−X face) ───────────────────────────────────────────────
	SpawnScaledCube(nullptr,
		Center + FVector(-(HX + HT), 0.f, HH),
		FVector(WallThickness / S, SizeXY.Y / S, WallHeight / S),
		RoomName + TEXT("_WallW"));

	// ── East wall (+X face) ───────────────────────────────────────────────
	SpawnScaledCube(nullptr,
		Center + FVector(HX + HT, 0.f, HH),
		FVector(WallThickness / S, SizeXY.Y / S, WallHeight / S),
		RoomName + TEXT("_WallE"));
}

// ---------------------------------------------------------------------------
// GenerateCorridor
// ---------------------------------------------------------------------------

TArray<AActor*> UBlockoutGeometrySubsystem::GenerateCorridor(
	FVector Start, FVector End, float Width, float Height,
	float WallThickness, FString CorridorName)
{
	TArray<AActor*> NewActors;

	FVector Dir = End - Start;
	const float Length = Dir.Size();
	if (Length < 1.f) return NewActors;
	Dir /= Length;   // normalize to unit direction

	const FVector Center = (Start + End) * 0.5f;
	const FRotator Rot   = Dir.Rotation();   // aligns local-X with Dir
	const float HT       = WallThickness * 0.5f;
	const float HH       = Height * 0.5f;
	constexpr float S    = 100.f;

	// Side vector perpendicular to Dir in the XY plane.
	// After applying Rot, this coincides with the actor's local +Y axis.
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();

	// ── Floor ─────────────────────────────────────────────────────────────
	// Scale: local-X = Length (along Dir), local-Y = Width, local-Z = thickness
	AActor* Floor = SpawnScaledCube(nullptr,
		Center + FVector(0.f, 0.f, -HT),
		FVector(Length / S, Width / S, WallThickness / S),
		CorridorName + TEXT("_Floor"));
	if (Floor) { Floor->SetActorRotation(Rot); NewActors.Add(Floor); }

	// ── Ceiling ───────────────────────────────────────────────────────────
	AActor* Ceiling = SpawnScaledCube(nullptr,
		Center + FVector(0.f, 0.f, Height + HT),
		FVector(Length / S, Width / S, WallThickness / S),
		CorridorName + TEXT("_Ceiling"));
	if (Ceiling) { Ceiling->SetActorRotation(Rot); NewActors.Add(Ceiling); }

	// ── Left wall (offset in +Side direction) ────────────────────────────
	// Scale: local-X = Length, local-Y = WallThickness, local-Z = Height
	AActor* WallL = SpawnScaledCube(nullptr,
		Center + Side * (Width * 0.5f + HT) + FVector(0.f, 0.f, HH),
		FVector(Length / S, WallThickness / S, Height / S),
		CorridorName + TEXT("_WallL"));
	if (WallL) { WallL->SetActorRotation(Rot); NewActors.Add(WallL); }

	// ── Right wall (offset in −Side direction) ───────────────────────────
	AActor* WallR = SpawnScaledCube(nullptr,
		Center - Side * (Width * 0.5f + HT) + FVector(0.f, 0.f, HH),
		FVector(Length / S, WallThickness / S, Height / S),
		CorridorName + TEXT("_WallR"));
	if (WallR) { WallR->SetActorRotation(Rot); NewActors.Add(WallR); }

	return NewActors;
}

// ---------------------------------------------------------------------------
// ComputeStaircasePlan / GenerateStaircase
// ---------------------------------------------------------------------------
//
// Math portee de Content/Python/stairs_ramps.py puis de l'ancien
// SBlockoutToolPanel::ComputeStaircasePlan (namespace anonyme) -- memes
// valeurs, meme formule de Blondel, meme repli de giron minimal. Deplacee ici
// (2026-07-30, session 21) pour etre appelable/testable depuis Python sans
// passer par un clic UI.

namespace
{
	constexpr float DefaultTargetStepHeight = 17.f;   // UU -- hauteur de marche confortable
	constexpr float DefaultMinStepHeight    = 5.f;    // UU
	constexpr float DefaultBlondelConstant  = 63.f;   // UU -- 2*hauteur_marche + giron
}

void UBlockoutGeometrySubsystem::ComputeStaircasePlan(
	float TotalHeight, float CapsuleRadius, float MaxStepHeight,
	int32& OutNumSteps, float& OutStepHeight, float& OutGoing, float& OutTotalRun, FString& OutWarning)
{
	OutWarning.Empty();

	// Giron minimum : diametre entier de la capsule ecraserait quasi-toujours
	// la formule de Blondel (teste en conditions reelles sur ce projet), d'ou
	// ce facteur reduit.
	const float MinTreadDepth = CapsuleRadius * 0.6f + 15.f;
	const float Target = FMath::Clamp(DefaultTargetStepHeight, DefaultMinStepHeight, MaxStepHeight);

	int32 NumSteps = FMath::Max(1, FMath::RoundToInt(TotalHeight / Target));
	float StepHeight = TotalHeight / NumSteps;

	int32 Safety = 0;
	while (StepHeight > MaxStepHeight && Safety < 500)
	{
		++NumSteps;
		StepHeight = TotalHeight / NumSteps;
		++Safety;
	}

	float Going = DefaultBlondelConstant - 2.f * StepHeight;
	if (Going < MinTreadDepth)
	{
		OutWarning = FString::Printf(
			TEXT("Giron ideal (Blondel) %.1f UU trop court pour la capsule (min %.1f UU) -- force au minimum."),
			Going, MinTreadDepth);
		Going = MinTreadDepth;
	}

	OutNumSteps = NumSteps;
	OutStepHeight = StepHeight;
	OutGoing = Going;
	OutTotalRun = Going * NumSteps;
}

TArray<AActor*> UBlockoutGeometrySubsystem::GenerateStaircase(
	FVector Start, float Yaw, float TotalHeight, float Width,
	float CapsuleRadius, float MaxStepHeight, FString StairName)
{
	TArray<AActor*> NewActors;
	if (TotalHeight <= 0.f)
	{
		return NewActors;
	}

	int32 NumSteps;
	float StepHeight, Going, TotalRun;
	FString Warning;
	ComputeStaircasePlan(TotalHeight, CapsuleRadius, MaxStepHeight, NumSteps, StepHeight, Going, TotalRun, Warning);

	const float YawRad = FMath::DegreesToRadians(Yaw);
	const FVector Fwd(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);

	NewActors.Reserve(NumSteps);
	for (int32 i = 0; i < NumSteps; ++i)
	{
		const float LocalX = i * Going + Going / 2.f;
		const float LocalZ = ((i + 1) * StepHeight) / 2.f;
		const FVector StepLoc = Start + Fwd * LocalX + FVector(0.f, 0.f, LocalZ);
		const FString StepLabel = FString::Printf(TEXT("%s_Step%02d"), *StairName, i + 1);
		if (AActor* Step = SpawnScaledRotatedCube(nullptr, StepLoc, FRotator(0.f, Yaw, 0.f),
				FVector(Going, Width, (i + 1) * StepHeight) / 100.f, StepLabel))
		{
			NewActors.Add(Step);
		}
	}
	return NewActors;
}

// ---------------------------------------------------------------------------
// ComputeRampPlan / GenerateRamp
// ---------------------------------------------------------------------------

void UBlockoutGeometrySubsystem::ComputeRampPlan(
	float TotalHeight, float RunOverride, float AngleOverrideDeg, float MaxWalkableAngleDeg,
	float& OutRun, float& OutAngleDeg, float& OutLength, bool& OutWalkable, FString& OutWarning)
{
	OutWarning.Empty();
	float Run;
	float AngleDeg;

	// Convention (Blueprint/Python-friendly, pas de TOptional) : <= 0 = "non
	// fourni". L'angle a priorite sur la longueur horizontale s'il est fourni --
	// meme convention que compute_ramp_plan() (stairs_ramps.py).
	if (AngleOverrideDeg > 0.f)
	{
		AngleDeg = AngleOverrideDeg;
		Run = TotalHeight / FMath::Tan(FMath::DegreesToRadians(AngleDeg));
	}
	else if (RunOverride > 0.f)
	{
		Run = RunOverride;
		AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(TotalHeight, Run));
	}
	else
	{
		Run = 400.f;
		AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(TotalHeight, Run));
	}

	OutRun = Run;
	OutAngleDeg = AngleDeg;
	OutLength = FMath::Sqrt(Run * Run + TotalHeight * TotalHeight);
	OutWalkable = AngleDeg <= MaxWalkableAngleDeg;
	if (!OutWalkable)
	{
		OutWarning = FString::Printf(
			TEXT("Pente a %.1f deg > angle marchable (%.1f deg) -- le joueur GLISSERA et ne pourra pas monter."),
			AngleDeg, MaxWalkableAngleDeg);
	}
}

AActor* UBlockoutGeometrySubsystem::GenerateRamp(
	FVector Start, float Yaw, float TotalHeight, float Width,
	float RunOverride, float AngleOverrideDeg, float MaxWalkableAngleDeg, FString RampName)
{
	if (TotalHeight <= 0.f)
	{
		return nullptr;
	}

	float Run, AngleDeg, Length;
	bool bWalkable;
	FString Warning;
	ComputeRampPlan(TotalHeight, RunOverride, AngleOverrideDeg, MaxWalkableAngleDeg, Run, AngleDeg, Length, bWalkable, Warning);

	const float YawRad = FMath::DegreesToRadians(Yaw);
	const FVector Fwd(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
	const FVector Mid = Start + Fwd * (Run / 2.f) + FVector(0.f, 0.f, TotalHeight / 2.f);

	constexpr float RampThickness = 30.f;
	return SpawnScaledRotatedCube(nullptr, Mid, FRotator(AngleDeg, Yaw, 0.f),
		FVector(Length, Width, RampThickness) / 100.f, RampName);
}

// ---------------------------------------------------------------------------
// GenerateVisionCone
// ---------------------------------------------------------------------------
//
// Migre depuis SBlockoutToolPanel::OnGenerateVisionConeClicked (2026-08-10,
// voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md item #1) -- meme formule,
// meme convention (SegMidYaw centre l'eventail sur Yaw).

TArray<AActor*> UBlockoutGeometrySubsystem::GenerateVisionCone(
	FVector Apex, float Yaw, float Radius, float AngleDeg, float Height, int32 NumSegments, FString ConeName)
{
	TArray<AActor*> NewActors;
	if (Radius <= 0.f || AngleDeg <= 0.f || AngleDeg > 360.f || Height <= 0.f || NumSegments < 1)
	{
		return NewActors;
	}

	const float AngleRad = FMath::DegreesToRadians(AngleDeg);
	const float SegAngleRad = AngleRad / NumSegments;
	const float SegAngleDeg = AngleDeg / NumSegments;
	const float ChordWidth = 2.f * Radius * FMath::Sin(SegAngleRad / 2.f);

	NewActors.Reserve(NumSegments);
	for (int32 i = 0; i < NumSegments; ++i)
	{
		const float SegMidYaw = Yaw - (AngleDeg / 2.f) + SegAngleDeg * (i + 0.5f);
		const float SegYawRad = FMath::DegreesToRadians(SegMidYaw);
		const FVector Dir(FMath::Cos(SegYawRad), FMath::Sin(SegYawRad), 0.f);
		const FVector SegLoc = Apex + Dir * (Radius / 2.f) + FVector(0.f, 0.f, Height / 2.f);
		const FString SegLabel = FString::Printf(TEXT("%s_Seg%02d"), *ConeName, i + 1);
		if (AActor* Seg = SpawnScaledRotatedCube(nullptr, SegLoc, FRotator(0.f, SegMidYaw, 0.f),
				FVector(Radius, ChordWidth, Height) / 100.f, SegLabel))
		{
			NewActors.Add(Seg);
		}
	}
	return NewActors;
}

// ---------------------------------------------------------------------------
// GenerateBridgeArch
// ---------------------------------------------------------------------------
//
// Migre depuis SBlockoutToolPanel::OnGenerateBridgeArchClicked (2026-08-10).
// ComputeArchArcPoint reste un helper prive de ce fichier (pas expose en
// UFUNCTION) : contrairement a ComputeStaircasePlan/ComputeRampPlan, il n'a
// pas de branche/repli a verifier isolement -- sa correction se verifie deja
// indirectement via la position des voussoirs generes (voir tests).

namespace
{
	struct FArchArcPoint
	{
		float X = 0.f;
		float Z = 0.f;
	};

	FArchArcPoint ComputeArchArcPoint(float T, float HalfSpan, float Rise)
	{
		const float AngleRad = PI * (1.f - T);
		FArchArcPoint P;
		P.X = HalfSpan * FMath::Cos(AngleRad);
		P.Z = Rise * FMath::Sin(AngleRad);
		return P;
	}
}

TArray<AActor*> UBlockoutGeometrySubsystem::GenerateBridgeArch(
	FVector Base, float Yaw, float Span, float Rise, float Thickness, float Width,
	float PierHeight, float DeckThickness, int32 NumSegments, FString ArchName)
{
	TArray<AActor*> NewActors;
	if (Span <= 0.f || Rise <= 0.f || Thickness <= 0.f || Width <= 0.f
		|| PierHeight < 0.f || DeckThickness <= 0.f || NumSegments < 1)
	{
		return NewActors;
	}

	const float YawRad = FMath::DegreesToRadians(Yaw);
	const FVector Fwd(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
	const FVector SpringBase = Base + FVector(0.f, 0.f, PierHeight);
	const float HalfSpan = Span / 2.f;

	NewActors.Reserve(NumSegments + 3);

	// ── Piliers (2, aux deux pieds de l'arc) ──
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float SignSide = (Side == 0) ? -1.f : 1.f;
		const FVector PierLoc = Base + Fwd * (SignSide * HalfSpan) + FVector(0.f, 0.f, PierHeight / 2.f);
		const FString PierLabel = FString::Printf(TEXT("%s_Pilier%d"), *ArchName, Side + 1);
		if (PierHeight > 0.f)
		{
			if (AActor* Pier = SpawnScaledRotatedCube(nullptr, PierLoc, FRotator(0.f, Yaw, 0.f),
					FVector(Thickness, Width, PierHeight) / 100.f, PierLabel))
			{
				NewActors.Add(Pier);
			}
		}
	}

	// ── Voussoirs (demi-ellipse, t=0 -> pied gauche, t=1 -> pied droit) ──
	for (int32 i = 0; i < NumSegments; ++i)
	{
		const float T0 = (float)i / NumSegments;
		const float T1 = (float)(i + 1) / NumSegments;
		const FArchArcPoint P0 = ComputeArchArcPoint(T0, HalfSpan, Rise);
		const FArchArcPoint P1 = ComputeArchArcPoint(T1, HalfSpan, Rise);

		const float MidX = (P0.X + P1.X) / 2.f;
		const float MidZ = (P0.Z + P1.Z) / 2.f;
		const float DeltaX = P1.X - P0.X;
		const float DeltaZ = P1.Z - P0.Z;
		const float SegLength = FMath::Sqrt(DeltaX * DeltaX + DeltaZ * DeltaZ);
		// Meme convention que la Rampe : FRotator(Pitch, Yaw, 0) avec Pitch positif
		// qui incline l'axe local X (longueur) vers le haut.
		const float PitchDeg = FMath::RadiansToDegrees(FMath::Atan2(DeltaZ, DeltaX));

		const FVector SegLoc = SpringBase + Fwd * MidX + FVector(0.f, 0.f, MidZ);
		const FString SegLabel = FString::Printf(TEXT("%s_Voussoir%02d"), *ArchName, i + 1);
		if (AActor* Seg = SpawnScaledRotatedCube(nullptr, SegLoc, FRotator(PitchDeg, Yaw, 0.f),
				FVector(SegLength, Width, Thickness) / 100.f, SegLabel))
		{
			NewActors.Add(Seg);
		}
	}

	// ── Tablier (deck) -- pose au sommet de l'arche/des piliers, leger surplomb ──
	const float DeckLength = Span + Thickness;
	const float DeckZ = PierHeight + Rise + Thickness + DeckThickness / 2.f;
	const FVector DeckLoc = Base + FVector(0.f, 0.f, DeckZ);
	const FString DeckLabel = ArchName + TEXT("_Tablier");
	if (AActor* Deck = SpawnScaledRotatedCube(nullptr, DeckLoc, FRotator(0.f, Yaw, 0.f),
			FVector(DeckLength, Width, DeckThickness) / 100.f, DeckLabel))
	{
		NewActors.Add(Deck);
	}

	return NewActors;
}

// ---------------------------------------------------------------------------
// ComputeCurvedTunnelPoints / GenerateCurvedTunnel
// ---------------------------------------------------------------------------
//
// Migre depuis SBlockoutToolPanel::OnGenerateCurvedTunnelClicked (2026-08-10).

void UBlockoutGeometrySubsystem::ComputeCurvedTunnelPoints(
	FVector Start, float Yaw, float Radius, float AngleDeg, int32 NumSegments, TArray<FVector>& OutPoints)
{
	OutPoints.Reset();
	if (Radius <= 0.f || NumSegments < 1 || FMath::IsNearlyZero(AngleDeg))
	{
		return;
	}

	// Centre de courbure : a gauche du sens de marche si Angle > 0 (virage a
	// gauche), a droite si Angle < 0. Meme convention que le yaw UE (rotation
	// positive de +X vers +Y).
	const float YawRad = FMath::DegreesToRadians(Yaw);
	const FVector LeftPerp(-FMath::Sin(YawRad), FMath::Cos(YawRad), 0.f);
	const float SignAngle = FMath::Sign(AngleDeg);
	const FVector Center = Start + LeftPerp * (Radius * SignAngle);

	const FVector StartOffset = Start - Center;
	const float StartAngleRad = FMath::Atan2(StartOffset.Y, StartOffset.X);

	OutPoints.Reserve(NumSegments + 1);
	for (int32 i = 0; i <= NumSegments; ++i)
	{
		const float T = (float)i / NumSegments;
		const float SweepRad = FMath::DegreesToRadians(AngleDeg * T);
		const float PtAngleRad = StartAngleRad + SweepRad;
		const FVector Pt = Center + FVector(FMath::Cos(PtAngleRad), FMath::Sin(PtAngleRad), 0.f) * Radius;
		OutPoints.Add(FVector(Pt.X, Pt.Y, Start.Z));
	}
}

TArray<AActor*> UBlockoutGeometrySubsystem::GenerateCurvedTunnel(
	FVector Start, float Yaw, float Radius, float AngleDeg, float Width, float Height,
	float WallThickness, int32 NumSegments, FString TunnelName)
{
	TArray<AActor*> NewActors;
	if (Radius <= 0.f || Width <= 0.f || Height <= 0.f || FMath::IsNearlyZero(AngleDeg) || NumSegments < 1)
	{
		return NewActors;
	}

	TArray<FVector> Points;
	ComputeCurvedTunnelPoints(Start, Yaw, Radius, AngleDeg, NumSegments, Points);
	if (Points.Num() < 2)
	{
		return NewActors;
	}

	for (int32 i = 0; i < NumSegments; ++i)
	{
		const FString SegName = FString::Printf(TEXT("%s_Seg%02d"), *TunnelName, i + 1);
		NewActors.Append(GenerateCorridor(Points[i], Points[i + 1], Width, Height, WallThickness, SegName));
	}
	return NewActors;
}

// ---------------------------------------------------------------------------
// FindActorsByLabelContains / SwapActorsToMesh
// ---------------------------------------------------------------------------
//
// Migre depuis SBlockoutToolPanel::FindActorsMatchingSwapIdentifier/
// OnSwapReplaceClicked (2026-08-10, voir Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md
// item #2) -- meme logique, aucune transaction editeur ici (reste la
// responsabilite de l'appelant Slate, voir commentaire dans le .h).

TArray<AActor*> UBlockoutGeometrySubsystem::FindActorsByLabelContains(
	UObject* WorldContext, FString Identifier)
{
	TArray<AActor*> Matches;
	if (Identifier.IsEmpty())
	{
		return Matches;
	}

	UWorld* World = nullptr;
	if (WorldContext)
	{
		World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull);
	}
	if (!World)
	{
		World = RG_GetEditorWorld();
	}
	if (!World)
	{
		return Matches;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A && A->GetActorLabel().Contains(Identifier))
		{
			Matches.Add(A);
		}
	}
	return Matches;
}

TArray<AActor*> UBlockoutGeometrySubsystem::SwapActorsToMesh(
	UObject* WorldContext, const TArray<AActor*>& OldActors, UStaticMesh* NewMesh)
{
	TArray<AActor*> NewActors;
	if (!NewMesh || OldActors.Num() == 0)
	{
		return NewActors;
	}

	UWorld* World = nullptr;
	if (WorldContext)
	{
		World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull);
	}
	if (!World)
	{
		World = RG_GetEditorWorld();
	}
	if (!World)
	{
		return NewActors;
	}

	UEditorActorSubsystem* EAS = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;

	for (AActor* Old : OldActors)
	{
		if (!Old)
		{
			continue;
		}

		// Proprietes a conserver (transform + tags/label/dossier Outliner).
		const FVector  Loc   = Old->GetActorLocation();
		const FRotator Rot   = Old->GetActorRotation();
		const FString  Label = Old->GetActorLabel();
		const TArray<FName> OldTags = Old->Tags;
		const FName Folder = Old->GetFolderPath();
		EComponentMobility::Type Mobility = EComponentMobility::Static;
		if (USceneComponent* OldRoot = Old->GetRootComponent())
		{
			Mobility = OldRoot->Mobility;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* NewActor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), Loc, Rot, SpawnParams);
		if (!NewActor)
		{
			continue;
		}

		if (UStaticMeshComponent* SMC = NewActor->GetStaticMeshComponent())
		{
			SMC->SetMobility(Mobility);
			SMC->SetStaticMesh(NewMesh);
		}
		// Echelle NATIVE (1,1,1) -- un kit d'art modulaire est concu a la bonne
		// taille, l'etirer deformerait le mesh.
		NewActor->SetActorScale3D(FVector::OneVector);
		NewActor->Tags = OldTags;
		NewActor->SetFolderPath(Folder);

		// Detruire l'ancienne AVANT de reprendre son label, sinon UE5 le voit
		// comme deja pris et suffixe le nouveau ("_2").
		if (EAS)
		{
			EAS->DestroyActor(Old);
		}
		else
		{
			Old->Destroy();
		}
		NewActor->SetActorLabel(Label);

		NewActors.Add(NewActor);
	}
	return NewActors;
}

// ---------------------------------------------------------------------------
// Alignement sur grille / Duplication en serie / Generateur de plan 2D --
// noyaux purs extraits le 2026-08-10 (voir
// Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md). Aucune de ces 3 fonctions ne
// touche a un acteur, un monde ou une texture -- pur calcul, testable a
// l'identique en Python sans setup/teardown de scene.
// ---------------------------------------------------------------------------

FVector UBlockoutGeometrySubsystem::SnapToGrid(FVector Position, float GridSize, bool bEnabled) const
{
	if (!bEnabled)
	{
		return Position;
	}
	const float G = FMath::Max(1.f, GridSize);
	return FVector(
		FMath::RoundToFloat(Position.X / G) * G,
		FMath::RoundToFloat(Position.Y / G) * G,
		FMath::RoundToFloat(Position.Z / G) * G);
}

TArray<FVector> UBlockoutGeometrySubsystem::ComputeSeriesOffsets(float YawDeg, float Spacing, int32 Count) const
{
	TArray<FVector> Out;
	if (Count <= 0)
	{
		return Out;
	}
	const float YawRad = FMath::DegreesToRadians(YawDeg);
	const FVector Dir(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
	Out.Reserve(Count);
	for (int32 i = 1; i <= Count; ++i)
	{
		Out.Add(Dir * (Spacing * i));
	}
	return Out;
}

void UBlockoutGeometrySubsystem::ComputePlanWallRects(const TArray<uint8>& Mask, int32 ImgW, int32 ImgH, int32 Cell,
	float UUPerPixel, float WallHeight, float PosX, float PosY, float PosZ,
	TArray<FBlockoutPlanWallRect>& OutWalls) const
{
	OutWalls.Reset();
	if (ImgW <= 0 || ImgH <= 0 || Cell <= 0 || Mask.Num() != ImgW * ImgH)
	{
		return;
	}

	// ── Echantillonnage sur une grille de cellules de Cell x Cell pixels --
	// meme regle que l'ancien handler : une cellule compte comme mur des
	// qu'AU MOINS UN de ses pixels est sombre, pour ne pas perdre un trait fin.
	const int32 GW = FMath::DivideAndRoundUp(ImgW, Cell);
	const int32 GH = FMath::DivideAndRoundUp(ImgH, Cell);
	TArray<uint8> Grid;
	Grid.SetNumZeroed(GW * GH);
	for (int32 py = 0; py < ImgH; ++py)
	{
		for (int32 px = 0; px < ImgW; ++px)
		{
			if (Mask[py * ImgW + px])
			{
				Grid[(py / Cell) * GW + (px / Cell)] = 1;
			}
		}
	}

	// ── Fusion gloutonne en rectangles maximaux -- identique a l'ancien handler.
	struct FPlanRect { int32 X = 0, Y = 0, W = 0, H = 0; };
	TArray<FPlanRect> Rects;
	TArray<uint8> Used;
	Used.SetNumZeroed(GW * GH);

	for (int32 gy = 0; gy < GH; ++gy)
	{
		for (int32 gx = 0; gx < GW; ++gx)
		{
			const int32 Idx = gy * GW + gx;
			if (!Grid[Idx] || Used[Idx])
			{
				continue;
			}

			int32 RW = 1;
			while (gx + RW < GW)
			{
				const int32 I = gy * GW + gx + RW;
				if (!Grid[I] || Used[I]) { break; }
				++RW;
			}

			int32 RH = 1;
			while (gy + RH < GH)
			{
				bool bRowOk = true;
				for (int32 i = 0; i < RW; ++i)
				{
					const int32 I = (gy + RH) * GW + gx + i;
					if (!Grid[I] || Used[I]) { bRowOk = false; break; }
				}
				if (!bRowOk) { break; }
				++RH;
			}

			for (int32 y = gy; y < gy + RH; ++y)
			{
				for (int32 x = gx; x < gx + RW; ++x)
				{
					Used[y * GW + x] = 1;
				}
			}
			Rects.Add({ gx, gy, RW, RH });
		}
	}

	if (Rects.Num() == 0)
	{
		return;
	}

	// ── Placement monde : le plan est CENTRE sur (PosX, PosY) -- identique a
	// l'ancien handler, y compris l'inversion de l'axe Y (ligne 0 de l'image en
	// haut, pour que le plan apparaisse dans le meme sens dans le viewport que
	// dans l'editeur d'image).
	const float PlanWorldW = ImgW * UUPerPixel;
	const float PlanWorldH = ImgH * UUPerPixel;
	const float OriginX = PosX - PlanWorldW / 2.f;
	const float OriginY = PosY - PlanWorldH / 2.f;

	OutWalls.Reserve(Rects.Num());
	for (const FPlanRect& R : Rects)
	{
		const int32 PxMinX = R.X * Cell;
		const int32 PxMinY = R.Y * Cell;
		const int32 PxMaxX = FMath::Min((R.X + R.W) * Cell, ImgW);
		const int32 PxMaxY = FMath::Min((R.Y + R.H) * Cell, ImgH);

		const float SizeX = (PxMaxX - PxMinX) * UUPerPixel;
		const float SizeY = (PxMaxY - PxMinY) * UUPerPixel;
		if (SizeX <= 0.f || SizeY <= 0.f)
		{
			continue;
		}

		const float CenterPxX = (PxMinX + PxMaxX) * 0.5f * UUPerPixel;
		const float CenterPxY = (PxMinY + PxMaxY) * 0.5f * UUPerPixel;

		FBlockoutPlanWallRect Wall;
		Wall.Location = FVector(
			OriginX + CenterPxX,
			OriginY + (PlanWorldH - CenterPxY),
			PosZ + WallHeight / 2.f);
		Wall.Size = FVector(SizeX, SizeY, WallHeight);
		OutWalls.Add(Wall);
	}
}
