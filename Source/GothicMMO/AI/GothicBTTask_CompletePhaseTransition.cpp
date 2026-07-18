// GothicBTTask_CompletePhaseTransition.cpp

#include "AI/GothicBTTask_CompletePhaseTransition.h"
#include "AI/AGothicBossAIController_BestialLucid.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

UGothicBTTask_CompletePhaseTransition::UGothicBTTask_CompletePhaseTransition()
{
	NodeName = TEXT("Gothic Complete Phase Transition");
}

EBTNodeResult::Type UGothicBTTask_CompletePhaseTransition::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AGothicBossAIController_BestialLucid* BossAIC =
			Cast<AGothicBossAIController_BestialLucid>(OwnerComp.GetAIOwner()))
	{
		BossAIC->CompletePhase2Transition();
		return EBTNodeResult::Succeeded;
	}

	UE_LOG(LogTemp, Error,
		TEXT("CompletePhaseTransition: AI Owner is not a AGothicBossAIController_BestialLucid — "
			 "is this node on the wrong tree?"));
	return EBTNodeResult::Failed;
}