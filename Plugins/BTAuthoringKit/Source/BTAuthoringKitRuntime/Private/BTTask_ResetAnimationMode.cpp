#include "BTTask_ResetAnimationMode.h"

#include "AIController.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"

UBTTask_ResetAnimationMode::UBTTask_ResetAnimationMode()
{
	NodeName = "Reset Animation Mode To Blueprint";
}

EBTNodeResult::Type UBTTask_ResetAnimationMode::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* MyController = OwnerComp.GetAIOwner();
	ACharacter* MyCharacter = MyController ? Cast<ACharacter>(MyController->GetPawn()) : nullptr;

	if (MyCharacter && MyCharacter->GetMesh())
	{
		MyCharacter->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	}

	return EBTNodeResult::Succeeded;
}
