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
#include "AI/GothicAttackCommitmentComponent.h"   // EGothicChainExitReason
#include "GothicBTService_WeightedActionSelect.generated.h"

class UAbilitySystemComponent;
class UBlackboardComponent;
class UGothicAttackCommitmentComponent;

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

// ── Attack chains (phase 3) ──────────────────────────────────────────────────
//
// Phase 2 gave a lock around ONE ability. A chain makes that lock span a SERIES:
// a three-hit combo is one commitment with three dispatches, so the movement
// layer cannot wedge itself between the hits. "Once in those events it's locked
// to those" — full commitment, no bail-outs, and the player is rewarded for
// learning the sequence because the sequence is a real object rather than three
// independent rolls that happened to land in a row.
//
// Every step dispatches through the SAME GothicBTTask_ActivateAbilityByTag node
// reading the same Blackboard key. There are no per-step BT branches: advancing
// writes the next step's tag into the key WITHOUT opening the lock, so the tree
// is byte-identical whether it is running a single attack or the middle of a
// combo. The dispatch task never learns that chains exist.

/**
 * One conditional exit from a step. First branch whose gate passes wins; a step
 * with no branches simply falls through to the next step in the array.
 *
 * This is where the Destiny-style positional intelligence lives: the same opener
 * continues into a different follow-up depending on where the player ended up,
 * which is a decision made once, at the moment the step ends, and then committed
 * to like everything else.
 */
USTRUCT()
struct FGothicAttackStepBranch
{
    GENERATED_BODY()

    /**
     * Horizontal (XY) range gate from pawn to target, same semantics and same
     * reason as FGothicWeightedActionEntry's — capsule half-heights must never
     * move a range band. -1 means no limit on that side.
     */
    UPROPERTY(EditAnywhere, Category = "Branch")
    float MinRange = -1.f;

    UPROPERTY(EditAnywhere, Category = "Branch")
    float MaxRange = -1.f;

    /**
     * Require the destination step's ability to be off cooldown and activatable.
     * On by default: a branch into an ability that cannot fire would dispatch a
     * step that immediately clears the key, which reads as a chain stuttering
     * rather than as the cooldown it actually is.
     */
    UPROPERTY(EditAnywhere, Category = "Branch")
    bool bRequireAbilityReady = true;

    /**
     * Index into the chain's Steps array. INDEX_NONE (-1) ends the chain here —
     * an authored early finish, which is the difference between "the combo has a
     * short version" and a bail-out.
     */
    UPROPERTY(EditAnywhere, Category = "Branch")
    int32 NextStep = -1;
};

/** One dispatch in a chain. */
USTRUCT()
struct FGothicAttackStep
{
    GENERATED_BODY()

    /**
     * Matched against the ability's ASSET TAGS — the same field the pool
     * entries, CombatSync and the dispatch task all read, NOT AbilityInputTag.
     */
    UPROPERTY(EditAnywhere, Category = "Step")
    FGameplayTag AbilityTag;

    /**
     * Seconds this step's end may go unobserved before the chain is abandoned.
     * <= 0 uses the service's ChainRecoveryWindow.
     *
     * NOT a bail-out window and not a combo timer the player can beat. It
     * measures how long the BEHAVIOR TREE was absent, which is normally one
     * service tick: the step ends, the branch unwinds, the subtree is re-entered
     * and the next tick advances. It only ever expires when something took the
     * subtree away — a stagger, a phase transition — and resuming a combo into a
     * fight that has moved on is not commitment, it is a stale decision.
     */
    UPROPERTY(EditAnywhere, Category = "Step")
    float RecoveryWindow = -1.f;

    /** Evaluated in order; first passing branch wins. Empty = next step in the array. */
    UPROPERTY(EditAnywhere, Category = "Step")
    TArray<FGothicAttackStepBranch> Branches;
};

/**
 * A named series of attacks, entered as one decision.
 *
 * Chains do NOT compete in the attack pool as extra entries — that would change
 * the odds of every single attack the moment one was authored. A chain hangs off
 * the pool entries named in EntryTags: the normal weighted roll picks a winner
 * exactly as it does today, and only then does an eligible chain get the chance
 * to convert that win into a series. So an empty Chains array is not merely
 * default-off, it is algebraically inert.
 */
USTRUCT()
struct FGothicAttackChain
{
    GENERATED_BODY()

    /** Identity in the CHAIN-enter/advance/exit timeline lines. Keep it short and stable. */
    UPROPERTY(EditAnywhere, Category = "Chain")
    FName ChainID;

    /**
     * Ability tags of the pool entries that can open this chain. EMPTY MEANS
     * NEVER, deliberately — an empty container attaching to every attack would
     * make a half-authored chain fire on abilities nobody aimed it at.
     */
    UPROPERTY(EditAnywhere, Category = "Chain")
    FGameplayTagContainer EntryTags;

    /**
     * Horizontal range band the chain may be ENTERED in, over and above the
     * entry's own band. -1 on either bound means no limit on that side.
     */
    UPROPERTY(EditAnywhere, Category = "Chain")
    float EntryMinRange = -1.f;

    UPROPERTY(EditAnywhere, Category = "Chain")
    float EntryMaxRange = -1.f;

    /**
     * Weight of taking this chain, relative to a FIXED weight of 1.0 for "just
     * do the single attack that was picked". So 1.0 means the chain fires half
     * the time it is eligible, 3.0 means three quarters. Relative to the other
     * eligible chains as well, in the same roll.
     *
     * Expressed against a fixed 1.0 rather than as a probability because it
     * composes: N eligible chains and the single attack are one weighted roll
     * over N+1 options, with no normalisation for a designer to keep in their
     * head.
     */
    UPROPERTY(EditAnywhere, Category = "Chain")
    float Weight = 1.f;

    /** Dispatched in order unless a step's Branches say otherwise. */
    UPROPERTY(EditAnywhere, Category = "Chain")
    TArray<FGothicAttackStep> Steps;
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
     *
     * OBSOLETE UNDER bSplitAttackPool, and not consulted there. It exists to
     * protect an ability pick from the MOVEMENT pool, and once the two pools
     * write different keys there is nothing to protect it from — the attack key
     * is latched until the dispatch task releases it, which is the thing this
     * window was approximating with a stopwatch. Retained, unchanged, because
     * BT_EnemyCombat and BT_FeralRetained still run the combined path and
     * removing it would change their behaviour today.
     */
    UPROPERTY(EditAnywhere, Category = "Selection")
    float AbilityActivationGrace = 0.5f;

    // ── Attack layer (phase 2) ───────────────────────────────────────────────
    //
    // Attacks and movement are different KINDS of decision and were never
    // really one pool. Movement wants to be responsive — reconsidered as the
    // fight moves. An attack wants to be COMMITTED: chosen once, run to the
    // end, learnable by the player. Sharing one pool and one output key meant
    // every mechanism that protects one of them had to be smuggled past the
    // other, which is what MinMovementCommitDuration, AbilityActivationGrace
    // and the HOLD-* phases all are.
    //
    // With bSplitAttackPool on, the same authored Actions array is scored as
    // TWO pools — entries with an AbilityTag are the attack layer, entries
    // without are the positioning layer — and each writes its own Blackboard
    // key. The attack write is a LOCK, not a hint: see AttackDispatchTimeout.
    //
    // Off by default, deliberately. BT_EnemyCombat (Thralls) and
    // BT_FeralRetained run the legacy combined path byte-for-byte until their
    // trees are hand-migrated. This is not a flag-day change.

    /**
     * Score movement and attacks as separate layers and write the attack pick
     * to ChosenAttackTagKey instead of ChosenActionKey.
     *
     * Requires ChosenAttackTagKey to be set and the tree to have a dispatch
     * branch gated on it. Turning this on without the BT-side work leaves the
     * pawn with a movement-only pool — it will position and never attack.
     */
    UPROPERTY(EditAnywhere, Category = "Selection|Attack Layer")
    bool bSplitAttackPool = false;

    /**
     * Name key the winning attack's ABILITY TAG (full tag name, e.g.
     * "Ability.Boss.BestialLucid.Claw") is written to — not its ActionID.
     *
     * The tag rather than the ID because this key IS the dispatch instruction:
     * one BT task reads it and activates that ability, replacing the N
     * hand-authored per-ability branches whose six independent couplings were
     * the entire bug family this phase exists to delete.
     *
     * It is also the commitment latch. Set = an attack is committed; None = the
     * attack layer is open. GothicBTTask_ActivateAbilityByTag clears it on
     * every exit path (its ReleaseKey), so "the ability ended" and "the lock
     * released" are the same event by construction rather than by two systems
     * agreeing.
     */
    UPROPERTY(EditAnywhere, Category = "Selection|Attack Layer")
    FBlackboardKeySelector ChosenAttackTagKey;

    /**
     * Multiplies the attack layer's TOTAL weight when deciding which layer wins
     * the tick. 1.0 (default) reproduces today's distribution exactly: with one
     * combined roll, an entry's chance is its score over the grand total, and
     * rolling for the layer first at P(attack) = AttackTotal / (AttackTotal +
     * MovementTotal) and then within the layer gives every entry the same
     * probability it had before. Nothing about the split changes the odds.
     *
     * Above 1.0 biases towards committing rather than repositioning WITHOUT
     * re-authoring every entry's BaseWeight — the one knob for "she should
     * press more" that does not require touching the per-entry tuning.
     *
     * Left at 1.0 on purpose: the balance point is a measured design decision,
     * and the ratchet-towards-movement this phase fixes was a LOCK problem, not
     * a weight problem. Tune it after measuring the split, not before.
     */
    UPROPERTY(EditAnywhere, Category = "Selection|Attack Layer")
    float AttackLayerWeightScale = 1.f;

    /**
     * Deadlock breaker, NOT a commitment window. Seconds an attack may sit
     * committed on the Blackboard without the ability ever going active.
     *
     * The distinction from AbilityActivationGrace matters. Grace was a window
     * during which a pick was PROTECTED, after which it became reroll-eligible
     * — so the protection was the scarce thing and running out of it was
     * normal. Here the protection is unconditional and total: once committed,
     * neither pool is re-scored until the key clears. This timeout only fires
     * when the ability was never dispatched AT ALL, which cannot happen in a
     * correctly-wired tree and therefore logs as an Error rather than passing
     * silently. Once the ability has gone active even once, the timeout is
     * disarmed permanently for that commitment — a long swing, a chain, or a
     * charge windup is never cut short by it. Full commitment is the point.
     *
     * 1.5s: the tree needs at least two service ticks (worst case 0.25s apart)
     * to abort the running branch and reach the dispatch task, plus room for a
     * montage-driven ability whose activation is gated behind a notify. Six
     * ticks of slack over that.
     */
    UPROPERTY(EditAnywhere, Category = "Selection|Attack Layer")
    float AttackDispatchTimeout = 1.5f;

    /**
     * Attack chains available to this character. EMPTY BY DEFAULT, and an empty
     * array reproduces the phase-2 behaviour exactly — nothing regresses until a
     * tree opts in by authoring one.
     *
     * Designer-facing and per-BT-asset, like Actions: the same C++ serves the
     * Retained's small moveset and the boss's phase-gated one, and what differs
     * is the data on the asset.
     */
    UPROPERTY(EditAnywhere, Category = "Selection|Attack Chains")
    TArray<FGothicAttackChain> Chains;

    /**
     * Default for a step whose RecoveryWindow is <= 0. See FGothicAttackStep —
     * this measures the behavior tree's absence between two steps, not the
     * player's reaction time.
     *
     * 1.0s: the observed path is one service tick (worst case 0.25s) for the
     * branch to unwind and be re-entered, so this is four ticks of slack. Large
     * enough that a normal advance never trips it, small enough that a stagger
     * or a phase transition — the events that legitimately end a chain — always
     * do.
     */
    UPROPERTY(EditAnywhere, Category = "Selection|Attack Chains")
    float ChainRecoveryWindow = 1.f;

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
     * The split path (bSplitAttackPool). Two layers, two output keys, one lock.
     *
     * Called from SelectAndWrite with everything already resolved, so both
     * paths read the world exactly once and in the same way. LogTimeline is the
     * caller's dedupe-aware emitter, passed by reference so the split lines land
     * in the same VigilTimeline stream as the legacy ones.
     */
    void SelectSplit(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        UBlackboardComponent* BB, APawn* Pawn, UAbilitySystemComponent* ASC,
        AActor* Target, float DistanceToTarget, float RecklessFactor,
        float AggressionFactor, float EffectiveCommitDuration, float Now,
        TFunctionRef<void(const TCHAR*, const FString&, bool)> LogTimeline);

    /** Which half of the authored Actions array a scoring pass looks at. */
    enum class EPoolFilter : uint8
    {
        All,        // legacy combined path — every entry
        Movement,   // entries with no AbilityTag
        Attack      // entries with an AbilityTag
    };

    /**
     * Scores the entries matching Filter and returns their total weight.
     * OutIndices/OutScores are parallel and index back into Actions.
     * OutGateParts always covers EVERY entry in authored order regardless of
     * Filter, so the diagnostic line is identical in both paths.
     */
    float ScoreEntries(UAbilitySystemComponent* ASC, float DistanceToTarget,
        float RecklessFactor, float AggressionFactor, EPoolFilter Filter,
        TArray<int32>& OutIndices, TArray<float>& OutScores,
        TArray<FString>& OutGateParts) const;

    /**
     * Weighted roll over a scored pool. Returns an index INTO Actions, or
     * INDEX_NONE if the pool is empty.
     *
     * bOutWasTail reports the last-eligible fallthrough — see the comment at
     * the call site: TotalWeight is accumulated by repeated float addition
     * while the roll is drawn against that sum, so the final remainder can sit
     * a few ULPs above the last score purely from rounding, and exiting without
     * a pick would freeze a sticky key.
     */
    int32 RollWeighted(const TArray<int32>& Indices, const TArray<float>& Scores,
        float TotalWeight, float& OutRoll, float& OutRemainder, bool& bOutWasTail) const;

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

    /**
     * True when ChosenAttackTagKey resolved to the same blackboard key ID as
     * ChosenActionKey. Resolved once per asset in InitializeFromAsset; the
     * split path degrades to movement-only while it is set rather than writing
     * ability tag names into the movement key.
     *
     * Not a UPROPERTY: it is a fact about the resolved blackboard, recomputed
     * every load, and must never survive into a saved asset.
     */
    bool bAttackLayerAliased = false;

    // ── Chain helpers (phase 3) ──────────────────────────────────────────────

    /**
     * Which chain, if any, the winning attack entry opens. Returns INDEX_NONE
     * for "run it as a single attack", which is both the default and the answer
     * whenever Chains is empty — so the no-chains case costs one array-empty
     * check and cannot perturb the roll that already happened.
     *
     * The roll here is over the eligible chains PLUS a fixed weight of 1.0 for
     * the single attack; see FGothicAttackChain::Weight.
     */
    int32 PickChainForEntry(const FGothicWeightedActionEntry& Entry, float DistanceToTarget) const;

    /**
     * Advances Commit to the next step and writes its tag to the attack key
     * WITHOUT clearing the commitment — the lock never opens between steps.
     *
     * Returns false when the chain is over, with OutReason naming why:
     * Completed for running out of steps or an authored NextStep of -1, BadStep
     * for a step whose tag is not registered.
     */
    bool TryAdvanceChain(UBlackboardComponent* BB, FBlackboard::FKey AttackKeyID,
        UGothicAttackCommitmentComponent* Commit, UAbilitySystemComponent* ASC,
        float DistanceToTarget, float Now, EGothicChainExitReason& OutReason,
        TFunctionRef<void(const TCHAR*, const FString&, bool)> LogTimeline) const;

    /** A step's RecoveryWindow, or ChainRecoveryWindow when it is unset. */
    float GetRecoveryWindow(int32 ChainIndex, int32 StepIndex) const;

    /** ChainID for the timeline lines, or "none" — never an empty field. */
    FString DescribeChain(int32 ChainIndex) const;

    bool IsAbilityReady(UAbilitySystemComponent* ASC, const FGameplayTag& AbilityTag) const;

    /** Distinct from IsAbilityReady — this checks ONLY whether it's currently
     *  executing, regardless of cooldown state, which is what matters for
     *  "don't interrupt the thing that's already running." */
    bool IsAbilityActive(UAbilitySystemComponent* ASC, const FGameplayTag& AbilityTag) const;
};