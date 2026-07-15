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

	UE_LOG(LogTemp, Log, TEXT("BossAI [%s]: BB valid=%d, Key=%s, KeyID=%d, Phase=%d"),
		InPawn ? *InPawn->GetName() : TEXT("NoPawn"),
		Blackboard != nullptr,
		*PhaseBlackboardKey.ToString(),
		Blackboard ? (int32)Blackboard->GetKeyID(PhaseBlackboardKey) : -1,
		CurrentPhase);
}
AGothicBossAIController::AGothicBossAIController()
{
}

void AGothicBossAIController::OnPhaseAdvance()
{
	CurrentPhase++;

	if (Blackboard && PhaseBlackboardKey != NAME_None)
	{
		Blackboard->SetValueAsInt(PhaseBlackboardKey, CurrentPhase);
	}

	UE_LOG(LogTemp, Log, TEXT("%s: Boss phase advanced to %d"),
		GetPawn() ? *GetPawn()->GetName() : TEXT("Unknown"), CurrentPhase);

	OnBossPhaseChanged.Broadcast(CurrentPhase);
}