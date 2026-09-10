#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ResetAnimationMode.generated.h"

/**
 * Petite task utilitaire (BTAuthoringKit) : remet le SkeletalMeshComponent du pawn possede en
 * mode AnimationBlueprint.
 *
 * A utiliser apres une chaine de UBTTask_PlayAnimation (task stock du moteur) dont un maillon a
 * ete lance en mode "bouclé + non bloquant" (ex. une pose/anim d'attente tenue pendant un
 * BTTask_Wait, comme une boucle d'idle assis) : dans ce cas precis, UBTTask_PlayAnimation ne
 * restaure JAMAIS lui-meme le mode AnimationBlueprint a la fin -- son CleanUp() ne le fait que
 * si CE maillon-la avait lui-meme capture AnimationBlueprint comme mode precedent, ce qui n'est
 * plus vrai des qu'un maillon bouclé est passe par la avant lui (verifie dans le source moteur,
 * Engine/Source/Runtime/AIModule/Private/BehaviorTree/Tasks/BTTask_PlayAnimation.cpp). Sans
 * cette task explicite en fin de chaine, le personnage resterait fige en mode animation
 * "single node" indefiniment (plus de locomotion via l'AnimBP ensuite).
 */
UCLASS()
class BTAUTHORINGKITRUNTIME_API UBTTask_ResetAnimationMode : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ResetAnimationMode();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
