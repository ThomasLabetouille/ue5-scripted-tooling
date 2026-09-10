#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ScriptedInputLibrary.generated.h"

class UInputAction;

/**
 * Injection d'input joueur pilotable par script (BTAuthoringKit).
 *
 * POURQUOI CETTE CLASSE EXISTE
 * ----------------------------
 * Verifie le 2026-08-27 en PIE reelle sur GameAnimationSample : aucune des deux voies
 * "naturelles" ne permet de faire marcher le pawn joueur depuis Python.
 *
 *   1. APawn::AddMovementInput -> 0 cm parcouru. Le pawn est Mover-based
 *      (CharacterMoverComponent, pas de CharacterMovementComponent) : le vecteur d'input en
 *      attente n'est jamais consomme par Mover, qui lit son input par son propre producteur.
 *
 *   2. Injection Enhanced Input -> impossible a atteindre. La classe
 *      UEnhancedInputLocalPlayerSubsystem et sa methode InjectInputVectorForAction sont bien
 *      exposees au Python, mais rien ne permet d'en obtenir l'INSTANCE : USubsystemBlueprintLibrary
 *      est absente du binding Python de ce build, et APlayerController::Player est protegee en
 *      lecture ("Property 'Player' is protected and cannot be read").
 *
 * Cette bibliotheque comble exactement ce trou : elle expose la recuperation du subsystem, que
 * seul le C++ peut faire ici, et rien d'autre.
 *
 * CE QUE CA GARANTIT
 * ------------------
 * L'input injecte entre par le MEME point qu'un vrai appui touche : il traverse l'Input Mapping
 * Context, les modifiers et les triggers, puis la logique de mouvement du personnage. C'est donc
 * un test par le VRAI chemin de jeu au sens du cahier des charges -- a la difference d'un
 * SetActorLocation (interdit, cf. CLAUDE.md piege 7) ou d'un appel direct sur le composant de
 * mouvement, qui prouveraient seulement que le teleport fonctionne.
 *
 * PORTABILITE
 * -----------
 * Rien ici n'est specifique a GameAnimationSample ni aux Behavior Trees : c'est de l'outillage de
 * test automatise reutilisable dans n'importe quel projet UE utilisant Enhanced Input.
 */
UCLASS()
class BTAUTHORINGKITRUNTIME_API UScriptedInputLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Y a-t-il un subsystem Enhanced Input joignable ? A appeler en premier dans un test : un
	 * false ici distingue "l'infrastructure de test est cassee" de "la feature ne marche pas",
	 * distinction qui a coute une session entiere le 2026-08-15.
	 */
	UFUNCTION(BlueprintPure, Category = "BTAuthoringKit|Scripted Input")
	static bool IsScriptedInputAvailable(UObject* WorldContextObject, int32 PlayerIndex = 0);

	/** Injecte une valeur pour UNE frame. A rappeler a chaque tick pour un maintien. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoringKit|Scripted Input")
	static bool InjectInputVector(UObject* WorldContextObject, UInputAction* Action,
		FVector Value, int32 PlayerIndex = 0);

	/**
	 * Maintient une valeur d'input jusqu'a StopContinuousInput -- l'equivalent d'une touche
	 * gardee enfoncee. A preferer a InjectInputVector dans un test etale dans le temps : le
	 * maintien ne depend plus de la cadence du callback de tick, donc le resultat ne varie pas
	 * avec la charge de l'editeur.
	 */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoringKit|Scripted Input")
	static bool StartContinuousInput(UObject* WorldContextObject, UInputAction* Action,
		FVector Value, int32 PlayerIndex = 0);

	/** Met a jour la valeur maintenue sans relacher. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoringKit|Scripted Input")
	static bool UpdateContinuousInput(UObject* WorldContextObject, UInputAction* Action,
		FVector Value, int32 PlayerIndex = 0);

	/** Relache l'input maintenu. Toujours appeler en fin de test, y compris sur echec. */
	UFUNCTION(BlueprintCallable, Category = "BTAuthoringKit|Scripted Input")
	static bool StopContinuousInput(UObject* WorldContextObject, UInputAction* Action,
		int32 PlayerIndex = 0);
};
