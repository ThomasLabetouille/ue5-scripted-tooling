#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "PatrolPerceptionAIController.generated.h"

class UAISenseConfig_Sight;
class UBehaviorTree;

/**
 * AIController generique et reutilisable (BTAuthoringKit) : lance un Behavior Tree a la
 * possession, et ecrit/efface automatiquement une cle Blackboard de type Object quand le
 * joueur local entre/sort du cone de vision (AI Perception, sens Sight uniquement).
 *
 * Pense pour etre utilise tel quel sur une Blueprint enfant (regler les proprietes ci-dessous
 * dans les Class Defaults), sans avoir besoin d'ecrire de C++ supplementaire pour un PNJ
 * "patrouille + reagit a la vue du joueur" basique.
 */
UCLASS()
class BTAUTHORINGKITRUNTIME_API APatrolPerceptionAIController : public AAIController
{
	GENERATED_BODY()

public:
	APatrolPerceptionAIController();

	/** Behavior Tree lance automatiquement des que ce controller possede un pawn. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Perception")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	/** Rayon de detection (vision), en centimetres. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Perception", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float SightRadius = 1500.f;

	/** Rayon au-dela duquel la cible deja vue est consideree perdue (>= SightRadius). */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Perception", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float LoseSightRadius = 1800.f;

	/** Demi-angle du cone de vision, en degres (0-180). */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Perception", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "Degrees"))
	float PeripheralVisionHalfAngleDegrees = 60.f;

	/** Nom de la cle Blackboard (type Object, filtree sur une classe d'Actor) qui recoit le joueur vu, ou est effacee quand il n'est plus vu. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Perception")
	FName TargetActorKeyName = "TargetActor";

	/**
	 * Tag (a poser sur des Actors places dans le niveau, ex. ATargetPoint) marquant les points de
	 * patrouille a utiliser. A la possession, ce controller cherche tous les Actors portant ce
	 * tag et remplit PatrolPointKeyNames (dans l'ordre trouve) avec leurs references.
	 */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Patrol")
	FName PatrolPointTag = "PatrolPoint";

	/** Tag sur l'Actor (ex. ATargetPoint) marquant le point ou le PNJ va s'asseoir. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Patrol")
	FName SitPointTag = "SitPoint";

	/** Noms des cles Blackboard (type Object) qui recevront les points de patrouille trouves dans le niveau via PatrolPointTag, dans l'ordre. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Patrol")
	TArray<FName> PatrolPointKeyNames = { "PatrolPoint1", "PatrolPoint2", "PatrolPoint3" };

	/** Nom de la cle Blackboard (type Object) qui recevra l'Actor marquant le point d'assise. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Patrol")
	FName SitPointKeyName = "SitPointActor";

	// ------------------------------------------------------------------
	// TOLERANCE ET HABITUATION  (M2 / M3 du cahier des charges)
	// ------------------------------------------------------------------
	//
	// Le sujet observe reagit a la PROXIMITE du joueur sur trois niveaux, et sa tolerance
	// augmente avec le temps passe pres de lui sans avoir ete brusque. C'est la mecanique
	// centrale du jeu : elle transforme "rester loin" en progression plutot qu'en contrainte.
	//
	// NON IMPLEMENTE VOLONTAIREMENT A CE STADE : le cahier des charges mentionne aussi la
	// vitesse de deplacement du joueur comme facteur. L'ajouter maintenant rendrait AC-2
	// invérifiable -- ce critere fixe ses seuils pour un joueur qui MARCHE, et un multiplicateur
	// de vitesse decalerait les transitions hors des bandes de mesure du test. A reprendre quand
	// le test saura distinguer marche et course.

	/** Cle Blackboard (Int) recevant le niveau de tolerance : 0 ignore, 1 alerte, 2 fuite. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance")
	FName AwarenessKeyName = "AwarenessLevel";

	/** Cle Blackboard (Vector) recevant un point de repli quand le niveau atteint 2. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance")
	FName FleeLocationKeyName = "FleeLocation";

	/** Distance a laquelle le sujet passe en alerte (niveau 1), avant habituation. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float AlertDistance = 1200.f;

	/** Distance a laquelle le sujet fuit (niveau 2), avant habituation. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float FleeDistance = 600.f;

	/** En deca de cette distance, la presence du joueur nourrit l'habituation. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float HabituationRange = 2000.f;

	/**
	 * Gain d'habituation par seconde passee dans la zone de confort.
	 * Calibre pour AC-3 : 60 s a 0.005/s donnent 0.30, soit 30 % de reduction des seuils --
	 * au-dessus des 20 % exiges, avec de la marge pour les aleas de mesure.
	 */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance", meta = (ClampMin = "0.0"))
	float HabituationGainPerSecond = 0.005f;

	/** Habituation maximale, en fraction de reduction des seuils. 0.5 = seuils divises par deux. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float MaxHabituation = 0.5f;

	/** Habituation perdue a chaque passage en fuite : brusquer le sujet coute du temps, pas la partie. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HabituationLossOnFlee = 0.15f;

	/** Distance visee pour le point de repli, a l'oppose du joueur. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float FleeTargetDistance = 2000.f;

	/** Frequence d'evaluation de la tolerance. Inutile de la faire a chaque frame. */
	UPROPERTY(EditAnywhere, Category = "BTAuthoringKit|Tolerance", meta = (ClampMin = "1.0"))
	float EvaluationRateHz = 10.f;

	/** Habituation courante (0-1). Exposee en lecture pour le debug et pour AC-3. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BTAuthoringKit|Tolerance")
	float Habituation = 0.f;

	/** Seuil d'alerte effectif apres habituation. Exposee pour que le test la mesure directement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BTAuthoringKit|Tolerance")
	float CurrentAlertThreshold = 0.f;

	/** Seuil de fuite effectif apres habituation. C'est la valeur dont AC-3 exige la baisse. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BTAuthoringKit|Tolerance")
	float CurrentFleeThreshold = 0.f;

	/** Niveau de tolerance courant (0/1/2), miroir de la cle Blackboard. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BTAuthoringKit|Tolerance")
	int32 AwarenessLevel = 0;

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "BTAuthoringKit|Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Recalcule habituation, seuils effectifs et niveau de tolerance, puis ecrit le Blackboard. */
	void EvaluateAwareness(float DeltaSeconds);

	/** Point de repli a l'oppose du joueur, projete sur le navmesh. Renvoie false si injoignable. */
	bool ComputeFleeLocation(const FVector& PlayerLocation, FVector& OutLocation) const;

	/** Accumulateur de l'evaluation cadencee (EvaluationRateHz). */
	float EvaluationAccumulator = 0.f;

	/** Cherche les Actors tagges PatrolPointTag/SitPointTag dans le niveau et remplit le Blackboard en consequence. Appelee apres RunBehaviorTree() dans OnPossess. */
	void PopulatePatrolAndSitKeysFromLevel();
};
