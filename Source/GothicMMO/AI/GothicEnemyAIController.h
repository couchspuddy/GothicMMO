// GothicEnemyAIController.h
// AI Controller for all Gothic enemies.
// Runs a Behavior Tree (assign BT_EnemyCombat in Blueprint).
// Manages the Blackboard and exposes helper functions to BT Tasks.
//
// Engagement model:
//   Enemies do NOT run directly to the player. They approach to a
//   PreferredEngageDistance, decelerate in the last ApproachDecelDistance,
//   and wait a staggered delay before committing. Attack tasks then
//   close the remaining gap with a lunge built into the animation.
//
// Blueprint child: BP_GothicEnemyAIController
//   - Set BehaviorTreeAsset to BT_EnemyCombat
//   - Assign to enemy Blueprint's AIControllerClass

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GenericTeamAgentInterface.h"
#include "GothicEnemyAIController.generated.h"

class UBehaviorTree;
class UBlackboardComponent;

// ============================================================================
// Blackboard Key Names
// Keep these in sync with your Blackboard asset (BB_Enemy).
// ============================================================================
namespace GothicBBKeys
{
    static const FName TargetActor          = TEXT("TargetActor");            // AActor*
    static const FName TargetLocation       = TEXT("TargetLocation");         // FVector
    static const FName bCanSeeTarget        = TEXT("bCanSeeTarget");          // bool
    static const FName bIsInCombat          = TEXT("bIsInCombat");            // bool
    static const FName PatrolOrigin          = TEXT("PatrolOrigin");           // FVector
    static const FName AttackRange           = TEXT("AttackRange");            // float
    static const FName EngageDistance        = TEXT("EngageDistance");          // float — stop here, don't stand on player
    static const FName StaggerDelay          = TEXT("StaggerDelay");           // float — randomized per-enemy approach delay
    static const FName ChosenAction          = TEXT("ChosenAction");           // FName — written by GothicBTService_WeightedActionSelect
}

UCLASS()
class GOTHICMMO_API AGothicEnemyAIController : public AAIController
{
    GENERATED_BODY()

public:
    AGothicEnemyAIController();

    virtual FGenericTeamId GetGenericTeamId() const override
    {
        return FGenericTeamId(1);
    }

    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

    /**
     * Movement authority gate for the leash return.
     *
     * While bLeashReturning is set, the ONLY move request this controller will
     * accept is its own walk home. Everything else — a BT movement task, a
     * Blueprint MoveTo, anything that reaches AAIController — is refused here.
     *
     * This is the fix for the measured mid-return stall. The Behavior Tree keeps
     * ticking throughout the return (nothing stops it), and it does so with a
     * cleared TargetActor and a stale ChosenAction, so it falls through its combat
     * branches into whatever idle/patrol movement it has. AAIController::MoveTo
     * ABORTS the active request before making a new one, so a single such task
     * firing mid-return tears down the controller's path; the walk home then sits
     * dead until TickLeashReturn's 2s recovery tick notices path following went
     * Idle and re-issues. That is exactly the 1.25s dead stop observed at 680uu
     * from the anchor in one of two runs — intermittent because it depends on the
     * tree happening to reach a movement task during the walk.
     *
     * Refusing at this single choke point covers every mover at once and, unlike
     * pausing the tree, carries no unwind obligation: the gate is a pure function
     * of bLeashReturning, which already has all of its exits (arrival, timeout,
     * unpossess) and cannot leave residual state behind if one is missed.
     */
    virtual FPathFollowingRequestResult MoveTo(const FAIMoveRequest& MoveRequest,
        FNavPathSharedPtr* OutPath = nullptr) override;

    /**
     * Called by AGothicEnemyBase::SetCombatTarget when perception fires.
     * Updates the Blackboard and transitions the BT to combat mode.
     * Rolls a new StaggerDelay each time combat is entered.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI")
    void SetBlackboardTarget(AActor* NewTarget);

    /**
     * Clears the combat target and returns to patrol/idle state.
     * Call when target dies or escapes detection range.
     *
     * Clears BOTH sources of truth — the Blackboard key AND the pawn's own
     * CombatTarget latch. It used to clear only the Blackboard, which made every
     * disengage a no-op: GothicBTService_CombatSync reconciles the two every
     * 0.2s, sees an empty key next to a pawn that still holds a target, and
     * calls SetBlackboardTarget straight back. See BreakLeash.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI")
    void ClearCombatTarget();

    /**
     * False while this enemy is leashing home. AGothicEnemyBase::SetCombatTarget
     * consults this before latching a target, so perception, encounter volumes
     * and pack propagation cannot re-aggro a disengaging enemy the same tick it
     * turned around.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    bool IsAcceptingCombatTargets() const { return !bLeashReturning; }

    /** True while walking back to the anchor after a broken leash. */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    bool IsLeashReturning() const { return bLeashReturning; }

    /** The anchor this enemy leashes to — its spawn/placement location. */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    FVector GetLeashAnchor() const { return PatrolOrigin; }

    /**
     * Returns the current Blackboard target as a typed actor.
     * Useful in BT Tasks to avoid casting to UObject manually.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    AActor* GetTargetActor() const;

    /**
     * Returns true if the target is within MeleeAttackRange.
     * This gates the attack task — NOT the approach distance.
     * The enemy holds at EngageDistance, then the attack task
     * checks this before lunging.
     *
     * Measured HORIZONTALLY (XY only). Capsule half-heights differ per creature
     * and put a permanent floor under any 3D pawn-to-pawn distance; see the
     * implementation.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    bool IsTargetInAttackRange() const;

    // -------------------------------------------------------------------------
    // Engagement tuning — read by BTService_ApproachSpeed
    // -------------------------------------------------------------------------

    /** The distance the enemy tries to hold before attacking (cm). */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetPreferredEngageDistance() const { return PreferredEngageDistance; }

    /** Distance at which the enemy begins decelerating (cm). */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetApproachDecelDistance() const { return ApproachDecelDistance; }

    /** Speed multiplier at engage distance (0-1). */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetDecelSpeedMultiplier() const { return DecelSpeedMultiplier; }

    /** The default walk speed before any decel scaling. Cached on possess. */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetDefaultWalkSpeed() const { return DefaultWalkSpeed; }
    
    /** Pauses the BT for the given duration — pack regroup on member death. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI")
    void EnterRegroupPause(float Duration);


protected:
    /** Assign in BP_GothicEnemyAIController. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

    /**
     * Standard melee attack range (cm), measured HORIZONTALLY.
     * This is the "can I attack" gate, not the approach distance.
     * Should be SMALLER than PreferredEngageDistance — the attack's
     * lunge animation covers the gap.
     *
     * Horizontal because actor locations sit at capsule centres: two pawns in
     * contact on the same floor are still |HalfHeightA - HalfHeightB| apart in
     * 3D. Tuning this against a 3D measurement means every creature's reach
     * silently changes whenever its capsule does.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Attack")
    float MeleeAttackRange = 200.f;

    /**
     * Distance the enemy holds from the target before attacking (cm),
     * measured HORIZONTALLY like every other range in the engagement model.
     * Set this per-tier in Blueprint children:
     *   Thrall: ~250   (close but not on top)
     *   Retained: ~350 (deliberate, measured)
     *   Feral: ~300    (fast closer, earns the gap)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Engagement")
    float PreferredEngageDistance = 300.f;

    /**
     * Distance at which the enemy starts decelerating (cm).
     * When closer than this to the target, speed scales down linearly
     * from full to DecelSpeedMultiplier at EngageDistance.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Engagement")
    float ApproachDecelDistance = 500.f;

    /**
     * Speed multiplier at engagement distance.
     * 0.6 means 60% of MaxWalkSpeed when arriving at stance distance.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Engagement")
    float DecelSpeedMultiplier = 0.6f;

    /**
     * Range for the randomized stagger delay (seconds).
     * Each time combat is entered, a random value in this range is
     * written to the Blackboard. The BT uses a Wait node reading
     * StaggerDelay before beginning the approach.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Engagement")
    FVector2D StaggerDelayRange = FVector2D(0.3f, 1.2f);

    /**
     * Range at which the enemy gives up chasing and returns to patrol (cm).
     * Should be larger than the perception LoseSightRadius.
     *
     * Measured HORIZONTALLY from PatrolOrigin, like every other range in this
     * class — see IsTargetInAttackRange for why. 3000 was already the tuned C++
     * default, and the leash never failing had nothing to do with its size.
     *
     * The ratios that used to be quoted here ("~8.5x the reposition orbit, 10x
     * the engage distance") described the C++ CDO and NOTHING ELSE, which made
     * them wrong everywhere they mattered. Blueprint children override both
     * numbers: the Bestial Lucid runs LeashRange 10000 against a
     * PreferredEngageDistance of 150 — a ratio of ~67x, not 10x — and the
     * reposition radius is a property of the tree, not of this class (boss 350,
     * Thrall 450). Read the values off the Blueprint, not off this comment.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI")
    float LeashRange = 3000.f;

    // ── Leash / disengage ────────────────────────────────────────────────────

    /**
     * Also break the leash when the TARGET leaves LeashRange of the anchor, not
     * only when the enemy itself has been dragged that far out.
     *
     * This is the arena half of the leash. Anchor-distance alone means the boss
     * has already walked the whole leash length out of her room before she gives
     * up — measured 2026-08-01, the Bestial Lucid followed a teleported player
     * 7,187uu out of the Rotunda. With this on she turns around at the first
     * check after the player leaves the arena instead.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|AI|Leash")
    bool bLeashOnTargetDistance = true;

    /**
     * How close to the anchor counts as home (cm), horizontally.
     *
     * 150 = 3x the 50uu acceptance radius the return MoveToLocation already
     * passes, so arrival still registers when the path legitimately stops short
     * on a navmesh edge — and comfortably inside the 200uu MeleeAttackRange, so
     * "home" is tighter than a single melee step.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|AI|Leash")
    float LeashReturnAcceptanceRadius = 150.f;

    /**
     * Failsafe (seconds). If the walk home hasn't finished by now, reset anyway
     * rather than leave an enemy permanently unaggroable.
     *
     * 20 = LeashRange 3000uu over a conservative 300uu/s enemy walk speed (10s
     * of travel) doubled for pathing detours. Not a tuning knob for feel — it
     * only fires when navigation has failed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|AI|Leash")
    float LeashReturnTimeoutSeconds = 20.f;

    /**
     * Restore Health to MaxHealth when the enemy gets home.
     *
     * True is the arena-boss default: a boss you can leash, heal from, and
     * re-pull at low health is a boss you can kill by walking. Restored on
     * ARRIVAL, not at the moment the leash breaks, so a player who chases her
     * back into the arena fights the health bar she left with until she's home.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|AI|Leash")
    bool bRestoreHealthOnLeashReset = true;

    /**
     * Breaks the leash: full disengage, walk back to the anchor, and suppress
     * re-aggro until home. Virtual only so subclasses can log/telegraph it —
     * the state reset itself belongs in OnLeashReset.
     */
    virtual void BreakLeash();

    /**
     * Called once the enemy is home and combat state has been wiped. Base
     * restores health; boss subclasses reset phase state here. Anything a fight
     * mutates that a fresh pull must not inherit belongs in an override.
     */
    virtual void OnLeashReset();


private:
    /**
     * The target SetBlackboardTarget was asked to write while there was no
     * Blackboard to write it to.
     *
     * Aggro can fire before the BT starts — the encounter volume wakes the boss
     * on overlap, and OnPossess/RunBehaviorTree has not necessarily happened
     * yet. The old early-return dropped that call on the floor: the pawn-side
     * CombatTarget stuck, the key stayed empty forever, and every task that
     * resolves TargetActor failed for the rest of the fight. Held here and
     * flushed the moment the Blackboard exists.
     */
    TWeakObjectPtr<AActor> PendingBlackboardTarget;

    /**
     * Cached patrol spawn point — enemy returns here when leash breaks.
     *
     * PROJECTED ONTO THE NAVMESH, not the raw actor location. An actor's location
     * is its capsule centre, which floats one scaled half-height above the floor;
     * for the Bestial Lucid that is 379.5uu (capsule 253 at scale 1.5), past the
     * 250uu default vertical extent Recast resolves a goal poly with. The raw
     * value therefore had no nav poly at all and every MoveToLocation to it was
     * rejected — the boss stood still for the whole leash timeout. See
     * EnsurePatrolOriginProjected.
     */
    FVector PatrolOrigin;

    /** True once PatrolOrigin has been successfully snapped to the navmesh. */
    bool bPatrolOriginProjected = false;

    /**
     * Snaps PatrolOrigin to the navmesh with an explicit query extent, once.
     * Idempotent and safe to retry — called at possess time and again at
     * BreakLeash in case the navmesh wasn't generated yet on the first attempt.
     * Returns true if the anchor is now a pathable point.
     *
     * bWarnOnFailure separates the two callers, because only one of them is
     * reporting a problem. The OnPossess attempt is EXPECTED to fail on a
     * world-partition / dynamically-generated navmesh — the tiles under the spawn
     * point are not guaranteed to exist that early, and every possessed AI in the
     * level failed it at t=0 (19 identical warnings per PIE start). Nothing is
     * broken at that point: the BreakLeash retry is what carries the fix, and it
     * succeeds. So the possess-time attempt logs Verbose and the BreakLeash retry
     * — where a failure genuinely does mean the enemy cannot path home — warns.
     */
    bool EnsurePatrolOriginProjected(bool bWarnOnFailure);

    /** Issues the walk-home move request and logs a rejected one. */
    void RequestLeashReturnMove();

    /** Cached default walk speed from the possessed pawn's movement component. */
    float DefaultWalkSpeed = 0.f;

    /** Periodic check to see if the target escaped the leash range. */
    FTimerHandle LeashCheckTimer;
    void CheckLeash();

    /**
     * True when Actor is still worth fighting: valid, and alive if it is a
     * AGothicCharacterBase. Static because it asks nothing of this controller.
     */
    static bool IsFightableTarget(const AActor* Actor);

    /**
     * Clears the engagement when neither the Blackboard key nor the pawn latch
     * still holds a fightable target. Run from CheckLeash — see its body for why
     * this exists at all (nothing but BreakLeash ever cleared a combat target).
     */
    void DropTargetIfNoLongerFightable();

    /** Handle for the pack-regroup BT pause. Member, so the pause is cancellable. */
    FTimerHandle RegroupPauseTimer;

    /** Resumes the behaviour tree after EnterRegroupPause. */
    void EndRegroupPause();

    /** True from BreakLeash until the enemy is home (or the failsafe fires). */
    bool bLeashReturning = false;

    /**
     * Set only for the duration of the controller's own leash-return move request,
     * so the MoveTo gate above can tell that one call apart from every other
     * caller. Scoped with TGuardValue at the single call site.
     */
    bool bIssuingLeashReturnMove = false;

    /** World seconds the current return started, for LeashReturnTimeoutSeconds. */
    float LeashReturnStartTime = 0.f;

    /** Drives the walk home and decides when it's finished. */
    void TickLeashReturn();
};