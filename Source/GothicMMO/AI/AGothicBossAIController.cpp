// GothicBossAIController.cpp

#include "AI/AGothicBossAIController.h"
#include "BehaviorTree/BlackboardComponent.h"

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