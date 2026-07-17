// GothicBTService_CombatSync.h
// The bridge between GAS and the Behavior Tree that this project has never had.
//
// The problem it closes (filed July 17): Blackboard keys are observable — they
// fire change notifications that Observer Aborts hooks — but gameplay tags and
// cooldown GEs are not. So when the BT needed to react to ability state, its
// only tools were a decorator with a hand-copied number, or nothing. Result:
// tasks that "succeed" at abilities that never activated, and a boss that
// stands still because the Selector can't see that everything is on cooldown.
//
// This service runs on a branch (put it on the root composite) and writes,
// every Interval:
//   - one Bool per configured ability tag: "is this ability activatable right
//     now" — cooldown, cost, and blocked tags all honored, because the check
//     IS CanActivateAbility. The GE stays the single source of truth; the BB
//     bool is a projection of it, refreshed on a cadence.
//   - DistanceToTarget (Float), for literal-comparison decorators.
//   - bInAttackRange (Bool), sourced from the controller's own
//     IsTargetInAttackRange() so MeleeAttackRange stays tuned in ONE place.
//   - TargetLocation (Vector), refreshed every service tick instead of every
//     2s leash tick — un-stales any Move To bound to the location key.
//
// With these as Blackboard keys, decorators get Observer Aborts: the moment
// bChargeReady flips true mid-walk, the tree aborts the walk and charges.
// That is the difference between turn-based AI and a creature.
//
// Gotcha #2 note: every key on this node is an FBlackboardKeySelector, chosen
// from a dropdown of the asset's real keys — immune to FName-typo silent
// no-ops by construction. An unset selector is skipped, visibly "None" in the
// editor, and reported once at OnBecomeRelevant.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GameplayTagContainer.h"
#include "GothicBTService_CombatSync.generated.h"

class UAbilitySystemComponent;

/** One ability whose readiness should be mirrored into a Blackboard bool. */
USTRUCT()
struct FGothicAbilityReadinessSync
{
    GENERATED_BODY()

    /**
     * Matched against the ability's ASSET TAGS — the same field
     * TryActivateAbilitiesByTag reads, NOT this project's AbilityInputTag.
     * (That distinction already cost one debugging pass on July 17.)
     * e.g. Ability.Boss.BestialLucid.Charge
     */
    UPROPERTY(EditAnywhere, Category = "Sync")
    FGameplayTag AbilityTag;

    /** Bool key to write readiness into, e.g. bChargeReady. */
    UPROPERTY(EditAnywhere, Category = "Sync")
    FBlackboardKeySelector ReadyKey;
};

UCLASS(meta = (DisplayName = "Gothic Combat Sync"))
class GOTHICMMO_API UGothicBTService_CombatSync : public UBTService
{
    GENERATED_BODY()

public:
    UGothicBTService_CombatSync();

    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        float DeltaSeconds) override;
    virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual FString GetStaticDescription() const override;

protected:
    /** Abilities to mirror. One entry per ability the tree branches on. */
    UPROPERTY(EditAnywhere, Category = "Sync")
    TArray<FGothicAbilityReadinessSync> AbilitiesToSync;

    /** Object key holding the current target (normally TargetActor). */
    UPROPERTY(EditAnywhere, Category = "Sync")
    FBlackboardKeySelector TargetActorKey;

    /** Optional Float key — straight-line distance to the target (cm). */
    UPROPERTY(EditAnywhere, Category = "Sync")
    FBlackboardKeySelector DistanceToTargetKey;

    /**
     * Optional Bool key — the controller's IsTargetInAttackRange() verdict.
     * Prefer this over comparing DistanceToTarget against a literal in a
     * decorator: the range number stays on the controller where it's tuned.
     */
    UPROPERTY(EditAnywhere, Category = "Sync")
    FBlackboardKeySelector InAttackRangeKey;

    /**
     * Optional Vector key — target's live location (normally TargetLocation).
     * Refreshed here every service tick; CheckLeash's 2s refresh becomes the
     * fallback rather than the only writer.
     */
    UPROPERTY(EditAnywhere, Category = "Sync")
    FBlackboardKeySelector TargetLocationKey;

private:
    UAbilitySystemComponent* ResolveASC(UBehaviorTreeComponent& OwnerComp) const;
};