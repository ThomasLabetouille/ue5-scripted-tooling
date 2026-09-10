#include "SitSurfaceLibrary.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"

namespace
{
	// Proportions du domaine, exprimees en fraction de la HAUTEUR TOTALE du personnage.
	//
	// Ce qui est sittable depend de la taille de celui qui s'assoit : un rebord de 80 cm est un
	// siege pour un adulte et un mur pour un enfant. Ces ratios donnent, pour le mannequin UE
	// standard (176 cm), les valeurs qu'on aurait ecrites a la main -- mais ils restent justes
	// si le mannequin change.
	constexpr float RatioMaxSeat = 0.50f;   // hauteur de hanche
	constexpr float RatioLegReach = 0.27f;  // sol atteignable par les pieds, assis
	constexpr float RatioMinDepth = 0.20f;
	constexpr float RatioMinWidth = 0.25f;
	constexpr float RatioHeadroom = 0.75f;
	constexpr float RatioSearch = 0.50f;

	// Hauteur d'origine de l'animation d'assise du banc, et demi-plage sur laquelle le Motion
	// Warping peut la deformer sans que ca se voie.
	//
	// A MESURER puis corriger ici : la valeur ci-dessous est une estimation. La mesure se fait
	// en PIE, en relevant la hauteur du bone pelvis pendant la boucle d'assise sur le banc du
	// sample. Tant qu'elle n'est pas faite, tout resultat de test sur la plage supportee est a
	// prendre comme provisoire.
	constexpr float BenchSeatHeight = 45.f;
	constexpr float WarpTolerance = 15.f;

	/** Trace descendante simple. Renvoie false si rien n'est touche. */
	bool TraceDown(UWorld* World, const FVector& From, float Distance,
		const FCollisionQueryParams& Params, FHitResult& OutHit)
	{
		return World->LineTraceSingleByChannel(
			OutHit, From, From - FVector(0.f, 0.f, Distance), ECC_Visibility, Params);
	}
}

FSitDomain USitSurfaceLibrary::GetDefaultDomain()
{
	return FSitDomain();
}

FSitDomain USitSurfaceLibrary::MakeDomainForCharacter(AActor* Character)
{
	FSitDomain Domain;
	if (!Character)
	{
		return Domain;
	}

	const UCapsuleComponent* Capsule = Character->FindComponentByClass<UCapsuleComponent>();
	if (!Capsule)
	{
		// Pas de capsule : on rend les valeurs par defaut plutot que de calculer sur une taille
		// inventee. Un domaine faux est pire qu'un domaine generique.
		return Domain;
	}

	const float TotalHeight = Capsule->GetScaledCapsuleHalfHeight() * 2.f;
	if (TotalHeight <= KINDA_SMALL_NUMBER)
	{
		return Domain;
	}

	// Negative : une marche ou une bordure sous les pieds reste une assise valable.
	Domain.MinSeatHeight = -TotalHeight * RatioLegReach;
	Domain.MaxSeatHeight = TotalHeight * RatioMaxSeat;
	Domain.LegReach      = TotalHeight * RatioLegReach;
	Domain.MinDepth      = TotalHeight * RatioMinDepth;
	Domain.MinWidth      = TotalHeight * RatioMinWidth;
	Domain.MinHeadroom   = TotalHeight * RatioHeadroom;
	Domain.SearchDistance = TotalHeight * RatioSearch;
	return Domain;
}

FSitSurfaceResult USitSurfaceLibrary::FindSitSurfaceForCharacter(AActor* Character)
{
	return FindSitSurface(Character, MakeDomainForCharacter(Character));
}

void USitSurfaceLibrary::GetSupportedHeightRange(float& OutMinHeight, float& OutMaxHeight)
{
	OutMinHeight = BenchSeatHeight - WarpTolerance;
	OutMaxHeight = BenchSeatHeight + WarpTolerance;
}

FSitSurfaceResult USitSurfaceLibrary::FindSitSurface(AActor* Character, const FSitDomain& Domain)
{
	FSitSurfaceResult Result;

	if (!Character || !Character->GetWorld())
	{
		Result.RejectReason = TEXT("AucuneSurface");
		return Result;
	}

	UWorld* const World = Character->GetWorld();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FindSitSurface), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(Character);

	const FVector Origin = Character->GetActorLocation();
	const FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();

	// 1. Le sol sous le personnage : son point ET sa normale.
	//
	// La normale compte autant que l'altitude. Toutes les hauteurs sont mesurees par rapport au
	// PLAN de ce sol, pas a son altitude ponctuelle -- sinon, sur un terrain en pente, le sol
	// situe devant apparait plus haut et se fait prendre pour un rebord. Signale par le joueur :
	// des sols pentus etaient refuses alors qu'on doit pouvoir s'y asseoir.
	FHitResult GroundHit;
	if (!TraceDown(World, Origin, 500.f, Params, GroundHit))
	{
		Result.RejectReason = TEXT("PasDeSol");
		return Result;
	}
	const FVector GroundPoint = GroundHit.ImpactPoint;
	const FVector GroundNormal = GroundHit.ImpactNormal;

	// Altitude qu'aurait le sol en un point donne, si sa pente se prolongeait.
	auto AltitudeSolAttendue = [&](float X, float Y) -> float
	{
		if (FMath::Abs(GroundNormal.Z) < KINDA_SMALL_NUMBER)
		{
			return GroundPoint.Z;
		}
		return GroundPoint.Z
			- (GroundNormal.X * (X - GroundPoint.X) + GroundNormal.Y * (Y - GroundPoint.Y))
			  / GroundNormal.Z;
	};

	// 2. Chercher la surface devant, dans la bande sittable mesuree depuis le plan du sol.
	const FVector Probe = Origin + Forward * Domain.SearchDistance;
	const float SolAuProbe = AltitudeSolAttendue(Probe.X, Probe.Y);
	const FVector SeatTraceTop(Probe.X, Probe.Y, SolAuProbe + Domain.MaxSeatHeight + 2.f);

	// La trace doit descendre jusqu'AU-DESSOUS du plan des pieds, sinon une surface plus basse
	// que le personnage est purement invisible -- symptome observe en playtest : "AucuneSurface"
	// en descendant d'une plateforme.
	const float LongueurTrace =
		(Domain.MaxSeatHeight - Domain.MinSeatHeight) + 34.f;

	FHitResult SurfaceHit;
	const bool bHitInBand = TraceDown(World, SeatTraceTop, LongueurTrace, Params, SurfaceHit);

	// bStartPenetrating : la trace a demarre DANS la geometrie. UE renvoie alors un hit au point
	// de depart -- ce n'est pas une surface, c'est l'endroit d'ou l'on regarde.
	if (!bHitInBand || SurfaceHit.bStartPenetrating)
	{
		FHitResult AboveHit;
		const FVector HighTop(Probe.X, Probe.Y,
			SolAuProbe + Domain.MaxSeatHeight + Domain.MinHeadroom);
		const bool bSomethingAbove =
			TraceDown(World, HighTop, Domain.MinHeadroom + 4.f, Params, AboveHit);
		Result.RejectReason = bSomethingAbove ? TEXT("TropHaut") : TEXT("AucuneSurface");
		if (bSomethingAbove)
		{
			Result.SeatHeight = AboveHit.ImpactPoint.Z - SolAuProbe;
		}
		return Result;
	}

	const FVector Seat = SurfaceHit.ImpactPoint;
	const FVector SeatNormal = SurfaceHit.ImpactNormal;
	const float SeatHeight = Seat.Z - AltitudeSolAttendue(Seat.X, Seat.Y);
	Result.SeatHeight = SeatHeight;
	Result.SeatLocation = Seat;

	// 3. Pente de la surface. Le plafond par defaut suit la pente sur laquelle le personnage
	//    peut tenir debout : ce qu'on peut arpenter, on doit pouvoir s'y asseoir.
	if (SeatNormal.Z < FMath::Cos(FMath::DegreesToRadians(Domain.MaxSlopeDegrees)))
	{
		Result.RejectReason = TEXT("TropIncline");
		return Result;
	}

	// Altitude attendue sur le PLAN DE LA SURFACE, pour les controles d'etendue. Comparer a une
	// altitude constante rejetait toute surface inclinee : a 35 cm de profondeur, une pente de
	// 15 degres fait deja 9 cm de denivele, soit plus que la tolerance. D'ou le "PasAssezProfond"
	// signale sur des sols pentus.
	auto AltitudeSurfaceAttendue = [&](const FVector& Offset) -> float
	{
		if (FMath::Abs(SeatNormal.Z) < KINDA_SMALL_NUMBER)
		{
			return Seat.Z;
		}
		return Seat.Z - (SeatNormal.X * Offset.X + SeatNormal.Y * Offset.Y) / SeatNormal.Z;
	};

	// La surface ne doit pas SE DEROBER. Monter n'est pas un defaut.
	//
	// Signale par le joueur : debout au bord d'un rebord, la sonde trouvait le SOL, puis son
	// controle de profondeur tombait sur le rebord -- plus haut -- et concluait "PasAssezProfond".
	// C'etait faux : s'asseoir par terre avec un rebord derriere soi, c'est s'asseoir avec un
	// dossier. Seul un affaissement condamne l'assise, parce qu'on tomberait dans le vide.
	//
	// La trace part donc de tres haut (de quoi voir un dossier jusqu'a la hauteur max du domaine)
	// et un demarrage EN PENETRATION signifie qu'il y a du solide : c'est un dossier tres haut,
	// pas un trou.
	auto SurfaceNeSeDerobePas = [&](const FVector& Offset) -> bool
	{
		const FVector Depart = Seat + Offset + FVector(0.f, 0.f, Domain.MaxSeatHeight);
		FHitResult Hit;
		if (!TraceDown(World, Depart, Domain.MaxSeatHeight + 40.f, Params, Hit))
		{
			return false;   // rien du tout sous le point : le vide
		}
		if (Hit.bStartPenetrating)
		{
			return true;    // du solide des le depart : un dossier, pas un trou
		}
		const float Ecart = Hit.ImpactPoint.Z - AltitudeSurfaceAttendue(Offset);
		return Ecart >= -6.f;
	};

	// 4. Profondeur : la surface doit se prolonger vers l'arriere du siege.
	if (!SurfaceNeSeDerobePas(Forward * Domain.MinDepth))
	{
		Result.RejectReason = TEXT("PasAssezProfond");
		return Result;
	}

	// 5. Largeur : des DEUX cotes. Un seul suffirait a valider une arete.
	const float Half = Domain.MinWidth * 0.5f;
	if (!SurfaceNeSeDerobePas(Right * Half) || !SurfaceNeSeDerobePas(Right * -Half))
	{
		Result.RejectReason = TEXT("PasAssezLarge");
		return Result;
	}

	// 6. Degagement au-dessus du siege.
	{
		FHitResult CeilingHit;
		if (World->LineTraceSingleByChannel(CeilingHit, Seat + FVector(0.f, 0.f, 5.f),
			Seat + FVector(0.f, 0.f, Domain.MinHeadroom), ECC_Visibility, Params))
		{
			Result.RejectReason = TEXT("PlafondBas");
			return Result;
		}
	}

	// 7. Le siege doit etre ATTEIGNABLE depuis le personnage.
	//
	// Sans ce controle, la sonde valide une surface situee DERRIERE un obstacle : elle trace
	// vers le bas a hauteur de SearchDistance, et si un mur mince se trouve entre les deux, elle
	// mesure le sol de l'autre cote. Signale par le joueur : un cube trop haut mais fin
	// annoncait "on peut s'asseoir" en detectant le sol derriere lui.
	{
		FHitResult BlockHit;
		if (World->LineTraceSingleByChannel(BlockHit, Origin, Seat + FVector(0.f, 0.f, 20.f),
			ECC_Visibility, Params))
		{
			Result.RejectReason = TEXT("Obstrue");
			return Result;
		}
	}

	// 8. Valide. On assoit le personnage DOS a la surface : il regarde d'ou il vient.
	Result.bValid = true;
	Result.SeatRotation = (-Forward).Rotation();

	const float Span = FMath::Max(Domain.MaxSeatHeight - Domain.MinSeatHeight, 1.f);
	Result.NormalizedHeight = FMath::Clamp((SeatHeight - Domain.MinSeatHeight) / Span, 0.f, 1.f);
	Result.bLegsDangle = SeatHeight > Domain.LegReach;

	float MinAnim, MaxAnim;
	GetSupportedHeightRange(MinAnim, MaxAnim);
	Result.bWithinAnimationRange = (SeatHeight >= MinAnim && SeatHeight <= MaxAnim);

	return Result;
}
