#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SitSurfaceLibrary.generated.h"

/**
 * Frontiere du domaine "sittable" : la definition de "quand c'est possible". En dehors, il ne se
 * passe rien.
 *
 * Les valeurs sont en centimetres parce que les traces le sont, mais elles ne doivent PAS etre
 * ecrites a la main : voir MakeDomainForCharacter(), qui les derive de la taille du personnage.
 * Les valeurs par defaut ci-dessous correspondent au mannequin UE standard (176 cm) et ne sont
 * la que comme repli.
 */
USTRUCT(BlueprintType)
struct BTAUTHORINGKITRUNTIME_API FSitDomain
{
	GENERATED_BODY()

	/**
	 * Hauteur de siege minimale, NEGATIVE : on peut s'asseoir sur une surface situee SOUS ses
	 * pieds -- une marche, une bordure, le bord d'une plateforme.
	 *
	 * A zero, l'agent de playtest a journalise "AucuneSurface" et "PasAssezProfond" a chaque
	 * fois que le personnage descendait de la plateforme centrale de DefaultLevel : le sol
	 * d'arrivee, plus bas que ses pieds, etait rejete comme "TropBas" et la trace ne descendait
	 * meme pas assez loin pour le voir. Trouve en promenant le personnage, pas en relisant le
	 * code.
	 *
	 * Calee sur la longueur de jambe : on s'assoit sur ce qu'on peut enjamber en descendant.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Units = "Centimeters"))
	float MinSeatHeight = -47.f;

	/** Hauteur de hanche. Au-dela il faut grimper avant de s'asseoir : ce n'est plus une assise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Units = "Centimeters"))
	float MaxSeatHeight = 88.f;

	/** Sans cette profondeur, on est assis sur une arete, pas sur un siege. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Units = "Centimeters"))
	float MinDepth = 35.f;

	/** Largeur de bassin plus une marge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Units = "Centimeters"))
	float MinWidth = 44.f;

	/**
	 * Pente maximale d'une surface d'assise.
	 *
	 * Calee sur la pente qu'un personnage peut arpenter (44 degres par defaut dans UE) et non
	 * sur un confort theorique : la regle voulue est "ce sur quoi on peut tenir debout, on doit
	 * pouvoir s'y asseoir". A 15 degres, des sols pentus parfaitement praticables etaient
	 * refuses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Units = "Degrees"))
	float MaxSlopeDegrees = 44.f;

	/** Ne pas s'asseoir sous un plafond bas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Units = "Centimeters"))
	float MinHeadroom = 132.f;

	/** Distance devant le personnage ou l'on cherche une surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Units = "Centimeters"))
	float SearchDistance = 88.f;

	/**
	 * Longueur de jambe utile : au-dessus de cette hauteur de siege, les pieds ne touchent plus
	 * le sol et les jambes pendent. Sert a choisir la pose, pas a valider le domaine.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Units = "Centimeters"))
	float LegReach = 47.f;
};

/**
 * Resultat d'une recherche de surface d'assise.
 *
 * NOTE DE CONCEPTION : `RejectReason` n'est pas du confort de debug, c'est ce qui rend AC-1
 * testable. Un test qui ne lit qu'un booleen ne peut pas distinguer "refuse pour la bonne
 * raison" de "refuse par accident" -- et un systeme qui refuse tout passerait tous les cas
 * negatifs sans rien valoir. Meme famille de piege que le zero silencieux d'une cle Blackboard
 * absente.
 */
USTRUCT(BlueprintType)
struct BTAUTHORINGKITRUNTIME_API FSitSurfaceResult
{
	GENERATED_BODY()

	/** La surface satisfait tout le domaine. */
	UPROPERTY(BlueprintReadOnly)
	bool bValid = false;

	/** Motif de refus, vide si valide. Valeurs : TropHaut, TropBas, TropIncline, PasAssezProfond, PasAssezLarge, PlafondBas, Obstrue, AucuneSurface, PasDeSol. */
	UPROPERTY(BlueprintReadOnly)
	FString RejectReason;

	/** Ou poser le bassin. */
	UPROPERTY(BlueprintReadOnly)
	FVector SeatLocation = FVector::ZeroVector;

	/** Orientation d'assise : le personnage tourne le dos a la surface. */
	UPROPERTY(BlueprintReadOnly)
	FRotator SeatRotation = FRotator::ZeroRotator;

	/** Hauteur du siege au-dessus du sol du personnage. */
	UPROPERTY(BlueprintReadOnly)
	float SeatHeight = 0.f;

	/** Hauteur ramenee dans [0,1] sur le domaine. Axe principal du Blend Space. */
	UPROPERTY(BlueprintReadOnly)
	float NormalizedHeight = 0.f;

	/** Les jambes pendent (siege plus haut que LegReach) plutot que d'avoir les pieds au sol. */
	UPROPERTY(BlueprintReadOnly)
	bool bLegsDangle = false;

	/**
	 * Vrai si les animations disponibles couvrent cette hauteur sans deformation visible.
	 *
	 * PUREMENT INDICATIF : ne JAMAIS s'en servir pour empecher une assise. Decision du
	 * 2026-08-29 -- ce qui compte est que le joueur PUISSE s'asseoir partout ou la geometrie le
	 * permet ; une animation approximative est acceptable, une assise refusee ne l'est pas.
	 * Ce drapeau sert a mesurer la couverture animée, pas a la faire respecter.
	 */
	UPROPERTY(BlueprintReadOnly)
	bool bWithinAnimationRange = false;
};

/**
 * Detection procedurale de surface d'assise (specification : Docs/ASSISE_CONTEXTUELLE.md).
 *
 * Aucune annotation manuelle du niveau : dans un monde en cubes, chaque rebord est un siege
 * potentiel et les annoter un par un serait absurde. La sonde trace, valide, et rend une
 * transform de siege plus les deux informations dont le Blend Space a besoin.
 *
 * Ne joue aucune animation et ne modifie rien : elle est donc testable seule, avant qu'aucune
 * animation n'existe.
 */
UCLASS()
class BTAUTHORINGKITRUNTIME_API USitSurfaceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Cherche une surface d'assise devant le personnage. Ne modifie rien. */
	UFUNCTION(BlueprintCallable, Category = "Assise")
	static FSitSurfaceResult FindSitSurface(AActor* Character, const FSitDomain& Domain);

	/** Comme FindSitSurface, mais avec le domaine derive du personnage lui-meme. A preferer. */
	UFUNCTION(BlueprintCallable, Category = "Assise")
	static FSitSurfaceResult FindSitSurfaceForCharacter(AActor* Character);

	/**
	 * Construit le domaine a partir des mesures reelles du personnage.
	 *
	 * Ce qui est sittable depend de la TAILLE de celui qui s'assoit : un rebord de 80 cm est un
	 * siege pour un adulte et un mur pour un enfant. Toutes les distances sont donc des
	 * proportions de la hauteur du personnage, lue sur sa capsule de collision -- et non des
	 * constantes qui deviendraient fausses des qu'on change de mannequin.
	 *
	 * Repli sur les valeurs par defaut du struct si le personnage n'a pas de capsule.
	 */
	UFUNCTION(BlueprintPure, Category = "Assise")
	static FSitDomain MakeDomainForCharacter(AActor* Character);

	/** Domaine par defaut (mannequin UE standard). Repli : preferer MakeDomainForCharacter. */
	UFUNCTION(BlueprintPure, Category = "Assise")
	static FSitDomain GetDefaultDomain();

	/**
	 * Plage de hauteur reellement couverte par les animations presentes.
	 *
	 * Aujourd'hui : la seule animation de banc, donc une plage etroite autour de sa hauteur
	 * d'origine. Les tests lisent CETTE fonction plutot que des valeurs en dur -- quand les
	 * poses d'ancrage arriveront, la plage s'elargira et les criteres d'acceptation resteront
	 * vrais sans etre reecrits.
	 */
	UFUNCTION(BlueprintPure, Category = "Assise")
	static void GetSupportedHeightRange(float& OutMinHeight, float& OutMaxHeight);
};
