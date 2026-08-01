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

    /**
     * Added to BaseWeight, scaled by (ArenaAggression - 1) — the Rotunda pillar
     * escalation. Zero effect at the baseline multiplier of 1.0 (four pillars
     * standing, or no arena manager in the level at all), fully applied at 2.0
     * (every pillar down).
     *
     * This must be authored ASYMMETRICALLY or it does nothing. The roll is
     * FRandRange(0, TotalWeight) against cumulative scores, so multiplying
     * EVERY entry by the same factor scales TotalWeight by the same factor and
     * leaves the distribution bit-for-bit identical. Aggression only becomes
     * visible as a BIAS: entries that should ramp when the arena is stripped
     * (the committing attacks) get a bonus, entries that should recede
     * (Reposition, the menace-hold) get zero or a negative value.
     *
     * Ships at 0 on every entry — the mechanism is wired, the CURVE is an
     * unmade design decision and wants a measured pass, not a guess. Until
     * values are authored the boss behaves exactly as she does today.
     */
    UPROPERTY(EditAnywhere, Category = "Action")
    float AggressionWeightBonus = 0.f;

    /**
     * Optional range gate, in HORIZONTAL (XY) cm from pawn to target.
     * -1 on either bound means no limit on that side.
     *
     * Horizontal so that a creature's capsule half-height can never move its
     * range bands: actor locations are capsule centres, so pawns in contact on
     * one floor still read a non-zero 3D separation.
     */
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

    /**
     * Divide MinMovementCommitDuration by the current arena aggression multiplier.
     *
     * This is the second, more direct half of pillar aggression. The commit
     * window is one of the two knobs that produced the measured orbiting: a
     * movement pick holds the Blackboard key for a flat 1.5s no matter what the
     * fight looks like, so the floor on "how long she walks instead of swinging"
     * is fixed. Dividing by the multiplier drops that floor to 0.75s with every
     * pillar down, which is the multiplier used as itself rather than as an
     * invented curve — and it is exactly inert at 1.0, so nothing changes for
     * the Retained or the Thralls, whose levels have no arena manager.
     *
     * Deliberately does NOT touch AbilityActivationGrace: that window is sized
     * from this service's own tick rate (2 x 0.25s worst-case spacing) and is a
     * measurement of the behavior tree, not a tuning value. Shrinking it would
     * re-open the ratchet-towards-movement bug it exists to close.
     */
    UPROPERTY(EditAnywhere, Category = "Selection")
    bool bScaleCommitDurationByAggression = true;

    /**
     * Grace window an ability pick gets between being WRITTEN to the Blackboard
     * and actually going active on the ASC.
     *
     * Without this the pool ratchets towards movement. A movement pick is
     * protected for MinMovementCommitDuration the instant it is written; an
     * ability pick was protected only by IsAbilityActive(), which is still
     * false on the tick after selection because the tree has not yet aborted
     * the running branch and reached ActivateAbilityByTag. So every ability
     * pick was eligible to be overwritten one service tick after it was made,
     * and roughly half the time it was overwritten by a movement entry that
     * then locked the key for seconds. Measured: the Bestial Lucid's Claw and
     * Reposition carry the SAME BaseWeight (2.5/2.5) and the Thrall's Strike
     * outweighs its Reposition 10:4, yet both read "Reposition" on every
     * blackboard sample of a 96s fight with the ability off cooldown.
     *
     * Sized from the service's own tick rate, not from feel: ticks land every
     * Interval +/- RandomDeviation, so the worst case spacing between two
     * ticks is 0.25s, and a behavior tree needs at least two of them to abort
     * the running branch and execute the newly-selected task. 2 x 0.25 = 0.5s.
     *
     * It is deliberately a window and not a latch: if the ability never goes
     * active (blocked by tags, failed activation), the window lapses and the
     * pool rerolls normally rather than wedging on a pick that never runs.
     */
    UPROPERTY(EditAnywhere, Category = "Selection")
    float AbilityActivationGrace = 0.5f;

    /**
     * Delay before the one-shot AssetTag validation runs. Matches CombatSync.
     * Must stay above zero: validating on the frame the branch becomes relevant
     * reads the pawn before BeginPlay has granted its StartupAbilities, and
     * every entry reports as missing.
     */
    UPROPERTY(EditAnywhere, Category = "Selection")
    float ValidationGraceSeconds = 1.f;

    /**
     * What to fall back to when NOTHING in the pool is eligible.
     *
     * Must name a MOVEMENT entry — one with no AbilityTag. An ability-backed
     * fallback would be a lie by construction: the only reason we are here is
     * that every ability failed its cooldown or range gate, so naming one would
     * re-assert the thing we just measured to be false. Entries with an
     * AbilityTag are ignored by the lookup for exactly that reason.
     *
     * If no matching entry exists, ChosenAction is CLEARED instead, which fails
     * every downstream equality decorator and hands control to the tree's own
     * Selector fallback.
     */
    UPROPERTY(EditAnywhere, Category = "Selection")
    FName FallbackActionID = FName("Approach");

    /** Runs once per branch activation, ValidationGraceSeconds after it becomes relevant. */
    void ValidateConfiguration(UBehaviorTreeComponent& OwnerComp) const;

private:
    /**
     * Computes eligible entries' final scores, rolls a weighted pick, and
     * writes the winner. If total weight is 0 — nothing ready, nothing in
     * range — it writes FallbackActionID, or clears the key if that entry
     * doesn't exist. It never leaves the key alone: a Blackboard Name key is
     * sticky, so "leave it untouched" means "run the previous pick forever."
     *
     * Gated at the top: won't overwrite ChosenAction while the current pick
     * is still genuinely running (ability-backed: IsActive() on the ASC;
     * movement-backed: MinMovementCommitDuration hasn't elapsed) — otherwise
     * every tick re-rolls and interrupts whatever just started.
     */
    void SelectAndWrite(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

    /**
     * Current Rotunda aggression multiplier, or 1.0 when this pawn's level has
     * no arena manager (every non-boss encounter). Resolved once per branch
     * activation into node memory and polled from there.
     *
     * Polling rather than binding OnArenaAggressionChanged is not laziness. BT
     * nodes are shared objects: one UGothicBTService_WeightedActionSelect
     * instance serves every pawn running that tree, and this node does not set
     * bCreateNodeInstance. A dynamic delegate bound from it would be bound once
     * per BT component onto the same UObject, so the multicast would fire the
     * same handler N times with no way to tell which pawn it was for, and any
     * state it wrote would be shared across all of them. Per-instance state on
     * a shared node has to live in NodeMemory, and NodeMemory is only reachable
     * from the node's own callbacks — which means polling.
     *
     * The read itself is cheap (a pillar count and an array index) and this
     * service already ticks at 5Hz. The one-time actor lookup is cached, misses
     * included, so a level with no manager pays for exactly one search.
     */
    float GetArenaAggression(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

    bool IsAbilityReady(UAbilitySystemComponent* ASC, const FGameplayTag& AbilityTag) const;

    /** Distinct from IsAbilityReady — this checks ONLY whether it's currently
     *  executing, regardless of cooldown state, which is what matters for
     *  "don't interrupt the thing that's already running." */
    bool IsAbilityActive(UAbilitySystemComponent* ASC, const FGameplayTag& AbilityTag) const;
};