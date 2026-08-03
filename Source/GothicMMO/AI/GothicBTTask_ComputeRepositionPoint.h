// GothicBTTask_ComputeRepositionPoint.h
// Computes a point offset laterally from the target's current bearing and
// writes it to a Vector key. Deliberately NOT a latent movement task — the
// Reposition branch pairs this with a stock BTTask_MoveTo bound to that key,
// reusing proven movement code instead of reimplementing move-completion
// tracking. This task's only job is picking WHERE.
//
// Why this exists: without it, "Reposition" in the weighted pool has nowhere
// to point — the only movement behavior available is walking straight at
// the target (Approach, or the root Selector's Move To fallback), which
// reads as pure pursuit regardless of how the ability-selection weighting
// varies. A predator that only ever walks straight at prey isn't prowling,
// it's chasing. This gives the pool a second kind of movement to pick.
//
// It now picks between three: a menacing hold, a lateral strafe, and a give-
// ground disengage. The third exists because the rest of the pool physically
// cannot produce one — every other branch closes distance or preserves it — so
// separation was a one-way ratchet and the boss ended every fight parked in the
// player's face. See the Give Ground block at the bottom of this header.
//
// Instant, not latent — runs once per activation, writes the point, ends
// immediately. The Sequence's next node (stock Move To) does the actual
// walking.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GothicBTTask_ComputeRepositionPoint.generated.h"

/**
 * Per-instance state for the menacing hold. When the task rolls a hold instead
 * of a strafe, it stays InProgress and ticks this timer down so the boss stands
 * planted (facing the player via the controller's focus lock) for a beat before
 * the Sequence's Move To runs as a no-op.
 */
struct FGothicRepositionMemory
{
    float HoldElapsed = 0.f;
    bool  bHolding    = false;
};

UCLASS(meta = (DisplayName = "Gothic Compute Reposition Point"))
class GOTHICMMO_API UGothicBTTask_ComputeRepositionPoint : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UGothicBTTask_ComputeRepositionPoint();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        float DeltaSeconds) override;
    virtual uint16 GetInstanceMemorySize() const override { return sizeof(FGothicRepositionMemory); }
    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual FString GetStaticDescription() const override;

protected:
    UPROPERTY(EditAnywhere, Category = "Reposition")
    FBlackboardKeySelector TargetActorKey;

    /** Where the computed point gets written. Bind a Move To after this task to this same key. */
    UPROPERTY(EditAnywhere, Category = "Reposition")
    FBlackboardKeySelector OutputPointKey;

    /**
     * Lateral offset range, degrees, applied to either side of the current
     * bearing at random. Deliberately excludes near-zero (that's just
     * Approach again) and near-180 (that's a retreat, which is a separate
     * branch with its own gate and its own distance band — see Give Ground
     * below — not something the strafe should back into by accident).
     */
    UPROPERTY(EditAnywhere, Category = "Reposition")
    float MinAngleOffset = 45.f;

    UPROPERTY(EditAnywhere, Category = "Reposition")
    float MaxAngleOffset = 110.f;

    /**
     * MAXIMUM distance from the target the computed point sits at.
     *
     * Not the exact distance: the strafe orbits at whatever separation the
     * pawn already has, and only clamps down to this when the pawn is farther
     * out. Seating the point at this radius unconditionally turned every
     * reposition into a retreat — an enemy in contact was pushed back out
     * past its own attack reach, and then had to walk the whole way in again,
     * which is the orbit-and-never-swing behavior this pairs with.
     */
    UPROPERTY(EditAnywhere, Category = "Reposition")
    float RepositionRadius = 450.f;

    /**
     * MINIMUM 2D displacement the computed point must sit from where the pawn
     * currently stands. A floor on the move, not on the orbit.
     *
     * The orbit-at-current-separation rule above is right about WHERE to stand
     * but says nothing about how far the walk actually is, and the two are not
     * the same number: the point sits on an arc around the target, so the
     * displacement is the CHORD, and the chord collapses as the pawn closes.
     * The Bestial Lucid parks at a measured 125uu from the player, which with
     * a 45-110 degree offset is a chord of only 96-205uu — against a capsule
     * radius of 90uu (60 x 1.5 scale). A stock Move To's reach test adds the
     * agent radius to its AcceptableRadius, so a walk that short can register
     * as AlreadyAtGoal and return instantly with no path request and no log
     * line, which is exactly the measured symptom: Reposition chosen on 92-100%
     * of samples and the boss moving exactly 0.0uu across 299 of them.
     *
     * 200uu is that 90uu capsule plus a stock AcceptableRadius of 50 and half
     * again on top, so the floor holds without this file having to read the
     * paired node's tuning. Not higher: the maximum chord available at a fixed
     * separation is 2*R, which at her 125uu is exactly 250uu, so a 250 floor
     * would demand a near-180-degree swing every time — a lap around the
     * player, not a strafe — and would force the radius concession constantly.
     * 200 is reachable at ~106 degrees with the separation untouched.
     *
     * Reached by widening the bearing first and stepping the radius outward
     * only when the capped arc cannot get there, because separation is the one
     * budget a reposition must not spend (see the ordering note in
     * ExecuteTask).
     */
    UPROPERTY(EditAnywhere, Category = "Reposition", meta = (ClampMin = "0.0"))
    float MinRepositionDistance = 200.f;

    // -------------------------------------------------------------------------
    // Menacing hold — the "she doesn't just orbit forever" beat.
    //
    // Reposition is the movement the pool leans on during ability cooldowns,
    // and it's range-gated to close quarters, so it fires exactly when the
    // player is crowded and wants a moment to line up a shot. Instead of always
    // strafing, this rolls a chance to plant in place: write the current spot to
    // the output key (so the paired Move To is a no-op) and stay InProgress for
    // HoldDuration. The controller's focus lock keeps her squared on the player
    // the whole time, so a hold reads as a predator sizing you up rather than a
    // lull. minMovementCommitDuration on the service already prevents the pool
    // from yanking the decision away mid-hold.
    // -------------------------------------------------------------------------

    /** Chance (0-1) a Reposition becomes a stand-and-menace instead of a strafe.
     *  Divided at runtime by the arena aggression multiplier where a
     *  GothicBossArenaManager exists, so holds get rarer as pillars fall; the
     *  authored value is what rolls at the baseline 1.0 and in every level that
     *  has no manager, and it is a hard CEILING — aggression can only ever
     *  reduce this, never inflate it (see the clamp in ExecuteTask). */
    UPROPERTY(EditAnywhere, Category = "Menace", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HoldChance = 0.35f;

    /** Seconds to hold the menace before the branch completes. */
    UPROPERTY(EditAnywhere, Category = "Menace", meta = (ClampMin = "0.0"))
    float HoldDuration = 1.6f;

    // -------------------------------------------------------------------------
    // Give ground — the disengage the action pool cannot otherwise express.
    //
    // Read the Bestial Lucid's authored pool and nothing in it can INCREASE
    // separation: Approach closes, Charge closes, Claw and Roar are attacks that
    // fire from where she stands, and Reposition orbits at
    // min(CurrentDistance2D, RepositionRadius) — a ceiling, never a floor, by
    // deliberate design (see RepositionRadius above; that ceiling is what fixed
    // the orbit-and-never-swing bug and it is not up for revision here).
    // Separation is therefore a one-way ratchet: every branch either shortens it
    // or preserves it, so once she converges on her measured ~125uu parking
    // distance she stays in the player's face for the rest of the fight and
    // there is never a punish window. The intended fantasy — stalk, pounce,
    // brief assault, disengage, repeat — is missing its fourth beat entirely.
    //
    // This branch is that beat, and it is the ONLY place in the kit permitted to
    // spend separation, which is why it is gated behind both a low chance and a
    // proximity test rather than living in the orbit math.
    // -------------------------------------------------------------------------

    /** Chance (0-1) an eligible Reposition becomes a disengage instead of a hold
     *  or a strafe. Rolled FIRST, ahead of the hold, because it is the rarest
     *  and most deliberate beat in the branch — losing it to a hold roll would
     *  make it rarer still and much harder to read on a timeline.
     *
     *  Deliberately NOT scaled by the arena aggression multiplier: aggression
     *  buys back holds (standing still), and a disengage is not standing still —
     *  it is the setup for a Charge. Making a stripped arena disengage less
     *  would remove the pounce exactly when the fight is meant to peak. */
    UPROPERTY(EditAnywhere, Category = "Give Ground", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float GiveGroundChance = 0.25f;

    /**
     * Distance band from the TARGET the disengage point sits in — an absolute
     * separation to arrive at, not a displacement to travel, which is the whole
     * difference between this and the strafe above.
     *
     * 600-750 is chosen against Charge's authored 550-2000 band and nothing
     * else. Charge is currently unreachable by construction: no branch in the
     * pool can push her past Reposition's 500uu maxRange, so her separation
     * never enters the band its minRange demands and the ability has never had
     * an opportunity to fire in normal play. Landing the disengage just inside
     * that band means the retreat-then-charge pattern falls out of the geometry
     * rather than needing a scripted chain — she gives ground, becomes Charge-
     * eligible on the next selection, and pounces. 600 rather than 550 so a
     * little overshoot or player drift during the walk does not drop her back
     * under the minRange before the pool re-rolls; 750 rather than higher so the
     * walk back in is a pounce and not a trek.
     *
     * These are STARTING POINTS for a measured pass, not tuned values. Nothing
     * here has been observed in PIE — the band is derived from the authored
     * action-pool numbers, and the real disengage distance should be set from
     * measured time-to-close and the player's actual punish window.
     */
    UPROPERTY(EditAnywhere, Category = "Give Ground", meta = (ClampMin = "0.0"))
    float GiveGroundMinDistance = 600.f;

    UPROPERTY(EditAnywhere, Category = "Give Ground", meta = (ClampMin = "0.0"))
    float GiveGroundMaxDistance = 750.f;

    /**
     * Eligibility gate: give ground only rolls when the pawn is already closer
     * to the target than this. Beyond it the branch never fires.
     *
     * A disengage is a response to CROWDING, not a movement option. Without the
     * gate she would roll it from mid-range too and the result reads as timidity
     * — backing away from a player she was never pressuring — and would also
     * fight the Approach branch for the same beats. 250uu sits comfortably above
     * the measured ~125uu parking distance and above Claw's 160uu reach, so the
     * gate passes in exactly the situation the user described (on top of the
     * player, no punish window) and stays shut once she has any real spacing.
     *
     * Also a starting point, not a measurement.
     */
    UPROPERTY(EditAnywhere, Category = "Give Ground", meta = (ClampMin = "0.0"))
    float GiveGroundTriggerRange = 250.f;

    /**
     * Bearing jitter, degrees, applied to either side of the straight-away
     * bearing at random.
     *
     * Deliberately its own small spread rather than a reuse of
     * MinAngleOffset/MaxAngleOffset (45-110): those exist to make a strafe read
     * as a strafe, and 45-110 degrees off the retreat line would turn a
     * disengage into a wide arc across the front of the player — visually a
     * flank, and it would drag her through the player's firing line for the
     * whole walk. A disengage should read as backing off along her own side of
     * the arena. 25 degrees is enough that repeated disengages do not retrace
     * one line into a rut, and small enough that every one of them still reads
     * as "away".
     */
    UPROPERTY(EditAnywhere, Category = "Give Ground", meta = (ClampMin = "0.0", ClampMax = "89.0"))
    float GiveGroundAngleJitter = 25.f;
};