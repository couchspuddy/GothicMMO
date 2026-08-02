// GothicBTTask_ActivateAbilityByTag.h
// Replaces the Blueprint "Try Activate Abilities by Tag → Finish Execute"
// tasks on the boss trees, which had two defects filed July 17:
//
//   1. They returned Succeeded whether or not the ability actually activated.
//      In a Selector, a task that always succeeds makes every sibling after it
//      unreachable — which is the leading hypothesis for "the boss doesn't
//      move unless she charges": Claw 'succeeds' at doing nothing, and the
//      Move To fallback below it never runs.
//   2. They finished the instant the ability activated rather than when it
//      ended, so the tree re-planned mid-swing.
//
// This task is honest in both directions:
//   - Activation fails (cooldown, cost, blocked tags, no matching ability)
//        → EBTNodeResult::Failed, and the Selector falls through to the next
//          option — which is what makes the Move To fallback reachable.
//   - Activation succeeds and bWaitForAbilityEnd
//        → InProgress until the ASC reports the ability ended, then Succeeded
//          (or Failed if it was cancelled — a stun mid-swing should re-plan).
//
// bCreateNodeInstance is true: this node holds per-run state (the watched
// spec handle, the delegate binding), so each BT component gets its own
// instance. Fine at boss counts; revisit if this ever runs on 200 trash mobs.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpecHandle.h"
#include "GothicBTTask_ActivateAbilityByTag.generated.h"

class UAbilitySystemComponent;
struct FAbilityEndedData;

UCLASS(meta = (DisplayName = "Gothic Activate Ability By Tag"))
class GOTHICMMO_API UGothicBTTask_ActivateAbilityByTag : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UGothicBTTask_ActivateAbilityByTag();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual FString GetStaticDescription() const override;

protected:
    /**
     * Matched against the ability's ASSET TAGS (the field
     * TryActivateAbilitiesByTag reads) — NOT AbilityInputTag, which is this
     * project's player-input plumbing and which a boss never uses.
     * e.g. Ability.Boss.BestialLucid.Claw
     *
     * The CONFIGURED source. Used when AbilityTagKey is unset or reads None,
     * which is every node on BT_EnemyCombat and BT_FeralRetained today.
     */
    UPROPERTY(EditAnywhere, Category = "Ability")
    FGameplayTag AbilityTag;

    // ── Blackboard-sourced dispatch (phase 1) ────────────────────────────────
    //
    // Why this exists. The old shape was one hand-authored branch per ability,
    // and each branch required SIX independent things to agree before it could
    // run: the pool entry's ActionID string, the decorator's string literal, the
    // readiness key's name, the CombatSync entry, the ability's asset tag, and
    // the entry's range band against the branch's range gate. Nothing in the
    // engine or this codebase checked any of the six. Measured on July 30-31:
    // Charge's band (550-2000) contradicted its gate (bInAttackRange <=160);
    // Claw's band was 450 against a 160 reach; Charge's gate was inverted;
    // Claw alone had flowAbortMode None; BT_EnemyCombat's pool was entirely
    // -1/-1 — and the live failure was a Charge picked correctly, held for
    // ~500ms, with five of six couplings verified right and the sixth wrong
    // invisibly.
    //
    // With the tag read from a key there is ONE branch and ONE coupling: the
    // key this task reads is the key the selector writes. There is nothing left
    // to desync.

    /**
     * Optional Name key holding the ability tag to activate, as a full tag name
     * (e.g. "Ability.Boss.BestialLucid.Claw").
     *
     * Overrides AbilityTag when set and non-None. When unset — or set but
     * currently None — the configured AbilityTag is used, so existing trees are
     * untouched and this is not a flag-day migration.
     *
     * A Name key and not a dedicated tag key because the Blackboard has no
     * gameplay-tag key type; the round trip through
     * FGameplayTag::RequestGameplayTag is exact for any registered tag, and an
     * unregistered one fails loudly here rather than silently activating
     * nothing.
     */
    UPROPERTY(EditAnywhere, Category = "Ability")
    FBlackboardKeySelector AbilityTagKey;

    /**
     * Optional Name key to CLEAR when this task stops running. Normally the
     * same key as AbilityTagKey — that is the commitment-release contract with
     * GothicBTService_WeightedActionSelect.
     *
     * The selector latches its attack pick into that key and then refuses to
     * re-score ANYTHING until the key is empty. Clearing it here is what makes
     * "the ability ended" and "the lock opened" the same event, observed rather
     * than estimated by a timer — which is the entire reason
     * AbilityActivationGrace is no longer needed on the split path.
     *
     * Cleared on every exit this task has, with one deliberate exception: the
     * already-active path. There the commitment is genuinely still running and
     * clearing would release the lock mid-swing; the selector's own
     * IsAbilityActive latch covers that case instead.
     *
     * Requires bWaitForAbilityEnd — with fire-and-forget the key would clear
     * the instant the ability started, and the selector would re-plan on top of
     * a running attack. Reported at Error if configured otherwise.
     */
    UPROPERTY(EditAnywhere, Category = "Ability")
    FBlackboardKeySelector ReleaseKey;

    /**
     * If true (default), the task stays InProgress until the ability ends —
     * the tree commits to the swing instead of re-planning mid-animation.
     * If false, returns Succeeded immediately after successful activation.
     */
    UPROPERTY(EditAnywhere, Category = "Ability")
    bool bWaitForAbilityEnd = true;

    /**
     * If true (default), aborting this task (Observer Aborts, phase change,
     * death) also cancels the running ability, so a higher-priority branch
     * doesn't fight an ability that's still executing underneath it.
     */
    UPROPERTY(EditAnywhere, Category = "Ability")
    bool bCancelAbilityOnAbort = true;

private:
    /** The BT component this run belongs to — needed to finish latently. */
    TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;

    /** The ASC we bound OnAbilityEnded on, for symmetric unbinding. */
    TWeakObjectPtr<UAbilitySystemComponent> CachedASC;

    /** The specific activation this run is waiting on. */
    FGameplayAbilitySpecHandle WatchedHandle;

    /**
     * The tag this RUN resolved to — from AbilityTagKey if it had one, else
     * AbilityTag. Safe as a member because bCreateNodeInstance is true, so each
     * BT component has its own copy of this node.
     */
    FGameplayTag ResolvedTag;

    FDelegateHandle AbilityEndedDelegateHandle;

    /** True only while ExecuteTask is on the stack — lets the ability-ended
     *  callback distinguish "ended synchronously during activation" from
     *  "ended later while latent," which need different finish paths. */
    bool bInExecuteTask = false;

    /** Set if the ability ended synchronously inside TryActivateAbility. */
    bool bEndedDuringActivation = false;
    bool bEndedWasCancelled = false;

    void HandleAbilityEnded(const FAbilityEndedData& EndedData);
    void Cleanup();

    /** AbilityTagKey's current value if it resolves to a registered tag, else AbilityTag. */
    FGameplayTag ResolveAbilityTag(UBehaviorTreeComponent& OwnerComp) const;

    /** Writes None to ReleaseKey — the commitment-release signal. No-op if unset. */
    void ReleaseCommitment(UBehaviorTreeComponent* OwnerComp) const;
};