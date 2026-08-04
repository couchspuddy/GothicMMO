// GothicEnemyAIController.cpp

#include "AI/GothicEnemyAIController.h"

#include "TimerManager.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AI/GothicEnemyBase.h"
#include "Character/GothicCharacterBase.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BrainComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"

namespace
{
    /**
     * Query extent used to snap PatrolOrigin onto the navmesh. See
     * EnsurePatrolOriginProjected for why an EXPLICIT extent is mandatory here.
     *
     * Vertical 1000: an actor's location is its CAPSULE CENTRE, so the anchor
     * captured at possess time floats above the floor the pawn is standing on by
     * one scaled capsule half-height. The worst case in the project is the
     * Bestial Lucid — capsule half-height 253 at actor scale 1.5 = 379.5uu — and
     * Recast's default vertical query extent is only 250 (see
     * DEFAULT_NAV_QUERY_EXTENT_VERTICAL in NavigationTypes.h). 379.5 > 250, so
     * the boss's anchor could never resolve to a nav poly at all. 1000 is ~2.6x
     * the measured worst case, leaving headroom for a larger creature or an
     * anchor authored a little above the floor, while still being far too short
     * to punch through to a different storey.
     *
     * Horizontal 150: deliberately capped at LeashReturnAcceptanceRadius. If the
     * anchor sits just off a navmesh edge the projection slides it sideways, and
     * bounding that slide by the same tolerance that defines "home" means the
     * effective anchor can never drift further from its authored placement than
     * the return already treats as arrived. (Recast's default is 50.)
     */
    const FVector PatrolOriginProjectionExtent(150.f, 150.f, 1000.f);

    /** Named so nothing else's RestartLogic can resume a leap by accident. */
    const TCHAR* GLeapPauseReason = TEXT("GothicLeapFlight");
}

AGothicEnemyAIController::AGothicEnemyAIController()
{
    bWantsPlayerState = false;
}

void AGothicEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    PatrolOrigin = InPawn->GetActorLocation();

    // Snap the anchor onto the navmesh immediately. Everything downstream — the
    // leash distance maths, the Blackboard key written below, and above all the
    // MoveToLocation that walks the enemy home — treats PatrolOrigin as a
    // pathable goal, and a capsule-centre is not one. Retried in BreakLeash if
    // the navmesh isn't ready this early — which is the NORMAL case, so this
    // attempt is quiet. See EnsurePatrolOriginProjected's header comment.
    EnsurePatrolOriginProjected(/*bWarnOnFailure=*/false);

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
    // FIRST, while the pawn is still ours: hand back its AirControl and drop the
    // landing binding, or a re-possess inherits a zeroed value and a stale
    // delegate. Everything below this line runs after Super, which is where the
    // pawn goes away.
    EndLeapFlight(/*bLanded=*/false, TEXT("unpossessed"));

    Super::OnUnPossess();
    GetWorldTimerManager().ClearTimer(LeashCheckTimer);

    // A regroup pause has no meaning without a pawn, and leaving it armed would
    // resume a tree this controller no longer drives.
    GetWorldTimerManager().ClearTimer(RegroupPauseTimer);

    // Restore default speed in case decel was active when unpossessed
    if (APawn* UnpossessedPawn = GetPawn())
    {
        if (ACharacter* Char = Cast<ACharacter>(UnpossessedPawn))
        {
            Char->GetCharacterMovement()->MaxWalkSpeed = DefaultWalkSpeed;
        }
    }
}

// ============================================================================
// Leap flight guard
// ============================================================================

void AGothicEnemyAIController::BeginLeapFlight()
{
    ACharacter* MeChar = Cast<ACharacter>(GetPawn());
    UCharacterMovementComponent* Move = MeChar ? MeChar->GetCharacterMovement() : nullptr;
    if (!MeChar || !Move)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicLeap[%s]: leap flight not guarded — no character/movement component. "
                 "The behaviour tree can steer this pawn out of the arc."),
            *GetNameSafe(GetPawn()));
        return;
    }

    if (bLeapInFlight)
    {
        // Second launch before the first landed. Don't re-save AirControl or
        // SavedAirControl would be overwritten with the zero we just wrote.
        GetWorldTimerManager().SetTimer(
            LeapFlightSafetyHandle, this, &AGothicEnemyAIController::HandleLeapFlightTimeout,
            LeapFlightSafetySeconds, false);
        return;
    }

    bLeapInFlight = true;

    // 1. No new move requests for the duration of the arc.
    if (UBrainComponent* Brain = GetBrainComponent())
    {
        Brain->PauseLogic(GLeapPauseReason);
    }

    // 2. And no steering even if one slips through — a Move To issued in
    //    MOVE_Falling is spent as air-control acceleration, which is exactly
    //    what was cancelling the horizontal velocity.
    SavedAirControl = Move->AirControl;
    Move->AirControl = 0.f;

    MeChar->LandedDelegate.AddDynamic(this, &AGothicEnemyAIController::HandleLeapLanded);

    GetWorldTimerManager().SetTimer(
        LeapFlightSafetyHandle, this, &AGothicEnemyAIController::HandleLeapFlightTimeout,
        LeapFlightSafetySeconds, false);

    UE_LOG(LogTemp, Verbose,
        TEXT("GothicLeap[%s]: leap flight begun — brain paused, AirControl %.3f -> 0 (safety %.1fs)"),
        *GetNameSafe(MeChar), SavedAirControl, LeapFlightSafetySeconds);
}

void AGothicEnemyAIController::AbortLeapFlight()
{
    EndLeapFlight(/*bLanded=*/false, TEXT("aborted by owner"));
}

void AGothicEnemyAIController::HandleLeapLanded(const FHitResult& Hit)
{
    EndLeapFlight(/*bLanded=*/true, TEXT("landed"));
}

void AGothicEnemyAIController::HandleLeapFlightTimeout()
{
    EndLeapFlight(/*bLanded=*/false, TEXT("safety timer — landing never arrived"));
}

void AGothicEnemyAIController::EndLeapFlight(bool bLanded, const TCHAR* Reason)
{
    if (!bLeapInFlight)
    {
        return;
    }

    // Cleared BEFORE the broadcast, deliberately. A listener is entitled to end
    // its ability from OnLeapLanded, and an EndAbility that calls
    // AbortLeapFlight re-enters here — the flag is what makes that a no-op
    // rather than a second unwind and a second broadcast.
    bLeapInFlight = false;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(LeapFlightSafetyHandle);
    }

    ACharacter* MeChar = Cast<ACharacter>(GetPawn());
    if (MeChar)
    {
        MeChar->LandedDelegate.RemoveDynamic(this, &AGothicEnemyAIController::HandleLeapLanded);

        if (UCharacterMovementComponent* Move = MeChar->GetCharacterMovement())
        {
            Move->AirControl = SavedAirControl;
        }
    }

    if (UBrainComponent* Brain = GetBrainComponent())
    {
        Brain->ResumeLogic(GLeapPauseReason);
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("GothicLeap[%s]: leap flight ended (%s) — brain resumed, AirControl restored to %.3f"),
        *GetNameSafe(MeChar), Reason, SavedAirControl);

    // Last, with every guarantee already restored, so a listener that reacts
    // synchronously sees a pawn that is fully unwound.
    OnLeapLanded.Broadcast(bLanded);
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

AActor* AGothicEnemyAIController::GetLeashQuarry() const
{
    // Same two doors DropTargetIfNoLongerFightable reads, and in the same order:
    // the Blackboard key is what the tree fights, the pawn latch is what every
    // aggro source funnels through. Either one holding a fightable actor means
    // there is still a quarry. Null, destroyed and dead all resolve to nullptr,
    // which the caller reads as "no quarry" and lets the break through.
    if (AActor* BBTarget = GetTargetActor())
    {
        if (IsFightableTarget(BBTarget))
        {
            return BBTarget;
        }
    }

    if (const AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(GetPawn()))
    {
        AActor* PawnTarget = Enemy->GetCombatTarget();
        if (IsFightableTarget(PawnTarget))
        {
            return PawnTarget;
        }
    }

    return nullptr;
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

    // Release a target that has stopped being one, before any leash maths runs
    // against it.
    DropTargetIfNoLongerFightable();

    if (Blackboard && Blackboard->GetValueAsBool(GothicBBKeys::bIsInCombat))
    {
        // HORIZONTAL, like every other range in this class. A 3D leash measured
        // from a spawn point on a different floor level than the fight silently
        // shortens itself by the height difference.
        const float DistFromOrigin = FVector::Dist2D(OwnerPawn->GetActorLocation(), PatrolOrigin);

        // The self-distance break is conditional on the QUARRY being out too.
        //
        // She never abandons a quarry standing on her ground, no matter where her
        // own movement swings her; she abandons a quarry that has dragged her off
        // it. The Bestial Lucid forced this. Her anchor sits on the EAST edge of
        // the arena and the encounter volume acquires the player at the arena
        // threshold, so her fights now BEGIN at 4,500+uu of anchor separation —
        // the mouth is 4,500-5,500 from her spawn. Ordinary in-arena combat
        // geometry then dips her own position across the 5,000 line without the
        // player going anywhere: pursuit toward the mouth, prowl orbit points
        // (which orbit the PLAYER, so they reach anchor-distance + orbit-radius),
        // give-ground retreats. Every one of those fired the break mid-fight —
        // FIGHT-ended|reason=disengaged, sprint home, heal to full, arena clock
        // discarded, with a live player still standing in the room.
        //
        //   boss 5,200 out / player 3,000 out  -> no break, the fight is on her ground
        //   boss 5,100 out / player 5,300 out  -> break; she was dragged out, walk home
        //   boss 5,200 out / no live quarry    -> break, unchanged
        //   boss inside 5,000                  -> never breaks, unchanged
        //
        // 2D, matching DistFromOrigin above and every other range in this class.
        bool bBroken = false;
        if (DistFromOrigin > LeashRange)
        {
            const AActor* Quarry = GetLeashQuarry();
            bBroken = !Quarry || FVector::Dist2D(Quarry->GetActorLocation(), PatrolOrigin) > LeashRange;
        }

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

        return;
    }

    // Targetless: re-scan what is already perceived. This is the general cure for
    // edge-only acquisition, and it is deliberately coupled to NOTHING — not to the
    // downed system, not to revives.
    //
    // The case that forced it: a perceived-but-unfightable actor BECOMING fightable
    // fires no event anywhere. Clearing the downed flag on a player standing in an
    // enemy's line of sight changes only a bool on their character; perception's
    // view of the world did not change, so OnPerceptionUpdated has nothing to
    // report and the enemy stares at a valid target forever. Broadcasting from
    // SetDowned(false) would fix that one transition while coupling PlayerState to
    // the AI stack and still missing every other unfightable->fightable case; a
    // periodic re-scan fixes all of them, including the ones nobody has hit yet.
    //
    // Last in CheckLeash on purpose: everything above can break the leash or drop a
    // target, and re-acquiring before those have run would hand back a target the
    // same tick is about to release. Piggybacked on the existing 2s leash timer —
    // no new tick — so the worst-case latency from "becomes fightable" to
    // "re-engaged" is one leash interval, and the cost on an idle enemy with an
    // empty perception set is one cast and one array check.
    //
    // Every enemy inherits this, boss included. The Feral's gated tree and the
    // boss's own acquisition are untouched by it — they simply stop being able to
    // get stuck idle in front of something they can see, which is a feature.
    ReacquireFromPerception(TEXT("idle-rescan"));
}

bool AGothicEnemyAIController::IsFightableTarget(const AActor* Actor)
{
    // One predicate, on the base class every character inherits — see
    // AGothicCharacterBase::IsFightableActor for what it tests and why. This used
    // to spell the test out here, which meant AGothicEnemyBase::NotifyDamagedBy
    // spelled out its own copy and the two could drift.
    return AGothicCharacterBase::IsFightableActor(Actor);
}

void AGothicEnemyAIController::DropTargetIfNoLongerFightable()
{
    // BreakLeash was the ONLY caller of ClearCombatTarget in the whole project.
    // Nothing released the target when it died, when it was destroyed, or when a
    // dead player respawned into a new pawn — so an enemy held a pointer to a
    // corpse until the leash happened to break, and if the fight stayed inside
    // LeashRange it held it forever.
    //
    // The visible consequence is AGothicEncounterVolume::IsAnyMemberEngaged, which
    // derives "this fight is already running" from GetCombatTarget(): a roster that
    // engaged once read engaged forever and HandleTriggerBeginOverlap could never
    // re-aggro it. That is precisely the bAggroTriggered latch the rewrite deleted,
    // reintroduced through a different door.
    //
    // Evaluated on the 2s leash tick rather than off a death delegate because one
    // test then covers all three ways a target stops being valid, on every enemy,
    // with no per-target binding to keep in step.
    AActor* BBTarget = GetTargetActor();

    AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(GetPawn());
    AActor* PawnTarget = Enemy ? Enemy->GetCombatTarget() : nullptr;

    if (!BBTarget && !PawnTarget)
    {
        return; // the disengaged steady state — nothing latched, nothing to do
    }

    // Either half still holding a live target keeps the engagement.
    // GothicBTService_CombatSync reconciles the two at 5Hz, so clearing while one
    // of them is valid would simply be undone on the next service tick.
    if (IsFightableTarget(BBTarget) || IsFightableTarget(PawnTarget))
    {
        return;
    }

    // Why it stopped being fightable, so a downed release reads differently from a
    // death or a destroyed actor in the timeline. LogVigilCombat rather than
    // LogTemp because LogTemp is clamped and this line has to survive a real run.
    // Three reasons, not two. "downed" and "dead" both used to print as the same
    // word once the downed state existed, and they are the two outcomes a reader
    // most needs to tell apart: a dead target is a kill, a downed one is the design
    // moment where the enemy abandons a body and goes hunting whoever is left.
    // "gone" is the residue — a destroyed actor, or a respawn that swapped the pawn.
    //
    // Read off whichever half was actually latched, Blackboard first, matching the
    // order every other reader of these two uses.
    const AActor* Dropped = BBTarget ? BBTarget : PawnTarget;
    const AGothicCharacterBase* DroppedChar = Cast<AGothicCharacterBase>(Dropped);

    const TCHAR* Reason = TEXT("gone");
    if (DroppedChar && IsValid(DroppedChar))
    {
        if (DroppedChar->IsDowned())
        {
            Reason = TEXT("downed");
        }
        else if (!DroppedChar->IsAlive())
        {
            Reason = TEXT("dead");
        }
    }

    const UWorld* World = GetWorld();
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|TargetDrop|target=%s|reason=%s|blackboard=%s|pawn=%s"),
        World ? World->GetTimeSeconds() : 0.f, *GetNameSafe(GetPawn()),
        *GetNameSafe(Dropped), Reason,
        *GetNameSafe(BBTarget), *GetNameSafe(PawnTarget));

    ClearCombatTarget();

    // The design beat this drop exists to serve: ignore the one who went down and
    // start eliminating whoever is still standing. Immediately, off what perception
    // ALREADY holds — waiting for the next perception edge means waiting for the
    // surviving player to walk out of and back into the enemy's senses, which in
    // the measured session simply never happened and the enemy went idle instead.
    ReacquireFromPerception(TEXT("drop-rescan"));
}

AActor* AGothicEnemyAIController::FindBestPerceivedTarget() const
{
    const APawn* OwnerPawn = GetPawn();
    const AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(OwnerPawn);
    if (!Enemy)
    {
        return nullptr;
    }

    const UAIPerceptionComponent* Perception = Enemy->GetPerceptionComponent();
    if (!Perception)
    {
        return nullptr;
    }

    // Every sense, not sight alone. Gunfire noise is a real acquisition source in
    // this project (see the hearing config on the perception component), and an
    // enemy that can hear you shooting but not see you should still come looking.
    TArray<AActor*> Perceived;
    Perception->GetCurrentlyPerceivedActors(TSubclassOf<UAISense>(), Perceived);

    const FVector PawnLocation = OwnerPawn->GetActorLocation();

    AActor* Best         = nullptr;
    float   BestDistance = TNumericLimits<float>::Max();

    for (AActor* Candidate : Perceived)
    {
        // The shared predicate, never a local re-spelling of it — a downed player
        // is not fightable, which is exactly what keeps this from handing the body
        // straight back to the enemy that just dropped it.
        if (!IsFightableTarget(Candidate))
        {
            continue;
        }

        const float Distance = FVector::Dist2D(PawnLocation, Candidate->GetActorLocation());
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best         = Candidate;
        }
    }

    return Best;
}

bool AGothicEnemyAIController::ReacquireFromPerception(const TCHAR* Via)
{
    // Cheap outs first — this runs on the 2s leash tick for every possessed enemy
    // in the level, so it must cost nothing in the common case.
    if (bLeashReturning)
    {
        return false; // walking home; IsAcceptingCombatTargets would refuse it anyway
    }

    AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(GetPawn());
    if (!Enemy || !Enemy->IsAlive())
    {
        return false;
    }

    // Only ever fills a hole. Either half still holding a fightable target means
    // there is an engagement running and nothing to re-acquire — this must never
    // retarget a fight in progress.
    if (GetLeashQuarry())
    {
        return false;
    }

    AActor* Best = FindBestPerceivedTarget();
    if (!Best)
    {
        return false;
    }

    Enemy->SetCombatTarget(Best);

    // Edge-triggered by construction: the guard above means this line can only be
    // reached from a targetless state, so a target held across many rescans prints
    // once, when it is latched. No latch member needed.
    const UWorld* World = GetWorld();
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|TargetAcquire|target=%s|via=%s|distance=%.1f"),
        World ? World->GetTimeSeconds() : 0.f, *GetNameSafe(Enemy),
        *GetNameSafe(Best), Via,
        FVector::Dist2D(Enemy->GetActorLocation(), Best->GetActorLocation()));

    return true;
}

FPathFollowingRequestResult AGothicEnemyAIController::MoveTo(
    const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath)
{
    // The walk home owns the pawn's movement until it finishes. See the header.
    if (bLeashReturning && !bIssuingLeashReturnMove)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("GothicEnemyAIController[%s]: refused a move request to %s — leash return in progress"),
            *GetNameSafe(GetPawn()), *MoveRequest.GetGoalLocation().ToCompactString());

        FPathFollowingRequestResult Refused;
        Refused.Code = EPathFollowingRequestResult::Failed;
        return Refused;
    }

    return Super::MoveTo(MoveRequest, OutPath);
}

bool AGothicEnemyAIController::EnsurePatrolOriginProjected(bool bWarnOnFailure)
{
    if (bPatrolOriginProjected)
    {
        return true;
    }

    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!NavSys)
    {
        return false;
    }

    // The extent is passed EXPLICITLY and is the entire point of this function.
    //
    // The obvious-looking alternative — MoveToLocation's
    // bProjectDestinationToNavigation flag — does not work here: that path
    // (AAIController::MoveTo) projects with INVALID_NAVEXTENT, which is
    // FVector::ZeroVector and makes the nav data substitute its own
    // DefaultQueryExtent of (50, 50, 250). A 379.5uu capsule-centre offset is
    // outside that box, so the flag would fail exactly where the raw goal
    // already fails — silently, and one call deeper.
    FNavLocation Projected;
    const FNavAgentProperties& AgentProps = GetNavAgentPropertiesRef();

    if (!NavSys->ProjectPointToNavigation(PatrolOrigin, Projected, PatrolOriginProjectionExtent, &AgentProps))
    {
        // Only the BreakLeash retry is a real problem. The possess-time attempt
        // failing means the navmesh wasn't built yet, and the retry fixes it —
        // the old text claimed "the walk home will fail until this resolves",
        // which was false for that caller and fired 19 times per PIE start.
        // (Two calls rather than a ternary verbosity: UE_LOG pastes its verbosity
        // argument into an ELogVerbosity:: token, so it cannot be an expression.)
        const FString Detail = FString::Printf(
            TEXT("GothicEnemyAIController[%s]: could not project leash anchor %s onto the navmesh (extent %s) — keeping the raw location."),
            *GetNameSafe(GetPawn()), *PatrolOrigin.ToCompactString(),
            *PatrolOriginProjectionExtent.ToCompactString());

        if (bWarnOnFailure)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("%s This is the leash-break retry, so the walk home will now use an "
                     "unpathable goal — check navmesh coverage under the spawn point."), *Detail);
        }
        else
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("%s Expected this early on a dynamic navmesh; BreakLeash retries."), *Detail);
        }
        return false;
    }

    PatrolOrigin           = Projected.Location;
    bPatrolOriginProjected = true;

    // Keep the tree's copy in step. On the OnPossess call the Blackboard doesn't
    // exist yet and the write below is a no-op — RunBehaviorTree publishes the
    // already-projected value moments later. On a retry from BreakLeash it does
    // exist, and this is what stops the key going stale.
    if (Blackboard)
    {
        Blackboard->SetValueAsVector(GothicBBKeys::PatrolOrigin, PatrolOrigin);
    }

    return true;
}

void AGothicEnemyAIController::RequestLeashReturnMove()
{
    // Never discard this result. A leash return that is never accepted by path
    // following looks identical to an enemy that simply chose not to move, and
    // that is precisely how a boss stood at speed 0 for the full 20s timeout,
    // 3861uu from her anchor, without a single line in the log.
    // Announce this one call as the leash return's own, so the MoveTo gate lets it
    // through. Scoped — anything the request synchronously triggers is still gated.
    const EPathFollowingRequestResult::Type Result = [this]()
    {
        TGuardValue<bool> OwnMoveScope(bIssuingLeashReturnMove, true);
        return MoveToLocation(PatrolOrigin, 50.f);
    }();

    if (Result == EPathFollowingRequestResult::Failed)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicEnemyAIController[%s]: leash return MoveToLocation FAILED for goal %s "
                 "(anchor projected: %s) — the enemy will not walk home."),
            *GetNameSafe(GetPawn()), *PatrolOrigin.ToCompactString(),
            bPatrolOriginProjected ? TEXT("yes") : TEXT("NO"));
    }
    else if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicEnemyAIController[%s]: leash return reported AlreadyAtGoal for %s while "
                 "%.0fuu out — the goal is resolving to the wrong poly."),
            *GetNameSafe(GetPawn()), *PatrolOrigin.ToCompactString(),
            GetPawn() ? FVector::Dist2D(GetPawn()->GetActorLocation(), PatrolOrigin) : -1.f);
    }
}

void AGothicEnemyAIController::BreakLeash()
{
    if (bLeashReturning)
    {
        return;
    }

    // Second chance at projection: on a dynamically-generated navmesh there is
    // no guarantee the tiles under the spawn point existed at possess time. This
    // is the attempt that matters, so a failure here warns.
    EnsurePatrolOriginProjected(/*bWarnOnFailure=*/true);

    UE_LOG(LogTemp, Log, TEXT("GothicEnemyAIController[%s]: leash broken — disengaging and returning to anchor %s"),
        *GetNameSafe(GetPawn()), *PatrolOrigin.ToCompactString());

    // Set the suppression flag BEFORE clearing, so nothing that reacts to the
    // clear inside the same call stack can re-aggro through the gate.
    bLeashReturning     = true;
    LeashReturnStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

    ClearCombatTarget();

    // Clear the combat decision too, not just the target.
    //
    // ChosenAction is deliberately STICKY — GothicBTService_WeightedActionSelect
    // returns without writing as soon as TargetActor is empty, precisely so the
    // tree can degrade to its non-combat branches. But nothing was clearing the
    // key on the way out, so the last pick survived the whole disengage: measured,
    // ChosenAction read "Reposition" for the entire return in both runs, keeping
    // an equality decorator satisfied and re-running the Reposition branch every
    // frame against a null target (the source of the per-frame
    // ComputeRepositionPoint failure spam). An empty key satisfies no equality
    // decorator, which is what lets the tree's own Selector fallback take over.
    if (Blackboard && Blackboard->GetKeyID(GothicBBKeys::ChosenAction) != FBlackboard::InvalidKey)
    {
        Blackboard->ClearValue(GothicBBKeys::ChosenAction);
    }

    // Drop whatever the tree had in flight. A boss that leashes mid-swing should
    // not carry the swing home with her.
    StopMovement();

    RequestLeashReturnMove();
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
        //
        // But only when there is nothing left running. MoveToLocation aborts the
        // active request before making a new one, so an unconditional re-issue
        // every 2s tears down a perfectly healthy path mid-stride and re-paths
        // from scratch. Checking for Idle keeps the recovery net the comment
        // above describes while leaving a working walk home alone.
        const UPathFollowingComponent* PathComp = GetPathFollowingComponent();
        if (!PathComp || PathComp->GetStatus() == EPathFollowingStatus::Idle)
        {
            RequestLeashReturnMove();
        }
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
    UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(GetBrainComponent());
    if (!BTComp)
    {
        return;
    }

    // One pause at a time. Two pack members dying inside PackRegroupDuration used
    // to arm two independent timers, and the first to fire resumed the tree while
    // the second pause was still nominally running.
    GetWorldTimerManager().ClearTimer(RegroupPauseTimer);

    BTComp->PauseLogic(TEXT("PackRegroup"));

    // Bound to THIS, not to a lambda capturing a raw UBehaviorTreeComponent*.
    //
    // The old form was crash-shaped in two ways at once. The handle was a LOCAL,
    // so the timer could neither be cancelled nor cleared when the controller or
    // pawn was destroyed; and the lambda captured the brain component by raw
    // pointer, which `if (BTComp)` then tested for null rather than for validity —
    // a pack member dying inside the window dereferenced a dangling pointer. A
    // UObject-bound timer is dropped automatically when its object goes away, and
    // the member handle makes the pause cancellable.
    GetWorldTimerManager().SetTimer(
        RegroupPauseTimer,
        this,
        &AGothicEnemyAIController::EndRegroupPause,
        FMath::Max(0.01f, Duration),
        false);
}

void AGothicEnemyAIController::EndRegroupPause()
{
    // Re-resolved rather than captured: the brain component this controller owns
    // at resume time is the only one it is safe to talk to.
    if (UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(GetBrainComponent()))
    {
        BTComp->ResumeLogic(TEXT("PackRegroup"));
    }
}