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
    virtual FString GetStaticDescription() const override;

protected:
    /**
     * Matched against the ability's ASSET TAGS (the field
     * TryActivateAbilitiesByTag reads) — NOT AbilityInputTag, which is this
     * project's player-input plumbing and which a boss never uses.
     * e.g. Ability.Boss.BestialLucid.Claw
     */
    UPROPERTY(EditAnywhere, Category = "Ability")
    FGameplayTag AbilityTag;

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
};