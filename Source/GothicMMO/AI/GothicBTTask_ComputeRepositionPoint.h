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

UCLASS(meta = (DisplayName = "Gothic Compute Reposition Point"))
class GOTHICMMO_API UGothicBTTask_ComputeRepositionPoint : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UGothicBTTask_ComputeRepositionPoint();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
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

    /** Distance from the target the computed point sits at. */
    UPROPERTY(EditAnywhere, Category = "Reposition")
    float RepositionRadius = 450.f;
};