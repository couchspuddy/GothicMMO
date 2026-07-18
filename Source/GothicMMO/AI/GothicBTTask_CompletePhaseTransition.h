// GothicBTTask_CompletePhaseTransition.h
// Thin closing step for the Phase 2 transition Sequence. Its only job is
// calling AGothicBossAIController_BestialLucid::CompletePhase2Transition() --
// the public entry point that clears bTransitionPending and runs the real
// phase advance (Blackboard write, broadcast, vital freeze).
//
// Deliberately not folded into GothicBTTask_FindNearestPillar or the Wall
// Pound activation step -- this needs to run strictly LAST, after Cry, the
// move, and Wall Pound have all genuinely finished, and giving it its own
// node makes that ordering explicit in the tree rather than implicit in
// some other task's side effects.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GothicBTTask_CompletePhaseTransition.generated.h"

UCLASS(meta = (DisplayName = "Gothic Complete Phase Transition"))
class GOTHICMMO_API UGothicBTTask_CompletePhaseTransition : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UGothicBTTask_CompletePhaseTransition();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};