#include "PatrolPerceptionAIController.h"

#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"

APatrolPerceptionAIController::APatrolPerceptionAIController()
{
	// PerceptionComponent est deja declare (public) sur AAIController -- on l'instancie ici.
	PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));

	// CORRECTIF 2026-08-15 (bug de fond, present depuis la creation de cette classe) :
	// ConfigureSense() DOIT etre appele ici, dans le constructeur, et PAS en BeginPlay().
	// Symptome observe : le PNJ ne voyait jamais le joueur, et le log crachait
	// "LogAIPerception: Warning: Listener must have a valid id to update its sense config"
	// une fois par PNJ. Cause : en BeginPlay() le composant de perception s'est deja enregistre
	// aupres du systeme de perception ; ConfigureSense() a ce moment-la est refuse et le sens
	// Sight n'est jamais reellement souscrit -- les valeurs paraissent correctes en inspectant
	// SensesConfig, mais elles sont inertes (aucun stimulus n'arrive jamais).
	// Les valeurs sont (re)appliquees en BeginPlay() pour prendre en compte une Blueprint enfant
	// qui surchargerait SightRadius/LoseSightRadius/PeripheralVisionHalfAngleDegrees.
	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionHalfAngleDegrees;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());

	// La tolerance s'evalue dans le temps (habituation), donc ce controller doit ticker.
	// AAIController ne l'active pas forcement selon la classe parente : on l'impose ici.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void APatrolPerceptionAIController::BeginPlay()
{
	Super::BeginPlay();

	if (SightConfig && PerceptionComponent)
	{
		// Le sens est deja enregistre par le constructeur (cf. commentaire la-bas). Ici on ne fait
		// que reappliquer les valeurs -- utile si une Blueprint enfant les a surchargees -- puis on
		// demande une mise a jour du listener. Ne PAS rappeler ConfigureSense() ici : a ce stade le
		// listener est deja enregistre et l'appel serait refuse ("Listener must have a valid id").
		SightConfig->SightRadius = SightRadius;
		SightConfig->LoseSightRadius = LoseSightRadius;
		SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionHalfAngleDegrees;

		PerceptionComponent->RequestStimuliListenerUpdate();
		PerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(this, &APatrolPerceptionAIController::HandleTargetPerceptionUpdated);
	}
}

void APatrolPerceptionAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (BehaviorTreeAsset)
	{
		// RunBehaviorTree cree le UBlackboardComponent (via l'asset Blackboard reference par le
		// Behavior Tree) -- PopulatePatrolAndSitKeysFromLevel() doit donc etre appelee APRES, pas
		// avant, sans quoi GetBlackboardComponent() renverrait encore nullptr.
		RunBehaviorTree(BehaviorTreeAsset);
		PopulatePatrolAndSitKeysFromLevel();
	}
}

void APatrolPerceptionAIController::PopulatePatrolAndSitKeysFromLevel()
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	TArray<AActor*> PatrolActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), PatrolPointTag, PatrolActors);

	for (int32 Index = 0; Index < PatrolPointKeyNames.Num(); ++Index)
	{
		if (PatrolActors.Num() == 0)
		{
			break;
		}
		// Si moins de points trouves que de cles a remplir, on reboucle sur les points deja
		// trouves plutot que de laisser des cles vides (evite un MoveTo vers une cible nulle).
		AActor* PatrolActor = PatrolActors[Index % PatrolActors.Num()];
		BB->SetValueAsObject(PatrolPointKeyNames[Index], PatrolActor);
	}

	TArray<AActor*> SitActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), SitPointTag, SitActors);
	if (SitActors.Num() > 0)
	{
		BB->SetValueAsObject(SitPointKeyName, SitActors[0]);
	}
}

void APatrolPerceptionAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!Actor || !BB)
	{
		return;
	}

	// Ne reagit qu'au joueur local -- evite de "voir" d'autres PNJ si le niveau en contient
	// plusieurs utilisant le meme controller.
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (Actor != PlayerPawn)
	{
		return;
	}

	if (Stimulus.WasSuccessfullySensed())
	{
		BB->SetValueAsObject(TargetActorKeyName, Actor);
	}
	else
	{
		BB->ClearValue(TargetActorKeyName);
	}
}

// ----------------------------------------------------------------------------------------
// TOLERANCE ET HABITUATION  (M2 / M3)
// ----------------------------------------------------------------------------------------

void APatrolPerceptionAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Evaluation cadencee plutot qu'a chaque frame : la tolerance est une grandeur lente, et
	// six sujets qui recalculent une distance a 120 Hz ne servent a rien.
	EvaluationAccumulator += DeltaSeconds;
	const float Period = 1.f / FMath::Max(EvaluationRateHz, 1.f);
	if (EvaluationAccumulator < Period)
	{
		return;
	}

	const float Elapsed = EvaluationAccumulator;
	EvaluationAccumulator = 0.f;
	EvaluateAwareness(Elapsed);
}

void APatrolPerceptionAIController::EvaluateAwareness(float DeltaSeconds)
{
	APawn* const SelfPawn = GetPawn();
	APawn* const PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!SelfPawn || !PlayerPawn)
	{
		return;
	}

	const FVector SelfLocation = SelfPawn->GetActorLocation();
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	const float Distance = FVector::Dist2D(SelfLocation, PlayerLocation);

	// Seuils effectifs : l'habituation les RESSERRE, c'est-a-dire qu'elle laisse le joueur
	// approcher plus pres avant de declencher le meme niveau.
	const float Tolerance = FMath::Clamp(Habituation, 0.f, MaxHabituation);
	CurrentAlertThreshold = AlertDistance * (1.f - Tolerance);
	CurrentFleeThreshold = FleeDistance * (1.f - Tolerance);

	int32 NewLevel = 0;
	if (Distance <= CurrentFleeThreshold)
	{
		NewLevel = 2;
	}
	else if (Distance <= CurrentAlertThreshold)
	{
		NewLevel = 1;
	}

	// Habituation : elle monte tant que le joueur est present sans avoir brusque le sujet.
	// La zone de confort deborde volontairement le seuil d'alerte (HabituationRange > AlertDistance)
	// pour que le joueur puisse s'habituer en restant a distance respectueuse -- c'est la lecture
	// exacte d'AC-3, qui fait patienter le joueur entre 1200 et 2000 cm.
	if (NewLevel < 2 && Distance <= HabituationRange)
	{
		Habituation = FMath::Min(Habituation + HabituationGainPerSecond * DeltaSeconds, MaxHabituation);
	}

	const bool bJustEnteredFlee = (NewLevel == 2 && AwarenessLevel != 2);
	if (bJustEnteredFlee)
	{
		// Brusquer le sujet coute du temps, jamais la partie : l'habituation recule, elle ne
		// s'effondre pas. Pas d'etat d'echec, c'est un principe du cahier des charges.
		Habituation = FMath::Max(Habituation - HabituationLossOnFlee, 0.f);

		FVector FleeLocation;
		if (ComputeFleeLocation(PlayerLocation, FleeLocation))
		{
			if (UBlackboardComponent* BB = GetBlackboardComponent())
			{
				if (BB->GetKeyID(FleeLocationKeyName) != FBlackboard::InvalidKey)
				{
					BB->SetValueAsVector(FleeLocationKeyName, FleeLocation);
				}
			}
		}
	}

	AwarenessLevel = NewLevel;

	if (UBlackboardComponent* BB = GetBlackboardComponent())
	{
		// Verifier l'existence de la cle : SetValueAsInt sur une cle absente journalise un
		// avertissement a CHAQUE appel, soit dix par seconde et par sujet.
		if (BB->GetKeyID(AwarenessKeyName) != FBlackboard::InvalidKey)
		{
			BB->SetValueAsInt(AwarenessKeyName, AwarenessLevel);
		}
	}
}

bool APatrolPerceptionAIController::ComputeFleeLocation(const FVector& PlayerLocation, FVector& OutLocation) const
{
	APawn* const SelfPawn = GetPawn();
	if (!SelfPawn)
	{
		return false;
	}

	const FVector SelfLocation = SelfPawn->GetActorLocation();
	FVector Away = (SelfLocation - PlayerLocation).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		// Sujet et joueur exactement superposes : n'importe quelle direction fait l'affaire.
		Away = SelfPawn->GetActorForwardVector().GetSafeNormal2D();
	}

	const FVector Desired = SelfLocation + Away * FleeTargetDistance;

	// Projeter sur le navmesh : un point de repli non navigable donnerait un MoveTo qui echoue
	// instantanement, et le sujet paraitrait fige -- exactement le symptome du piege 1.
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSystem)
	{
		OutLocation = Desired;
		return true;
	}

	FNavLocation Projected;
	if (NavSystem->ProjectPointToNavigation(Desired, Projected, FVector(500.f, 500.f, 500.f)))
	{
		OutLocation = Projected.Location;
		return true;
	}

	// Pas de point navigable dans cette direction : se rabattre sur un point atteignable au hasard
	// dans le rayon vise, plutot que de ne pas fuir du tout.
	FNavLocation Fallback;
	if (NavSystem->GetRandomReachablePointInRadius(SelfLocation, FleeTargetDistance, Fallback))
	{
		OutLocation = Fallback.Location;
		return true;
	}

	return false;
}
