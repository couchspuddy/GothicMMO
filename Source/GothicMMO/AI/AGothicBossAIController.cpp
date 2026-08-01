// GothicBossAIController.cpp

#include "AI/AGothicBossAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
// AGothicBossAIController.cpp — new override
void AGothicBossAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn); // runs the base AGothicEnemyAIController::OnPossess, which calls
	// RunBehaviorTree(BehaviorTreeAsset) — this is what creates Blackboard

	if (Blackboard && PhaseBlackboardKey != NAME_None)
	{
		Blackboard->SetValueAsInt(PhaseBlackboardKey, CurrentPhase);
	}

}
AGothicBossAIController::AGothicBossAIController()
{
}

void AGothicBossAIController::OnLeashReset()
{
	Super::OnLeashReset(); // health restore

	if (!bResetPhaseOnLeashReset || CurrentPhase == 1)
	{
		return;
	}

	CurrentPhase = 1;

	if (Blackboard && PhaseBlackboardKey != NAME_None)
	{
		Blackboard->SetValueAsInt(PhaseBlackboardKey, CurrentPhase);
	}

	// Broadcast like any other phase change — anything that reacted to the
	// advance (VFX, music, BT branches) has to be told it has been undone.
	OnBossPhaseChanged.Broadcast(CurrentPhase);
}

void AGothicBossAIController::OnPhaseAdvance()
{
	CurrentPhase++;

	if (Blackboard && PhaseBlackboardKey != NAME_None)
	{
		Blackboard->SetValueAsInt(PhaseBlackboardKey, CurrentPhase);
	}


	OnBossPhaseChanged.Broadcast(CurrentPhase);
}