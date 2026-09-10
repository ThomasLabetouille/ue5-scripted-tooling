#include "ScriptedInputLibrary.h"

#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/**
	 * Le seul service que cette bibliotheque rend vraiment : atteindre l'instance du subsystem.
	 * ULocalPlayer::GetSubsystem est un template C++, donc invisible depuis la reflexion Python.
	 */
	UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem(UObject* WorldContextObject, int32 PlayerIndex)
	{
		if (!GEngine || !WorldContextObject)
		{
			return nullptr;
		}

		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		if (!World)
		{
			return nullptr;
		}

		APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, PlayerIndex);
		if (!PlayerController)
		{
			return nullptr;
		}

		ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
		if (!LocalPlayer)
		{
			return nullptr;
		}

		return LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	}
}

bool UScriptedInputLibrary::IsScriptedInputAvailable(UObject* WorldContextObject, int32 PlayerIndex)
{
	return GetInputSubsystem(WorldContextObject, PlayerIndex) != nullptr;
}

bool UScriptedInputLibrary::InjectInputVector(UObject* WorldContextObject, UInputAction* Action,
	FVector Value, int32 PlayerIndex)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem(WorldContextObject, PlayerIndex);
	if (!Subsystem || !Action)
	{
		return false;
	}

	Subsystem->InjectInputVectorForAction(Action, Value, TArray<UInputModifier*>(), TArray<UInputTrigger*>());
	return true;
}

bool UScriptedInputLibrary::StartContinuousInput(UObject* WorldContextObject, UInputAction* Action,
	FVector Value, int32 PlayerIndex)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem(WorldContextObject, PlayerIndex);
	if (!Subsystem || !Action)
	{
		return false;
	}

	Subsystem->StartContinuousInputInjectionForAction(Action, FInputActionValue(Value),
		TArray<UInputModifier*>(), TArray<UInputTrigger*>());
	return true;
}

bool UScriptedInputLibrary::UpdateContinuousInput(UObject* WorldContextObject, UInputAction* Action,
	FVector Value, int32 PlayerIndex)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem(WorldContextObject, PlayerIndex);
	if (!Subsystem || !Action)
	{
		return false;
	}

	Subsystem->UpdateValueOfContinuousInputInjectionForAction(Action, FInputActionValue(Value));
	return true;
}

bool UScriptedInputLibrary::StopContinuousInput(UObject* WorldContextObject, UInputAction* Action,
	int32 PlayerIndex)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem(WorldContextObject, PlayerIndex);
	if (!Subsystem || !Action)
	{
		return false;
	}

	Subsystem->StopContinuousInputInjectionForAction(Action);
	return true;
}
