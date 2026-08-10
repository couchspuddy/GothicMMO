// GothicBTTask_MoveToOrbit.h
// Holds the pawn on a ring at engagement distance around its combat target and
// keeps packmates from stacking on top of one another, so a group reads as a
// loose cordon instead of a pile on the player's origin.
//
// This is a LATENT movement task, not a compute-a-point task like
// GothicBTTask_ComputeRepositionPoint. It resolves the target the same way the
// engagement services do — AGothicEnemyAIController::GetTargetActor(), no
// Blackboard key — computes a ring point along the pawn's CURRENT bearing to
// the target (the angle is preserved, never snapped, so each enemy holds the
// sector it already occupies), nudges that point away from any packmate inside
// the separation radius, nav-projects it, and walks there through the pawn's
// existing MoveToLocation/nav path. No new movement system.
//
// It stays InProgress and re-picks on a short interval rather than every tick:
// the ring point tracks the player as they move and the separation nudge
// responds to the pack shuffling, but the pawn is not issued a fresh path every
// frame. The task never completes on its own — a ring hold is a persistent
// posture; the tree's higher-priority branches (attack once in range / holding
// a token) abort it, and AbortTask stops the walk cleanly.
//
// Placement: the fallback/approach branch, beneath the attack branch a
// decorator gates. When the attack decorator opens, it aborts this and the pawn
// closes; when it shuts again the pawn falls back here and re-forms the ring.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GothicBTTask_MoveToOrbit.generated.h"

/**
 * Per-instance state. Just the re-pick timer and whether a move is live, so the
 * tick can throttle path requests to RepickInterval instead of spamming one per
 * frame.
 */
struct FGothicOrbitMemory
{
    float TimeSinceRepick = 0.f;
    bool  bMoveIssued     = false;
};

UCLASS(meta = (DisplayName = "Gothic Move To Orbit"))
class GOTHICMMO_API UGothicBTTask_MoveToOrbit : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UGothicBTTask_MoveToOrbit();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        float DeltaSeconds) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual uint16 GetInstanceMemorySize() const override { return sizeof(FGothicOrbitMemory); }
    virtual FString GetStaticDescription() const override;

protected:
    /**
     * Radius of the ring, cm — how far from the target the pawn tries to stand.
     * The pawn keeps its current bearing to the target and only slides in/out to
     * this distance, so the ring forms without anyone circling to a new angle.
     *
     * The engagement model already carries a PreferredEngageDistance on
     * AGothicEnemyAIController (used by ApproachSpeed/WeightedActionSelect); this
     * task deliberately keeps its own knob so the ring radius can be tuned per
     * behaviour tree without moving the speed-ramp's floor. Wiring note: if a
     * pawn's authored PreferredEngageDistance and this diverge badly the pawn
     * ping-pongs between the ramp's floor and the ring — keep them close.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Orbit", meta = (ClampMin = "0.0"))
    float EngagementDistance = 250.f;

    /**
     * Packmates whose capsule origin sits within this 2D radius push this pawn's
     * ring point away from them (sphere overlap on the Pawn object channel). The
     * combat target is excluded from the query — the ring already sets distance
     * from the player; this is purely enemy-vs-enemy spacing.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Orbit", meta = (ClampMin = "0.0"))
    float SeparationRadius = 120.f;

    /**
     * Maximum lateral shove, cm, the separation nudge can add to the ring point.
     * The crowd vector is normalized before scaling, so N packmates cannot sum
     * into a launch — the push saturates at this magnitude no matter how tight
     * the pile.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Orbit", meta = (ClampMin = "0.0"))
    float SeparationPushStrength = 120.f;

    /**
     * Seconds between re-picks. The task stays InProgress the whole time; this
     * only throttles how often a fresh ring point is computed and a new path
     * issued. Short enough that the ring tracks a moving player, long enough that
     * the pawn is not re-pathing every frame.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Orbit", meta = (ClampMin = "0.05"))
    float RepickInterval = 0.5f;

    /**
     * Acceptance radius handed to MoveToLocation. Kept small — the whole point is
     * to seat the pawn ON the ring — but non-zero so a pawn already in place does
     * not thrash trying to hit an exact point.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Orbit", meta = (ClampMin = "0.0"))
    float MoveAcceptanceRadius = 40.f;

private:
    /** Computes the ring point (ring + separation nudge), returns false if the
     *  target is gone. Fills bOutProjected with whether nav-projection succeeded. */
    bool ComputeOrbitPoint(const APawn& Pawn, const AActor& Target,
        FVector& OutPoint, bool& bOutProjected) const;

    /** Sum of weighted away-vectors from packmates inside SeparationRadius,
     *  clamped and scaled to SeparationPushStrength. 2D. Excludes Target. */
    FVector ComputeSeparation(const APawn& Self, const AActor& Target) const;

    /** Recomputes the point and (re)issues the move. Logs the timeline line. */
    void IssueOrbitMove(UBehaviorTreeComponent& OwnerComp, FGothicOrbitMemory& Memory) const;
};
