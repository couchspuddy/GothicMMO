// GothicBTService_WeightedActionSelect.h
// Replaces fixed Selector priority with weighted probabilistic choice.
//
// Why this exists: a strict Selector priority order (Charge > Claw > Roar,
// whichever's first that's ready wins) is, by construction, a thing a player
// can learn and solve — watch enough fights and "she always Charges over
// Clawing when both are up" stops being a guess. Reactive re-evaluation
// (GothicBTService_CombatSync + Observer Aborts) makes that flowchart fast,
// not unpredictable. This service replaces the flowchart with a weighted
// roll: every eligible action gets scored, one is picked probabilistically,
// and two identical situations stop resolving identically.
//
// Deliberately generic — no BestialLucid-specific logic lives here. The same
// node configures the Retained's small moveset and the Boss's larger,
// phase-gated one; what differs is the Actions array on each BT asset, not
// the C++ doing the picking. Shared foundation, per-character data.
//
// What this does NOT cover, on purpose:
//   - Wall Pound and Slam (Phase 2 Charge) are explicitly NOT weighted pool
//     members — both need a learnable, predictable cadence for the player to
//     plan around ("avoid the walls" only works as a strategy if the collapse
//     advances on a rhythm, not whenever scoring happens to favor it). Those
//     get their own fixed-cooldown BT branches sitting above this service's
//     output in Selector priority, same Observer-Abort pattern, so a due
//     cadence ability interrupts whatever this service picked.
//   - Cry is not a candidate in this array either — it's a probabilistic
//     substitution applied AFTER Roar is chosen, not a competing entry with
//     its own weight. Handled downstream of this service, not inside it.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GameplayTagContainer.h"
#include "GothicBTService_WeightedActionSelect.generated.h"

class UAbilitySystemComponent;

/** One candidate in the weighted pool. */
USTRUCT()
struct FGothicWeightedActionEntry
{
    GENERATED_BODY()

    /**
     * Written to the ChosenAction Blackboard key when this entry wins. Keep it
     * short and stable — BT decorators downstream do Name equality checks
     * against it (e.g. "ChosenAction == Approach").
     */
    UPROPERTY(EditAnywhere, Category = "Action")
    FName ActionID;

    /**
     * Matched against the ability's ASSET TAGS — the same field CombatSync and
     * GothicBTTask_ActivateAbilityByTag already read, NOT AbilityInputTag.
     * That mismatch has already cost one debugging session on this project;
     * check AssetTags first if an entry never seems to win.
     */
    UPROPERTY(EditAnywhere, Category = "Action")
    FGameplayTag AbilityTag;

    /**
     * Starting weight before modifiers. Relative to sibling entries only —
     * there's no fixed scale, tune these by comparing them to each other.
     */
    UPROPERTY(EditAnywhere, Category = "Action")
    float BaseWeight = 1.f;

    /**
     * Added to BaseWeight, scaled by (1 - HealthPercent) — fully applied at 0
     * health, zero effect at full health. This is the ONLY place "wounded and
     * reckless" lives; leave at 0 for characters whose trait is patience
     * rather than escalation (the Retained's whole kit, specifically — her
     * design is patience in choosing the moment, not getting more aggressive
     * as she's damaged. Don't default this to nonzero for her entries).
     */
    UPROPERTY(EditAnywhere, Category = "Action")
    float RecklessnessWeightBonus = 0.f;

    /** Optional range gate. -1 on either bound means no limit on that side. */
    UPROPERTY(EditAnywhere, Category = "Action")
    float MinRange = -1.f;

    UPROPERTY(EditAnywhere, Category = "Action")
    float MaxRange = -1.f;
};

UCLASS(meta = (DisplayName = "Gothic Weighted Action Select"))
class GOTHICMMO_API UGothicBTService_WeightedActionSelect : public UBTService
{
    GENERATED_BODY()

public:
    UGothicBTService_WeightedActionSelect();

    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        float DeltaSeconds) override;
    virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual FString GetStaticDescription() const override;
    virtual uint16 GetInstanceMemorySize() const override;

protected:
    /** The weighted pool for this character. Configure per BT asset. */
    UPROPERTY(EditAnywhere, Category = "Selection")
    TArray<FGothicWeightedActionEntry> Actions;

    /** Name key the winning ActionID is written to. Downstream Sequences branch on this via equality decorators. */
    UPROPERTY(EditAnywhere, Category = "Selection")
    FBlackboardKeySelector ChosenActionKey;

    /** Object key holding the current target — used for range gating. */
    UPROPERTY(EditAnywhere, Category = "Selection")
    FBlackboardKeySelector TargetActorKey;

    /**
     * Movement entries (no AbilityTag — Approach, Reposition) have no
     * IsActive() signal to hold a decision open with, unlike Claw/Charge/
     * Roar. Without this, they'd reroll every service tick and flicker
     * between each other instead of actually walking anywhere. Minimum time
     * to hold a movement decision before it's eligible to be replaced.
     */
    UPROPERTY(EditAnywhere, Category = "Selection")
    float MinMovementCommitDuration = 1.5f;

private:
    /**
     * Computes eligible entries' final scores, rolls a weighted pick, and
     * writes the winner. If total weight is 0 — nothing ready, nothing in
     * range — the key is left untouched rather than forced to a bad answer;
     * the tree's own fallback (Move To) handles that gracefully, same
     * honest-degradation principle as GothicBTTask_ActivateAbilityByTag.
     *
     * Gated at the top: won't overwrite ChosenAction while the current pick
     * is still genuinely running (ability-backed: IsActive() on the ASC;
     * movement-backed: MinMovementCommitDuration hasn't elapsed) — otherwise
     * every tick re-rolls and interrupts whatever just started.
     */
    void SelectAndWrite(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

    bool IsAbilityReady(UAbilitySystemComponent* ASC, const FGameplayTag& AbilityTag) const;

    /** Distinct from IsAbilityReady — this checks ONLY whether it's currently
     *  executing, regardless of cooldown state, which is what matters for
     *  "don't interrupt the thing that's already running." */
    bool IsAbilityActive(UAbilitySystemComponent* ASC, const FGameplayTag& AbilityTag) const;
};