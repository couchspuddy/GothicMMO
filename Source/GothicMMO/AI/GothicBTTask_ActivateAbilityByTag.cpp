// GothicBTTask_ActivateAbilityByTag.cpp

#include "AI/GothicBTTask_ActivateAbilityByTag.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AI/GothicEnemyAIController.h"     // GothicBBKeys::ChosenAction

// ----------------------------------------------------------------------------
// Instrumentation
//
// Every line this file emits starts with the same literal prefix so a single
// grep answers "did this branch run, and what happened?":
//
//     ActivateAbilityByTag[<Pawn>|<Tag>|ChosenAction=<BB value>]: <outcome>
//
// The July 30 Charge diagnosis burned most of a session because this task was
// silent on entry and silent on two of its five failure paths — so "no log
// line" was consistent with both "the branch never ran" and "the branch ran and
// was refused." Absence of a line now means exactly one thing: ExecuteTask was
// never reached.
//
// Category is LogVigilCombat, not LogTemp. LogTemp defaults to Log, so Verbose
// lines are dropped silently — which is the exact failure mode that made these
// paths invisible in the first place. LogVigilCombat ships at Verbose.
// ----------------------------------------------------------------------------
namespace
{
    /** The correlation stamp shared by every line below. Cheap enough to build
     *  unconditionally: ExecuteTask runs on branch entry, not per tick. */
    FString MakeTaskContext(const UBehaviorTreeComponent& OwnerComp,
        const APawn* Pawn, const FGameplayTag& AbilityTag)
    {
        const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
        const FName ChosenAction = BB
            ? BB->GetValueAsName(GothicBBKeys::ChosenAction)
            : NAME_None;

        return FString::Printf(TEXT("%s|%s|ChosenAction=%s"),
            *GetNameSafe(Pawn),
            AbilityTag.IsValid() ? *AbilityTag.ToString() : TEXT("(no tag set)"),
            ChosenAction.IsNone() ? TEXT("(none)") : *ChosenAction.ToString());
    }
}

UGothicBTTask_ActivateAbilityByTag::UGothicBTTask_ActivateAbilityByTag()
{
    NodeName = TEXT("Gothic Activate Ability By Tag");

    // Per-run state (WatchedHandle, delegate binding) lives on the node, so
    // each running tree needs its own instance.
    bCreateNodeInstance = true;
}

EBTNodeResult::Type UGothicBTTask_ActivateAbilityByTag::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    const AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* Pawn = AIC ? AIC->GetPawn() : nullptr;
    UAbilitySystemComponent* ASC =
        Pawn ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn) : nullptr;

    // FIRST statement with any output — before every early return, so this line
    // existing proves the branch was entered and the tag it was entered with.
    const FString Ctx = MakeTaskContext(OwnerComp, Pawn, AbilityTag);
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("ActivateAbilityByTag[%s]: ENTER node '%s' (waitForEnd=%d)"),
        *Ctx, *NodeName, bWaitForAbilityEnd ? 1 : 0);

    if (!ASC)
    {
        UE_LOG(LogVigilCombat, Error,
            TEXT("ActivateAbilityByTag[%s]: FAIL no-asc — pawn has no AbilitySystemComponent"),
            *Ctx);
        return EBTNodeResult::Failed;
    }

    if (!AbilityTag.IsValid())
    {
        UE_LOG(LogVigilCombat, Error,
            TEXT("ActivateAbilityByTag[%s]: FAIL no-tag — AbilityTag not set on node '%s'"),
            *Ctx, *NodeName);
        return EBTNodeResult::Failed;
    }

    // Find the spec ourselves rather than using TryActivateAbilitiesByTag —
    // we need the handle to (a) watch for this specific activation's end and
    // (b) cancel exactly this activation on abort, nothing else.
    const FGameplayAbilitySpec* FoundSpec = nullptr;
    for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
    {
        if (Spec.Ability && Spec.Ability->GetAssetTags().HasTag(AbilityTag))
        {
            FoundSpec = &Spec;
            break;
        }
    }

    if (!FoundSpec)
    {
        // Loud: this is the AssetTags-vs-AbilityInputTag misconfiguration
        // (July 17) presenting again. Without this line the symptom is a
        // silently skipped branch.
        UE_LOG(LogVigilCombat, Error,
            TEXT("ActivateAbilityByTag[%s]: FAIL no-spec — no granted ability carries this AssetTag; check AssetTags, not AbilityInputTag"),
            *Ctx);
        return EBTNodeResult::Failed;
    }

    if (FoundSpec->IsActive())
    {
        // Already mid-execution — don't stack activations; let the Selector
        // pick something else (usually Move To). Legitimate and common, so
        // Verbose: this is a tree-timing observation, not a defect.
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("ActivateAbilityByTag[%s]: FAIL already-active — spec is mid-execution, not re-activating"),
            *Ctx);
        return EBTNodeResult::Failed;
    }

    WatchedHandle = FoundSpec->Handle;
    bInExecuteTask = true;
    bEndedDuringActivation = false;
    bEndedWasCancelled = false;
    CachedASC = ASC;
    CachedOwnerComp = &OwnerComp;

    // Bind BEFORE activating: an ability that runs synchronously (instant
    // commit + EndAbility inside ActivateAbility) ends during the
    // TryActivateAbility call below, and we must not miss it.
    if (bWaitForAbilityEnd)
    {
        AbilityEndedDelegateHandle = ASC->OnAbilityEnded.AddUObject(
            this, &UGothicBTTask_ActivateAbilityByTag::HandleAbilityEnded);
    }

    const bool bActivated = ASC->TryActivateAbility(WatchedHandle);

    bInExecuteTask = false;

    if (!bActivated)
    {
        // Cooldown, cost, blocked tags — an honest Failed is the entire point
        // of this node. The Selector falls through to the next child. Honest to
        // the tree was never honest to a human, though: this path used to be
        // silent, so a refused activation and a branch that never ran looked
        // identical. Verbose, not Error — a cooldown refusal is normal play.
        // GAS logs the specific reason on LogAbilitySystem Verbose; this line
        // is the anchor that says the call was actually made.
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("ActivateAbilityByTag[%s]: FAIL activation-refused — TryActivateAbility returned false (cost/cooldown/blocked tags; see LogAbilitySystem)"),
            *Ctx);
        Cleanup();
        return EBTNodeResult::Failed;
    }

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("ActivateAbilityByTag[%s]: ACTIVATED"), *Ctx);

    if (!bWaitForAbilityEnd || bEndedDuringActivation)
    {
        const bool bCancelled = bEndedWasCancelled;
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("ActivateAbilityByTag[%s]: FINISH immediate — %s (endedDuringActivation=%d)"),
            *Ctx,
            bCancelled ? TEXT("Failed/cancelled") : TEXT("Succeeded"),
            bEndedDuringActivation ? 1 : 0);
        Cleanup();
        return bCancelled ? EBTNodeResult::Failed : EBTNodeResult::Succeeded;
    }

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("ActivateAbilityByTag[%s]: INPROGRESS — waiting for ability end"), *Ctx);

    return EBTNodeResult::InProgress;
}

void UGothicBTTask_ActivateAbilityByTag::HandleAbilityEnded(const FAbilityEndedData& EndedData)
{
    if (EndedData.AbilitySpecHandle != WatchedHandle)
    {
        return;
    }

    // Synchronous end: ExecuteTask is still on the stack (an instant ability
    // committed and ended inside TryActivateAbility). Record and let
    // ExecuteTask return the result itself — calling FinishLatentTask before
    // the task has actually gone latent corrupts the tree's task state.
    if (bInExecuteTask || !CachedOwnerComp.IsValid())
    {
        bEndedDuringActivation = true;
        bEndedWasCancelled = EndedData.bWasCancelled;
        return;
    }

    UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get();
    const bool bCancelled = EndedData.bWasCancelled;

    // The counterpart to the INPROGRESS line. A branch that logs INPROGRESS and
    // never logs this is the "never-ending swing" signature.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("ActivateAbilityByTag[%s|%s]: FINISH latent — %s"),
        *GetNameSafe(OwnerComp->GetAIOwner() ? OwnerComp->GetAIOwner()->GetPawn() : nullptr),
        AbilityTag.IsValid() ? *AbilityTag.ToString() : TEXT("(no tag set)"),
        bCancelled ? TEXT("Failed/cancelled") : TEXT("Succeeded"));

    Cleanup();

    // Cancelled (stunned mid-swing, phase interrupt) → Failed so the tree
    // re-plans rather than believing the attack landed.
    FinishLatentTask(*OwnerComp,
        bCancelled ? EBTNodeResult::Failed : EBTNodeResult::Succeeded);
}

EBTNodeResult::Type UGothicBTTask_ActivateAbilityByTag::AbortTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    if (bCancelAbilityOnAbort && CachedASC.IsValid() && WatchedHandle.IsValid())
    {
        CachedASC->CancelAbilityHandle(WatchedHandle);
    }

    Cleanup();
    return EBTNodeResult::Aborted;
}

void UGothicBTTask_ActivateAbilityByTag::Cleanup()
{
    if (CachedASC.IsValid() && AbilityEndedDelegateHandle.IsValid())
    {
        CachedASC->OnAbilityEnded.Remove(AbilityEndedDelegateHandle);
    }
    AbilityEndedDelegateHandle.Reset();
    WatchedHandle = FGameplayAbilitySpecHandle();
    CachedASC = nullptr;
    CachedOwnerComp = nullptr;
}

FString UGothicBTTask_ActivateAbilityByTag::GetStaticDescription() const
{
    return FString::Printf(TEXT("Activate: %s%s"),
        AbilityTag.IsValid() ? *AbilityTag.ToString() : TEXT("(no tag set)"),
        bWaitForAbilityEnd ? TEXT("\nwaits for ability end") : TEXT("\nfire and forget"));
}