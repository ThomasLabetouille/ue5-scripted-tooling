#include "SitAbilityComponent.h"

#include "SitSurfaceLibrary.h"
#include "Components/InputComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"

namespace
{
	constexpr float AttachRetrySeconds = 0.5f;
	constexpr int32 MaxAttachAttempts = 40;   // 20 secondes : large, mais pas infini
}

// ============================================================================
// COMPOSANT
// ============================================================================

USitAbilityComponent::USitAbilityComponent()
{
	// Le tick ne sert qu'a l'affichage de debogage. La capacite elle-meme est evenementielle.
	PrimaryComponentTick.bCanEverTick = true;
}

void USitAbilityComponent::BeginPlay()
{
	Super::BeginPlay();
	TryBindInput();

	if (bSpawnDebugSeats)
	{
		SpawnDebugSeats();
	}
}

void USitAbilityComponent::SpawnDebugSeats()
{
	AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	if (!Owner || !World || DebugSeatHeights.Num() == 0)
	{
		return;
	}

	UStaticMesh* const Cube =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SitAbility] cube moteur introuvable -- pas de rebords de test"));
		return;
	}

	// Le sol reel sous le joueur : poser les blocs a l'altitude du pawn les enterrerait ou les
	// ferait flotter selon la hauteur de sa capsule.
	const FVector Origin = Owner->GetActorLocation();
	FHitResult GroundHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DebugSeats), false);
	Params.AddIgnoredActor(Owner);
	const float GroundZ = World->LineTraceSingleByChannel(GroundHit, Origin,
		Origin - FVector(0.f, 0.f, 500.f), ECC_Visibility, Params)
		? GroundHit.ImpactPoint.Z : Origin.Z;

	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();

	const float Side = 160.f;   // largeur, tres au-dessus du minimum requis
	for (int32 i = 0; i < DebugSeatHeights.Num(); ++i)
	{
		const float H = DebugSeatHeights[i];
		if (H <= 0.f)
		{
			continue;
		}

		// En arc devant le joueur, espaces pour qu'on puisse passer de l'un a l'autre.
		const FVector Centre = Origin
			+ Forward * 320.f
			+ Right * ((i - (DebugSeatHeights.Num() - 1) * 0.5f) * 260.f)
			+ FVector(0.f, 0.f, GroundZ - Origin.Z + H * 0.5f);

		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* const Actor =
			World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Centre,
				FRotator::ZeroRotator, Spawn);
		if (!Actor)
		{
			continue;
		}

		Actor->SetMobility(EComponentMobility::Movable);
		Actor->SetActorScale3D(FVector(Side / 100.f, Side / 100.f, H / 100.f));
		if (UStaticMeshComponent* const Comp = Actor->GetStaticMeshComponent())
		{
			Comp->SetStaticMesh(Cube);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[SitAbility] %d rebord(s) de test cree(s) pour la partie"),
		DebugSeatHeights.Num());
}

void USitAbilityComponent::TryBindInput()
{
	APawn* const Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return;
	}

	UEnhancedInputComponent* const EIC = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
	APlayerController* const PC = Cast<APlayerController>(Pawn->GetController());
	if (!EIC || !PC)
	{
		// Le composant d'input n'existe qu'apres possession, et il doit etre un
		// UEnhancedInputComponent. On laisse bInputBound a false plutot que de faire semblant :
		// un test rouge doit pouvoir distinguer "la feature ne marche pas" de "le branchement
		// d'input n'a jamais eu lieu".
		return;
	}

	ULocalPlayer* const LocalPlayer = PC->GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* const Subsystem =
		LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	SitAction = NewObject<UInputAction>(this, TEXT("IA_Sit_Runtime"));
	SitAction->ValueType = EInputActionValueType::Boolean;

	SitContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Sit_Runtime"));
	SitContext->MapKey(SitAction, SitKey);

	// Priorite superieure, mais AJOUT et non remplacement : le contexte du sample continue de
	// fonctionner, seule la touche d'assise s'y ajoute.
	Subsystem->AddMappingContext(SitContext, ContextPriority);

	// Started : declenche au moment de l'appui, une seule fois. Triggered se repeterait a chaque
	// frame ou la touche est maintenue, et le personnage alternerait assis/debout sans fin.
	EIC->BindAction(SitAction, ETriggerEvent::Started, this, &USitAbilityComponent::ToggleSit);
	bInputBound = true;

	RemapLegacyKeys(Pawn);
}

void USitAbilityComponent::RemapLegacyKeys(APawn* Pawn)
{
	if (!Pawn || !Pawn->InputComponent)
	{
		return;
	}

	LegacyKeyBindings.Reset();
	RemappedBindings = 0;

	// Le noeud du Blueprint est un K2Node_InputDebugKeyEvent. Premiere tentative : parcourir
	// InputComponent->DebugKeyBindings -- ce membre N'EXISTE PAS sur UInputComponent en UE 5.8,
	// la compilation l'a refuse. On scanne donc les liaisons ordinaires des DEUX composants
	// d'input en presence, celui du pawn et celui du PlayerController : une liaison de debogage
	// peut atterrir sur l'un ou l'autre selon la facon dont le Blueprint l'enregistre.
	auto Scanner = [this](UInputComponent* Comp, const TCHAR* Origine)
	{
		if (!Comp)
		{
			return;
		}
		for (FInputKeyBinding& Binding : Comp->KeyBindings)
		{
			LegacyKeyBindings.Add(FString::Printf(TEXT("%s: %s (evenement %d)"),
				Origine, *Binding.Chord.Key.ToString(), (int32)Binding.KeyEvent.GetValue()));

			if (DebugFlyKeyFrom.IsValid() && Binding.Chord.Key == DebugFlyKeyFrom)
			{
				// Touche de remplacement invalide = bascule neutralisee, sans rien retirer de
				// l'asset du sample.
				Binding.Chord.Key = DebugFlyKeyTo;
				++RemappedBindings;
			}
		}
	};

	Scanner(Pawn->InputComponent, TEXT("pawn"));
	if (APlayerController* const PC = Cast<APlayerController>(Pawn->GetController()))
	{
		Scanner(PC->InputComponent, TEXT("controller"));
	}

	UE_LOG(LogTemp, Log,
		TEXT("[SitAbility] %d liaison(s) clavier heritee(s) remappee(s) sur %d au total"),
		RemappedBindings, LegacyKeyBindings.Num());
}

void USitAbilityComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bShowDebug || !GEngine)
	{
		return;
	}

	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Ce que la sonde voit MAINTENANT, en continu : c'est ce qui permet de comprendre pourquoi
	// un endroit est refuse, en se promenant, sans lire un fichier de resultats.
	const FSitSurfaceResult Vue = USitSurfaceLibrary::FindSitSurfaceForCharacter(Owner);

	const FColor Couleur = Vue.bValid ? FColor::Green : FColor::Red;
	if (Vue.SeatLocation != FVector::ZeroVector)
	{
		DrawDebugSphere(GetWorld(), Vue.SeatLocation, 18.f, 12, Couleur, false, -1.f, 0, 1.5f);
	}

	FString Ligne;
	if (bIsSeated)
	{
		Ligne = FString::Printf(TEXT("ASSIS  -  siege a %.0f cm  -  jambes %s"),
			SeatHeight, bLegsDangle ? TEXT("pendantes") : TEXT("au sol"));
	}
	else if (Vue.bValid)
	{
		Ligne = FString::Printf(TEXT("[T] pour s'asseoir  -  siege a %.0f cm  -  jambes %s"),
			Vue.SeatHeight, Vue.bLegsDangle ? TEXT("pendantes") : TEXT("au sol"));
	}
	else
	{
		Ligne = FString::Printf(TEXT("assise impossible ici  -  %s"), *Vue.RejectReason);
	}

	GEngine->AddOnScreenDebugMessage(/*Key*/ 7701, 0.f,
		bIsSeated ? FColor::Cyan : Couleur, Ligne);
}

bool USitAbilityComponent::TrySit()
{
	++AttemptCount;

	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		LastRejectReason = TEXT("PasDeProprietaire");
		return false;
	}

	const FSitSurfaceResult Result = USitSurfaceLibrary::FindSitSurfaceForCharacter(Owner);

	if (!Result.bValid)
	{
		// On conserve le motif : c'est lui qui permet de distinguer un refus legitime d'un
		// systeme qui refuse tout. Un booleen seul ne le dirait pas.
		LastRejectReason = Result.RejectReason;
		return false;
	}

	// Note : bWithinAnimationRange est deliberement IGNORE ici. La couverture animee ne doit
	// jamais empecher une assise -- decision du 2026-08-29. Un refus est un bug, une animation
	// approximative n'en est pas un.
	bIsSeated = true;
	SeatLocation = Result.SeatLocation;
	SeatRotation = Result.SeatRotation;
	SeatHeight = Result.SeatHeight;
	bLegsDangle = Result.bLegsDangle;
	LastRejectReason.Empty();
	return true;
}

void USitAbilityComponent::StandUp()
{
	if (!bIsSeated)
	{
		return;
	}
	bIsSeated = false;
	SeatHeight = 0.f;
	bLegsDangle = false;
}

void USitAbilityComponent::ToggleSit()
{
	if (bIsSeated)
	{
		StandUp();
	}
	else
	{
		TrySit();
	}
}

// ============================================================================
// SUBSYSTEM : greffe le composant sans toucher au pawn du sample
// ============================================================================

void USitAbilitySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Uniquement en jeu : le monde de l'editeur n'a pas de joueur a equiper, et lui greffer des
	// composants salirait le niveau -- exactement l'effet de bord qu'on cherche a eviter.
	if (!InWorld.IsGameWorld())
	{
		return;
	}

	AttachAttempts = 0;
	InWorld.GetTimerManager().SetTimer(
		AttachTimer, [this]() { TryAttachToPlayerPawn(); },
		AttachRetrySeconds, /*bLoop*/ true, /*FirstDelay*/ 0.f);
}

void USitAbilitySubsystem::TryAttachToPlayerPawn()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	++AttachAttempts;

	APawn* const Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (Pawn && Pawn->InputComponent)
	{
		if (!Pawn->FindComponentByClass<USitAbilityComponent>())
		{
			USitAbilityComponent* const Comp =
				NewObject<USitAbilityComponent>(Pawn, USitAbilityComponent::StaticClass(),
					TEXT("SitAbility"));
			Comp->RegisterComponent();
		}
		World->GetTimerManager().ClearTimer(AttachTimer);
		return;
	}

	if (AttachAttempts >= MaxAttachAttempts)
	{
		// Abandonner bruyamment plutot que de boucler en silence : un composant qui ne s'attache
		// jamais donnerait un test rouge sans explication.
		UE_LOG(LogTemp, Warning,
			TEXT("[SitAbility] pawn joueur introuvable apres %d tentatives -- capacite non greffee"),
			AttachAttempts);
		World->GetTimerManager().ClearTimer(AttachTimer);
	}
}
