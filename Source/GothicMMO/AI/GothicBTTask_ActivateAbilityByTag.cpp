// GothicBTTask_ActivateAbilityByTag.cpp

#include "AI/GothicBTTask_ActivateAbilityByTag.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"

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

    if (!ASC)
    {
        UE_LOG(LogTemp, Error, TEXT("ActivateAbilityByTag[%s]: no ASC on pawn"),
            *GetNameSafe(Pawn));
        return EBTNodeResult::Failed;
    }

    if (!AbilityTag.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("ActivateAbilityByTag[%s]: AbilityTag not set on node '%s'"),
            *GetNameSafe(Pawn), *NodeName);
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
        UE_LOG(LogTemp, Error,
            TEXT("ActivateAbilityByTag[%s]: no granted ability carries AssetTag '%s' — check AssetTags, not AbilityInputTag"),
            *GetNameSafe(Pawn), *AbilityTag.ToString());
        return EBTNodeResult::Failed;
    }

    if (FoundSpec->IsActive())
    {
        // Already mid-execution — don't stack activations; let the Selector
        // pick something else (usually Move To).
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
        // of this node. The Selector falls through to the next child.
        Cleanup();
        return EBTNodeResult::Failed;
    }

    if (!bWaitForAbilityEnd || bEndedDuringActivation)
    {
        const bool bCancelled = bEndedWasCancelled;
        Cleanup();
        return bCancelled ? EBTNodeResult::Failed : EBTNodeResult::Succeeded;
    }

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