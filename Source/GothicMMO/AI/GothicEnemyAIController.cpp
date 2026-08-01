// GothicEnemyAIController.cpp

#include "AI/GothicEnemyAIController.h"

#include "TimerManager.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/GothicAttributeSet.h"
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

    // A leashing enemy is not available to be re-aggroed. The pawn-side gate in
    // AGothicEnemyBase::SetCombatTarget catches perception, encounter volumes and
    // pack propagation; this catches anything that reaches the controller
    // directly — including CombatSync's pawn-to-Blackboard recovery net, which
    // would otherwise undo the disengage within one 0.2s service tick.
    if (bLeashReturning)
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
    // Clear the PAWN's latch first, and unconditionally — ahead of the Blackboard
    // early-out below, which would otherwise skip it.
    //
    // This is the bug that made the leash cosmetic. CombatTarget on the pawn was
    // never cleared by anything, and GothicBTService_CombatSync reconciles the
    // pawn against the Blackboard at 5Hz: every leash break cleared the key, and
    // 0.2s later the service read the still-latched pawn target and called
    // SetBlackboardTarget to put it straight back. The 2-second leash check was
    // being undone ten times between its own ticks — measured as the Bestial
    // Lucid chasing a teleported player 7,187uu out of her arena while still
    // reporting in_combat: true.
    if (AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(GetPawn()))
    {
        Enemy->ClearCombatTarget();
    }

    // Release the facing lock and full speed even without a Blackboard — both are
    // controller/pawn state, not tree state.
    ClearFocus(EAIFocusPriority::Gameplay);

    if (ACharacter* SpeedChar = Cast<ACharacter>(GetPawn()))
    {
        SpeedChar->GetCharacterMovement()->MaxWalkSpeed = DefaultWalkSpeed;
    }

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

    // A return in progress owns the pawn until it finishes. Nothing below should
    // run — there is no target to track and no leash left to break.
    if (bLeashReturning)
    {
        TickLeashReturn();
        return;
    }

    if (Blackboard && Blackboard->GetValueAsBool(GothicBBKeys::bIsInCombat))
    {
        // HORIZONTAL, like every other range in this class. A 3D leash measured
        // from a spawn point on a different floor level than the fight silently
        // shortens itself by the height difference.
        const float DistFromOrigin = FVector::Dist2D(OwnerPawn->GetActorLocation(), PatrolOrigin);

        bool bBroken = DistFromOrigin > LeashRange;

        // The arena half: the target leaving the anchor's radius breaks the leash
        // even if the enemy hasn't been dragged out yet. Without this the enemy
        // has to walk the whole leash length before giving up, which is the
        // measured "boss follows you out of her own room" behaviour.
        if (!bBroken && bLeashOnTargetDistance)
        {
            if (const AActor* Target = GetTargetActor())
            {
                bBroken = FVector::Dist2D(Target->GetActorLocation(), PatrolOrigin) > LeashRange;
            }
        }

        if (bBroken)
        {
            BreakLeash();
            return;
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

void AGothicEnemyAIController::BreakLeash()
{
    if (bLeashReturning)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("GothicEnemyAIController[%s]: leash broken — disengaging and returning to anchor %s"),
        *GetNameSafe(GetPawn()), *PatrolOrigin.ToCompactString());

    // Set the suppression flag BEFORE clearing, so nothing that reacts to the
    // clear inside the same call stack can re-aggro through the gate.
    bLeashReturning     = true;
    LeashReturnStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

    ClearCombatTarget();

    // Drop whatever the tree had in flight. A boss that leashes mid-swing should
    // not carry the swing home with her.
    StopMovement();

    MoveToLocation(PatrolOrigin, 50.f);
}

void AGothicEnemyAIController::TickLeashReturn()
{
    APawn* OwnerPawn = GetPawn();
    if (!OwnerPawn)
    {
        return;
    }

    const float Now     = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    const float Elapsed = Now - LeashReturnStartTime;

    const bool bHome      = FVector::Dist2D(OwnerPawn->GetActorLocation(), PatrolOrigin) <= LeashReturnAcceptanceRadius;
    const bool bTimedOut  = Elapsed >= LeashReturnTimeoutSeconds;

    if (!bHome && !bTimedOut)
    {
        // Re-issue rather than trust the original request to survive. The tree is
        // still ticking underneath this — its own idle/patrol MoveTo can and does
        // take the movement request away mid-return.
        MoveToLocation(PatrolOrigin, 50.f);
        return;
    }

    if (bTimedOut && !bHome)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicEnemyAIController[%s]: leash return timed out after %.1fs at %.0fuu from anchor — "
                 "resetting anyway (check navmesh coverage between the arena and the anchor)"),
            *GetNameSafe(OwnerPawn), Elapsed,
            FVector::Dist2D(OwnerPawn->GetActorLocation(), PatrolOrigin));
    }

    // Home. Lift the suppression only after the reset has run, so the fresh pull
    // can never land on a half-reset boss.
    OnLeashReset();

    bLeashReturning = false;
}

void AGothicEnemyAIController::OnLeashReset()
{
    if (!bRestoreHealthOnLeashReset)
    {
        return;
    }

    UAbilitySystemComponent* ASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn());
    if (!ASC)
    {
        return;
    }

    const float MaxHealth = ASC->GetNumericAttribute(UGothicAttributeSet::GetMaxHealthAttribute());
    if (MaxHealth > 0.f)
    {
        // Base value, not current: this is a state reset, not a heal, and it must
        // not be scaled by HealingReceived or intercepted by damage-path modifiers.
        ASC->SetNumericAttributeBase(UGothicAttributeSet::GetHealthAttribute(), MaxHealth);
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