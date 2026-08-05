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

/**
 * Fired exactly once per leap flight, on whichever ending arrives first.
 *
 * bLanded is true only for a real ACharacter landing. The safety timer, an
 * unpossess and an explicit AbortLeapFlight all pass false — a listener that
 * ends an ability on this delegate must be woken by those endings too, or a
 * leap that never lands leaves the ability hanging forever, which is the
 * failure mode the guard exists to prevent.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGothicLeapLandedSignature, bool, bLanded);

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
     *
     * The horizontal reach is paired with a separate vertical test — the two
     * bodies' capsule spans must overlap, within MeleeVerticalSlack — so a
     * target on another floor cannot satisfy this gate. The two checks are
     * deliberately not collapsed into one 3D distance.
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

    // ── Leap flight guard ────────────────────────────────────────────────────
    //
    // Lives on the ENEMY base, not the boss base. Any creature that leaves the
    // ground under its own launch has this problem, and it is not a boss
    // problem: it is a problem with the relationship between LaunchCharacter
    // and a behaviour tree that goes on running. Putting the guard on
    // AGothicBossAIController would guarantee rediscovering it the first time a
    // Thrall lunges.
    //
    // First written and PIE-proven on AGothicFeralRetainedController for
    // GA_FeralBreakout; hoisted here verbatim, plus the Blueprint surface the
    // Bestial Lucid's Charge graph needs.

    /**
     * Call immediately BEFORE LaunchCharacter. Makes the flight steering-proof
     * from both ends until the pawn lands.
     *
     * The launch itself was never the problem — it already overrides both axes.
     * What killed it was everything that happens on the ticks AFTER: the
     * behaviour tree re-issues a Move To toward the player, and in MOVE_Falling
     * that request is spent as air-control acceleration against the launch's XY
     * velocity. Measured in PIE on the Feral Retained: with the player standing
     * OPPOSITE the leap direction she gained her full +322uu of height and
     * travelled 7-13uu horizontally — she went straight up and landed where she
     * stood.
     *
     * So the brain is paused (no new move requests) AND AirControl is driven to
     * zero (an in-flight request that slips through cannot bend the arc anyway).
     * AirControl is set on the Blueprint, not in C++, so zeroing it is the only
     * value-independent way to be sure.
     *
     * Both are undone on landing — ACharacter::LandedDelegate, with a timer
     * fallback so a missed landing can never leave a pawn permanently
     * brain-dead. Idempotent: a second call while already in flight only
     * refreshes the safety timer, so SavedAirControl is never overwritten with
     * the zero this function just wrote.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI|Leap")
    void BeginLeapFlight();

    /**
     * Ends the flight early — the ability driving it was cancelled, the pawn was
     * staggered mid-arc, a phase transition tore the action down.
     *
     * Restores the brain and AirControl exactly as a landing would and
     * broadcasts OnLeapLanded with bLanded=false. Safe when no flight is in
     * progress. An ability's EndAbility should call this unconditionally: the
     * normal case is a no-op because the landing already unwound it, and the
     * cancel case is the difference between a boss who recovers and a boss
     * wedged with a paused brain and zero air control.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI|Leap")
    void AbortLeapFlight();

    /** True between BeginLeapFlight and the landing (or its fallback). */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI|Leap")
    bool IsLeapInFlight() const { return bLeapInFlight; }

    /**
     * The end of a leap, whichever way it ended. Bind an ability to this rather
     * than ending it on a flat timer — a timer that outlives the arc lets the
     * tree re-plan mid-flight, and a timer that undershoots it resolves the
     * ability's range checks at the launch position instead of the landing one.
     */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|AI|Leap")
    FGothicLeapLandedSignature OnLeapLanded;


protected:
    /**
     * Hard ceiling on a leap flight, in seconds. If the landing never arrives —
     * the pawn clips into geometry, gets teleported, dies mid-arc — this
     * restores its brain and its air control anyway.
     *
     * 3 is comfortably longer than the ~1.3s the Feral Retained's measured arc
     * spends airborne. Per-creature, so a longer leap can raise it without
     * every other creature waiting that long for a failed landing.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Leap",
        meta = (ClampMin = "0.5", ClampMax = "10.0"))
    float LeapFlightSafetySeconds = 3.f;

    /** Unpause the brain, restore AirControl, unbind, clear the timer, broadcast. Idempotent. */
    void EndLeapFlight(bool bLanded, const TCHAR* Reason);


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
     * How much clear vertical air (cm) may sit between the two bodies and the
     * melee gate still pass.
     *
     * MeleeAttackRange is horizontal on purpose (see above), but nothing was
     * bounding Z at all, and the old claim that "floors are separated by
     * navigation and line of sight" was never enforced by anything: an enemy
     * under a mezzanine with the player directly above it passed the attack
     * gate on every tick and swung into the ceiling forever, because the XY
     * separation was small and the tree had no other reason to disengage.
     * Measured on the Rotunda mezzanine: 19 consecutive whiffs, the pair 92.5uu
     * apart horizontally and 199.9uu apart vertically (220.3 in 3D).
     *
     * The check compares feet-to-head SPANS, not origin dZ, because origins sit
     * at capsule centres and a threshold on origin dZ cannot survive this
     * project's creature sizes: the Bestial Lucid's scaled half-height is 379.5
     * (253 authored, actor scale 1.5), so she and a player on the same flat
     * floor are ~290uu apart in origin Z — more than the ~200uu of a real
     * storey. Same-floor pairs of ANY size overlap vertically; a storey apart
     * leaves genuine clear air (+19.9uu in the measured case).
     *
     * So this value is small by design: it exists for stairs, slopes and a
     * pawn mid-step, not to encode a floor height. Raising it past ~20 starts
     * re-admitting the cross-floor case this fixes. EditAnywhere so a specific
     * encounter can be corrected on the instance without a re-place.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|AI|Attack")
    float MeleeVerticalSlack = 15.f;

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
     * them wrong everywhere they mattered. Blueprint children and placed
     * instances override the number to fit their room, and the reposition radius
     * is a property of the tree, not of this class (boss 350, Thrall 450). Read
     * the values off the Blueprint and the instance, not off this comment.
     *
     * KEPT AS A WARNING: this comment used to record that the Bestial Lucid ran
     * LeashRange 10000. That single override is why she chased respawning
     * players out of the Rotunda to the PlayerStart, with every part of the
     * leash machinery below working correctly the whole time. See
     * AGothicBossAIController_BestialLucid's constructor for the replacement.
     *
     * EDITANYWHERE, not EditDefaultsOnly. A placed enemy freezes its overrides at
     * placement time, so an EditDefaultsOnly leash can never be repaired on an
     * actor already standing in the level — the only remedy is to delete and
     * re-place it. A leash radius is per-encounter data: an arena boss and a
     * corridor patroller want different numbers out of the same class, and the
     * entire point of the property is that it gets fitted to the room the enemy
     * is guarding. That belongs on the instance.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|AI")
    float LeashRange = 3000.f;

    /**
     * How far a perception rescan may reach (cm, horizontal).
     *
     * FindBestPerceivedTarget asks for every CURRENTLY perceived actor and used
     * distance only to sort them, so any stimulus the perception component still
     * holds was a valid re-acquisition regardless of range. With hearing stimuli
     * that never aged out, that meant an enemy who had once heard a shot pulling
     * the player back in from 7010uu, every 6s, each time starting an Approach the
     * leash then cancelled.
     *
     * 2000 = the sight config's LoseSightRadius: the rescan can reach exactly as
     * far as sight can hold. Do NOT tune it lower — the in-arena cases (a boss
     * re-acquiring across her own room) live inside this radius, and shortening it
     * regresses them.
     *
     * EDITANYWHERE for the same reason as LeashRange above: a placed enemy freezes
     * its overrides at placement, so an EditDefaultsOnly value could never be
     * repaired on an actor already standing in the level.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|AI")
    float ReacquireRange = 2000.f;

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
    /** Landing hook bound to the pawn's LandedDelegate for the leap's duration. */
    UFUNCTION()
    void HandleLeapLanded(const FHitResult& Hit);

    /** Fallback if the landing never fires — see LeapFlightSafetySeconds. */
    void HandleLeapFlightTimeout();

    /** In-flight guard — EndLeapFlight is racing four callers (landing, timer, abort, unpossess). */
    bool bLeapInFlight = false;

    /** AirControl as it was before the leap, restored verbatim on landing. */
    float SavedAirControl = 0.f;

    FTimerHandle LeapFlightSafetyHandle;

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
     * True when Actor is still worth fighting. Thin forward to
     * AGothicCharacterBase::IsFightableActor, which is the one definition of
     * fightability the whole project shares — do not re-implement the test here.
     * Static because it asks nothing of this controller.
     */
    static bool IsFightableTarget(const AActor* Actor);

    /**
     * The fightable actor this enemy is currently engaged with, or nullptr if it
     * has none — Blackboard key first, pawn combat-target latch second. Used by
     * the leash to ask whether there is still a quarry worth staying out for.
     */
    AActor* GetLeashQuarry() const;

    /**
     * Clears the engagement when neither the Blackboard key nor the pawn latch
     * still holds a fightable target. Run from CheckLeash — see its body for why
     * this exists at all (nothing but BreakLeash ever cleared a combat target).
     */
    void DropTargetIfNoLongerFightable();

    /**
     * The nearest currently-perceived fightable actor, or nullptr.
     *
     * Asks the pawn's perception component for its CURRENT set rather than
     * reading an event payload — the whole point is to answer the question at a
     * moment when no perception event is arriving.
     *
     * Nearest wins. The edge path (AGothicEnemyBase::OnPerceptionUpdated) takes
     * the first sensed actor in the update array because an update names one or
     * two actors; a full re-scan can see the whole room, and "first in an
     * unordered array" would be arbitrary there. 2D, like every other range in
     * this class — see IsTargetInAttackRange for why capsule heights must never
     * enter a combat distance.
     */
    AActor* FindBestPerceivedTarget() const;

    /**
     * Acquires the best currently-perceived fightable actor if this enemy has no
     * target. Returns true if one was latched.
     *
     * WHY THIS EXISTS: acquisition was purely edge-triggered — OnPerceptionUpdated
     * fires when perception CHANGES, and a player who was already perceived and
     * stays perceived generates no further events. So any code path that cleared
     * TargetActor while the player stood in plain sight left the enemy permanently
     * idle: nothing was ever going to re-deliver an actor perception already knew
     * about. Measured on a 2-player listen server — an enemy dropped a downed P2
     * (correctly), P2 was revived 285uu in front of it with LOS clear and
     * currently_seen_by_sight true, and the blackboard read TargetActor: None
     * forever. The same hole meant an enemy that dropped a downed player only
     * "turned on the survivors" if a fresh perception edge happened to arrive.
     *
     * Routed through AGothicEnemyBase::SetCombatTarget, the single aggro choke
     * point, so the leash gate still refuses it while walking home.
     *
     * Via is a short tag for the telemetry line only.
     */
    bool ReacquireFromPerception(const TCHAR* Via);

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