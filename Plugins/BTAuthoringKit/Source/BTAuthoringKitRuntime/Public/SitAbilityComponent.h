#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "InputCoreTypes.h"

class UInputAction;
class UInputMappingContext;
#include "SitAbilityComponent.generated.h"

/**
 * Capacite de s'asseoir, greffee sur le pawn joueur.
 *
 * POURQUOI UN COMPOSANT ET PAS UNE MODIFICATION DU PAWN
 * -----------------------------------------------------
 * Le pawn du sample (SandboxCharacter_Mover) est un Blueprint central : y toucher par script
 * signifie editer un graphe Blueprint, ce que l'API Python ne sait pas faire de facon stable --
 * et ce projet a deja paye le prix d'un asset abime par une manipulation de graphe.
 *
 * Ici, RIEN du sample n'est modifie. Le composant est cree et attache au pawn a l'execution par
 * USitAbilitySubsystem. Retirer ce plugin suffit a retrouver le projet d'origine, exactement.
 *
 * ETAT ACTUEL, ASSUME
 * -------------------
 * Le personnage entre et sort de l'etat "assis" et memorise le siege detecte, mais il n'est
 * encore ni deplace vers le siege ni fige : ces deux points passent par les mecanismes natifs de
 * Mover (queue_layered_move_activation et queue_next_mode) et font l'objet de l'increment
 * suivant. Ce qui est livre ici est la CAPACITE -- s'asseoir la ou c'est possible, et nulle part
 * ailleurs -- qui est ce qu'AC-1 demande.
 */
UCLASS(ClassGroup = (Assise), meta = (BlueprintSpawnableComponent))
class BTAUTHORINGKITRUNTIME_API USitAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USitAbilityComponent();

	/** Touche d'assise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assise")
	FKey SitKey = EKeys::T;

	/** Priorite du contexte d'input ajoute. Au-dessus de celui du sample, sans le remplacer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assise")
	int32 ContextPriority = 10;

	/**
	 * Input Action creee A L'EXECUTION, et non chargee depuis un asset.
	 *
	 * Deux raisons. D'abord, aucun asset n'est ajoute au Content du sample et IMC_Sandbox n'est
	 * pas modifie : retirer le plugin restaure le projet a l'identique. Ensuite, l'action reste
	 * une vraie UInputAction, donc l'injection Enhanced Input deja eprouvee
	 * (UScriptedInputLibrary) fonctionne dessus sans rien contourner -- le test emprunte le meme
	 * chemin qu'un appui clavier, jusqu'aux triggers et aux modifiers.
	 *
	 * Exposee en lecture precisement pour que les tests puissent l'injecter.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Assise")
	TObjectPtr<UInputAction> SitAction;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> SitContext;

	/** Assis ou non. */
	UPROPERTY(BlueprintReadOnly, Category = "Assise")
	bool bIsSeated = false;

	/** Transform du siege retenu au moment de s'asseoir. */
	UPROPERTY(BlueprintReadOnly, Category = "Assise")
	FVector SeatLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Assise")
	FRotator SeatRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Assise")
	float SeatHeight = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Assise")
	bool bLegsDangle = false;

	/** Motif du dernier refus. Vide si la derniere tentative a reussi. Sert aux tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Assise")
	FString LastRejectReason;

	/** Nombre de tentatives depuis le debut de la partie. Distingue "refuse" de "jamais tente". */
	UPROPERTY(BlueprintReadOnly, Category = "Assise")
	int32 AttemptCount = 0;

	/** Tente de s'asseoir. Renvoie false si aucune surface valide devant le personnage. */
	UFUNCTION(BlueprintCallable, Category = "Assise")
	bool TrySit();

	/** Se releve. Sans effet si le personnage n'est pas assis. */
	UFUNCTION(BlueprintCallable, Category = "Assise")
	void StandUp();

	/** S'assoit si debout, se releve si assis. C'est ce que la touche declenche. */
	UFUNCTION(BlueprintCallable, Category = "Assise")
	void ToggleSit();

	/** Le composant a-t-il reussi a s'abonner a l'input ? Distingue une feature cassee d'un test casse. */
	UPROPERTY(BlueprintReadOnly, Category = "Assise")
	bool bInputBound = false;

	/**
	 * Affichage de debogage a l'ecran et dans le monde.
	 *
	 * Sans lui la feature est INVISIBLE : le personnage entre dans l'etat assis sans animation
	 * ni deplacement, donc appuyer sur la touche ne montre rien et donne l'impression que rien
	 * ne marche. Ce retour visuel est ce qui rend la capacite testable a la main avant que
	 * l'animation existe -- il montre en continu ce que la sonde voit devant le personnage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assise|Debug")
	bool bShowDebug = true;

	// ------------------------------------------------------------------
	// COMPATIBILITE CLAVIER AZERTY
	// ------------------------------------------------------------------
	//
	// Le sample lie sa bascule "mode vol de debug" a une touche clavier directement dans le
	// graphe du pawn (commentaire "Debug Flying Mode - Toggled on Z key"). En AZERTY, cette
	// touche est aussi celle qui fait avancer : impossible de marcher sans decoller.
	//
	// On ne peut pas defaire une liaison inscrite dans un graphe Blueprint depuis l'exterieur.
	// En revanche, la liaison RUNTIME portee par l'InputComponent du pawn est modifiable -- ce
	// qui corrige le probleme sans toucher a un seul asset du sample, et disparait avec le
	// plugin. C'est le meme principe que la greffe du composant lui-meme.

	/**
	 * Touche a remapper DANS LES LIAISONS RUNTIME. Desactive par defaut.
	 *
	 * Ce chemin s'est revele inoperant pour le mode vol du sample : son evenement est un
	 * K2Node_InputDebugKeyEvent, qui ne s'enregistre pas dans les liaisons clavier ordinaires --
	 * le compteur RemappedBindings est reste a zero, ce qui l'a revele. Le vrai correctif passe
	 * par UBTAuthoringLibrary::RemapInputKeyInBlueprint, qui modifie le noeud du graphe.
	 *
	 * Laisse en place parce que le listing LegacyKeyBindings reste un bon diagnostic, mais
	 * neutralise : une cle invalide ne remappe rien.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assise|Compatibilite")
	FKey DebugFlyKeyFrom;

	/** Touche de remplacement. Laisser INVALIDE pour desactiver purement la bascule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assise|Compatibilite")
	FKey DebugFlyKeyTo = EKeys::W;

	/** Nombre de liaisons effectivement remappees. Zero signale que la touche n'a pas ete trouvee. */
	UPROPERTY(BlueprintReadOnly, Category = "Assise|Compatibilite")
	int32 RemappedBindings = 0;

	/** Toutes les liaisons clavier heritees du pawn, pour diagnostic. */
	UPROPERTY(BlueprintReadOnly, Category = "Assise|Compatibilite")
	TArray<FString> LegacyKeyBindings;

	/**
	 * Fabrique quelques rebords de hauteurs variees devant le joueur au demarrage de la partie.
	 *
	 * Mesure du 2026-08-29 : dans DefaultLevel, le bloc le plus bas fait 128 cm. Il n'existe
	 * RIEN entre le sol et cette hauteur -- les blocs du sample sont dimensionnes pour le
	 * parkour, pas pour s'asseoir. La feature marchait, mais il n'y avait nulle part ou la voir
	 * marcher autrement que par terre.
	 *
	 * Ces blocs sont crees A L'EXECUTION : ils n'existent que le temps de la partie et ne
	 * touchent jamais le niveau. Mettre a false quand le level design fournira ses propres
	 * rebords.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assise|Debug")
	bool bSpawnDebugSeats = true;

	/** Hauteurs des rebords de test, en centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assise|Debug")
	TArray<float> DebugSeatHeights = {20.f, 40.f, 60.f, 80.f};

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void TryBindInput();
	void RemapLegacyKeys(APawn* Pawn);
	void SpawnDebugSeats();
};

/**
 * Greffe USitAbilityComponent sur le pawn joueur des qu'il existe.
 *
 * Un WorldSubsystem plutot qu'une modification du GameMode ou du pawn : aucun asset du projet
 * n'a besoin d'etre touche, et le comportement disparait avec le plugin.
 */
UCLASS()
class BTAUTHORINGKITRUNTIME_API USitAbilitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	/** Le pawn n'existe pas forcement au BeginPlay du monde : on reessaie jusqu'a le trouver. */
	void TryAttachToPlayerPawn();

	FTimerHandle AttachTimer;
	int32 AttachAttempts = 0;
};
