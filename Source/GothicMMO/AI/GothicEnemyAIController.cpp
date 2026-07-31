// GothicEnemyAIController.cpp

#include "AI/GothicEnemyAIController.h"

#include "TimerManager.h"
#include "AI/GothicEnemyBase.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"

AGothicEnemyAIController::AGothicEnemyAIController()
{
    bWantsPlayerState = false;
}

void AGothicEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    PatrolOrigin = InPawn->GetActorLocation();

    // Cache the default walk speed so the decel service can restore it
    if (ACharacter* Char = Cast<ACharacter>(InPawn))
    {
        DefaultWalkSpeed = Char->GetCharacterMovement()->MaxWalkSpeed;
    }

    if (BehaviorTreeAsset)
    {
        RunBehaviorTree(BehaviorTreeAsset);

        if (Blackboard)
        {
            Blackboard->SetValueAsVector(GothicBBKeys::PatrolOrigin, PatrolOrigin);
            Blackboard->SetValueAsFloat(GothicBBKeys::AttackRange, MeleeAttackRange);
            Blackboard->SetValueAsFloat(GothicBBKeys::EngageDistance, PreferredEngageDistance);
            Blackboard->SetValueAsBool(GothicBBKeys::bIsInCombat, false);
        }
    }

    // Flush any target that arrived before the tree did. Aggro is not
    // synchronised with possession — an encounter volume can call
    // SetCombatTarget on a pawn whose controller has no Blackboard yet — and
    // before this the call was simply lost. Done AFTER RunBehaviorTree so the
    // bIsInCombat=false written above cannot stomp it.
    if (AActor* Pending = PendingBlackboardTarget.Get())
    {
        PendingBlackboardTarget.Reset();
        UE_LOG(LogTemp, Verbose,
            TEXT("GothicEnemyAIController[%s]: flushing target %s cached before the Blackboard existed"),
            *GetName(), *Pending->GetName());
        SetBlackboardTarget(Pending);
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

    // Restore default speed in case decel was active when unpossessed
    if (APawn* UnpossessedPawn = GetPawn())
    {
        if (ACharacter* Char = Cast<ACharacter>(UnpossessedPawn))
        {
            Char->GetCharacterMovement()->MaxWalkSpeed = DefaultWalkSpeed;
        }
    }
}

void AGothicEnemyAIController::SetBlackboardTarget(AActor* NewTarget)
{
    if (!NewTarget)
    {
        return;
    }

    if (!Blackboard)
    {
        // Not a failure — just early. Hold it and let OnPossess push it once
        // RunBehaviorTree has created the Blackboard. GothicBTService_CombatSync
        // is the second, continuous safety net for the same race.
        PendingBlackboardTarget = NewTarget;
        return;
    }

    Blackboard->SetValueAsObject(GothicBBKeys::TargetActor,    NewTarget);
    Blackboard->SetValueAsVector(GothicBBKeys::TargetLocation, NewTarget->GetActorLocation());
    Blackboard->SetValueAsBool(GothicBBKeys::bIsInCombat,      true);
    Blackboard->SetValueAsBool(GothicBBKeys::bCanSeeTarget,    true);

    // Lock focus onto the target. This is the piece that makes an enemy face the
    // player: with the pawn on bUseControllerDesiredRotation, focus drives the
    // control rotation, so the enemy keeps facing the target while it strafes,
    // repositions, and approaches instead of turning to face its own movement.
    // Gameplay priority outranks the path-following focal point, so movement can
    // never steal the facing. Nothing set focus before — the root cause of a boss
    // that constantly showed you its back.
    SetFocus(NewTarget, EAIFocusPriority::Gameplay);

    // Roll a new stagger delay each time combat starts
    const float Delay = FMath::FRandRange(StaggerDelayRange.X, StaggerDelayRange.Y);
    Blackboard->SetValueAsFloat(GothicBBKeys::StaggerDelay, Delay);

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

    // Release the facing lock so the enemy can look where it walks again (patrol,
    // return-to-home) once it's out of combat.
    ClearFocus(EAIFocusPriority::Gameplay);

    // Restore full speed when leaving combat
    if (ACharacter* Char = Cast<ACharacter>(GetPawn()))
    {
        Char->GetCharacterMovement()->MaxWalkSpeed = DefaultWalkSpeed;
    }
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
    APawn*  OwnerPawn = GetPawn();

    if (!Target || !OwnerPawn)
    {
        return false;
    }

    // HORIZONTAL distance, deliberately. See the MeleeAttackRange comment in the
    // header: an actor's location sits at the centre of its capsule, so the 3D
    // separation between two pawns standing on the same floor is never zero — it
    // floors at the difference of their capsule half-heights. The Bestial Lucid's
    // capsule fix (88 -> 253) therefore silently subtracted 165uu of reach from
    // every range check that measured in 3D: at 150uu of genuine melee contact
    // the 3D distance read 327uu, past a 200uu MeleeAttackRange, and this
    // function returned false in 100% of sampled combat ticks.
    //
    // Creature capsule heights must never change combat reach. Floors are
    // separated by navigation and line of sight, not by this number.
    const float DistanceToTarget = FVector::Dist2D(OwnerPawn->GetActorLocation(), Target->GetActorLocation());
    return DistanceToTarget <= MeleeAttackRange;
}

void AGothicEnemyAIController::CheckLeash()
{
    APawn* OwnerPawn = GetPawn();
    if (!OwnerPawn)
    {
        return;
    }

    if (Blackboard && Blackboard->GetValueAsBool(GothicBBKeys::bIsInCombat))
    {
        const float DistFromOrigin = FVector::Dist(OwnerPawn->GetActorLocation(), PatrolOrigin);

        if (DistFromOrigin > LeashRange)
        {
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

void AGothicEnemyAIController::EnterRegroupPause(float Duration)
{
    if (UBehaviorTreeComponent* BTComp =
        Cast<UBehaviorTreeComponent>(GetBrainComponent()))
    {
        BTComp->PauseLogic(TEXT("PackRegroup"));

        FTimerHandle RegroupTimer;
        GetWorldTimerManager().SetTimer(RegroupTimer, [BTComp]()
        {
            if (BTComp)
            {
                BTComp->ResumeLogic(TEXT("PackRegroup"));
            }
        }, Duration, false);

    }
}