// GothicEnemyAIController.cpp

#include "AI/GothicEnemyAIController.h"
#include "AI/GothicEnemyBase.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

AGothicEnemyAIController::AGothicEnemyAIController()
{
    bWantsPlayerState = false;
}

void AGothicEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    PatrolOrigin = InPawn->GetActorLocation();

    if (BehaviorTreeAsset)
    {
        RunBehaviorTree(BehaviorTreeAsset);

        if (Blackboard)
        {
            Blackboard->SetValueAsVector(GothicBBKeys::PatrolOrigin, PatrolOrigin);
            Blackboard->SetValueAsFloat(GothicBBKeys::AttackRange, MeleeAttackRange);
            Blackboard->SetValueAsBool(GothicBBKeys::bIsInCombat, false);
        }
    }

    GetWorldTimerManager().SetTimer(
        LeashCheckTimer,
        this,
        &AGothicEnemyAIController::CheckLeash,
        2.0f,
        true);
}

void AGothicEnemyAIController::OnUnPossess()
{
    Super::OnUnPossess();
    GetWorldTimerManager().ClearTimer(LeashCheckTimer);
}

void AGothicEnemyAIController::SetBlackboardTarget(AActor* NewTarget)
{
    if (!Blackboard || !NewTarget)
    {
        return;
    }

    Blackboard->SetValueAsObject(GothicBBKeys::TargetActor,    NewTarget);
    Blackboard->SetValueAsVector(GothicBBKeys::TargetLocation, NewTarget->GetActorLocation());
    Blackboard->SetValueAsBool(GothicBBKeys::bIsInCombat,      true);
    Blackboard->SetValueAsBool(GothicBBKeys::bCanSeeTarget,    true);
}

void AGothicEnemyAIController::ClearCombatTarget()
{
    if (!Blackboard)
    {
        return;
    }

    Blackboard->ClearValue(GothicBBKeys::TargetActor);
    Blackboard->SetValueAsBool(GothicBBKeys::bIsInCombat,   false);
    Blackboard->SetValueAsBool(GothicBBKeys::bCanSeeTarget, false);
}

AActor* AGothicEnemyAIController::GetTargetActor() const
{
    if (Blackboard)
    {
        return Cast<AActor>(Blackboard->GetValueAsObject(GothicBBKeys::TargetActor));
    }
    return nullptr;
}

bool AGothicEnemyAIController::IsTargetInAttackRange() const
{
    AActor* Target  = GetTargetActor();
    APawn*  OwnerPawn = GetPawn();  // renamed from Owner to avoid hiding AActor::Owner

    if (!Target || !OwnerPawn)
    {
        return false;
    }

    const float DistanceToTarget = FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation());
    return DistanceToTarget <= MeleeAttackRange;
}

void AGothicEnemyAIController::CheckLeash()
{
    APawn* OwnerPawn = GetPawn();  // renamed from Owner to avoid hiding AActor::Owner
    if (!OwnerPawn)
    {
        return;
    }

    if (Blackboard && Blackboard->GetValueAsBool(GothicBBKeys::bIsInCombat))
    {
        const float DistFromOrigin = FVector::Dist(OwnerPawn->GetActorLocation(), PatrolOrigin);

        if (DistFromOrigin > LeashRange)
        {
            UE_LOG(LogTemp, Log, TEXT("%s: Leash broken - returning to patrol origin."), *OwnerPawn->GetName());
            ClearCombatTarget();
            MoveToLocation(PatrolOrigin, 50.f);
        }
    }

    if (AActor* Target = GetTargetActor())
    {
        if (Blackboard)
        {
            Blackboard->SetValueAsVector(GothicBBKeys::TargetLocation, Target->GetActorLocation());
        }
    }
}
