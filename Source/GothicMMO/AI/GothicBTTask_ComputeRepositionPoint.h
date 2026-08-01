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
     * Approach again) and near-180 (that's a retreat, a different trait
     * entirely — the boss doesn't back off tactically, per her kit).
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

    /** Chance (0-1) a Reposition becomes a stand-and-menace instead of a strafe. */
    UPROPERTY(EditAnywhere, Category = "Menace", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HoldChance = 0.35f;

    /** Seconds to hold the menace before the branch completes. */
    UPROPERTY(EditAnywhere, Category = "Menace", meta = (ClampMin = "0.0"))
    float HoldDuration = 1.6f;
};