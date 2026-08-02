// GothicBTTask_ActivateAbilityByTag.cpp

#include "AI/GothicBTTask_ActivateAbilityByTag.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "BehaviorTree/BehaviorTree.h"      // Asset.GetName() in InitializeFromAsset
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
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

    // Per-run state (WatchedHandle, delegate binding, ResolvedTag) lives on the
    // node, so each running tree needs its own instance.
    bCreateNodeInstance = true;

    // Both keys are OPTIONAL, and saying so is not cosmetic. A selector left at
    // None is auto-bound by FBlackboardKeySelector::ResolveSelectedKey to the
    // FIRST blackboard key matching its type filter unless bNoneIsAllowedValue
    // is set — so without these calls, every node a designer never touched came
    // up silently bound to the first Name key in the tree's blackboard
    // (ChosenAction, on every tree in this project). Measured on BT_BestialLucid:
    // three untouched nodes read back AbilityTagKey=ChosenAction and
    // ReleaseKey=ChosenAction, which made them log a spurious tag-resolution
    // Error every activation and CLEAR a key they do not own.
    //
    // It also makes "None" selectable in the editor dropdown, which is the only
    // way to clear a node that has already been serialized with a bound key.
    AbilityTagKey.AddNameFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTTask_ActivateAbilityByTag, AbilityTagKey));
    AbilityTagKey.AllowNoneAsValue(true);

    ReleaseKey.AddNameFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTTask_ActivateAbilityByTag, ReleaseKey));
    ReleaseKey.AllowNoneAsValue(true);
}

void UGothicBTTask_ActivateAbilityByTag::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);

    if (UBlackboardData* BBAsset = GetBlackboardAsset())
    {
        AbilityTagKey.ResolveSelectedKey(*BBAsset);
        ReleaseKey.ResolveSelectedKey(*BBAsset);
    }

    // Key dispatch and a literal tag are alternatives, never a pair: the key
    // wins at runtime whenever it holds a value, so one of the two is dead
    // config and which one is dead is not visible from the graph. This is also
    // the exact fingerprint the auto-binding bug above left behind — a node
    // authored with a literal AbilityTag that came back with AbilityTagKey
    // pointing at ChosenAction. Warning rather than Error because a hand-built
    // node with a deliberate fallback tag still runs correctly; it is just
    // config nobody can read.
    if (!AbilityTagKey.IsNone() && AbilityTag.IsValid())
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("ActivateAbilityByTag[%s|%s]: both AbilityTagKey ('%s') and a literal AbilityTag ('%s') are set. "
                 "The key wins whenever it holds a value, so one of these is dead config. If this node is meant to "
                 "activate its literal tag, set AbilityTagKey (and ReleaseKey) back to None."),
            *Asset.GetName(), *GetNodeName(),
            *AbilityTagKey.SelectedKeyName.ToString(), *AbilityTag.ToString());
    }
}

FGameplayTag UGothicBTTask_ActivateAbilityByTag::ResolveAbilityTag(
    UBehaviorTreeComponent& OwnerComp) const
{
    if (AbilityTagKey.IsNone())
    {
        return AbilityTag;
    }

    const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!BB)
    {
        return AbilityTag;
    }

    const FName TagName = BB->GetValue<UBlackboardKeyType_Name>(AbilityTagKey.GetSelectedKeyID());
    if (TagName.IsNone())
    {
        // An empty key is not an error: it is the normal reading when this node
        // is configured for key dispatch but the branch was entered by some
        // other route. Fall back to the configured tag, which is None on a
        // pure-dispatch node and produces the existing FAIL no-tag line.
        return AbilityTag;
    }

    const FGameplayTag Resolved = FGameplayTag::RequestGameplayTag(TagName, /*ErrorIfNotFound=*/false);
    if (!Resolved.IsValid())
    {
        UE_LOG(LogVigilCombat, Error,
            TEXT("ActivateAbilityByTag: Blackboard key '%s' holds '%s', which is not a registered gameplay tag. "
                 "The selector writes FGameplayTag::GetTagName() — a mismatch here means the tag was renamed "
                 "or the key was hand-set."),
            *AbilityTagKey.SelectedKeyName.ToString(), *TagName.ToString());
        return AbilityTag;
    }

    return Resolved;
}

void UGothicBTTask_ActivateAbilityByTag::ReleaseCommitment(UBehaviorTreeComponent* OwnerComp) const
{
    if (ReleaseKey.IsNone() || !OwnerComp)
    {
        return;
    }

    if (UBlackboardComponent* BB = OwnerComp->GetBlackboardComponent())
    {
        BB->SetValue<UBlackboardKeyType_Name>(ReleaseKey.GetSelectedKeyID(), NAME_None);
    }
}

EBTNodeResult::Type UGothicBTTask_ActivateAbilityByTag::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    const AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* Pawn = AIC ? AIC->GetPawn() : nullptr;
    UAbilitySystemComponent* ASC =
        Pawn ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn) : nullptr;

    // Resolve BEFORE the first log line, so every line below names the tag this
    // run actually dispatched rather than the configured default.
    ResolvedTag = ResolveAbilityTag(OwnerComp);

    // FIRST statement with any output — before every early return, so this line
    // existing proves the branch was entered and the tag it was entered with.
    const FString Ctx = MakeTaskContext(OwnerComp, Pawn, ResolvedTag);
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("ActivateAbilityByTag[%s]: ENTER node '%s' (waitForEnd=%d, source=%s)"),
        *Ctx, *NodeName, bWaitForAbilityEnd ? 1 : 0,
        AbilityTagKey.IsNone() ? TEXT("configured") : TEXT("blackboard"));

    // A release key with fire-and-forget clears the lock the instant the ability
    // starts, so the selector re-plans on top of a running attack — precisely
    // the mid-swing re-plan this task was written to stop. Loud rather than
    // silently corrected: the correct fix is a config change, not a guess here.
    if (!ReleaseKey.IsNone() && !bWaitForAbilityEnd)
    {
        UE_LOG(LogVigilCombat, Error,
            TEXT("ActivateAbilityByTag[%s]: ReleaseKey is set but bWaitForAbilityEnd is false — "
                 "the commitment lock will open the moment the ability starts. Set bWaitForAbilityEnd."),
            *Ctx);
    }

    if (!ASC)
    {
        UE_LOG(LogVigilCombat, Error,
            TEXT("ActivateAbilityByTag[%s]: FAIL no-asc — pawn has no AbilitySystemComponent"),
            *Ctx);
        ReleaseCommitment(&OwnerComp);
        return EBTNodeResult::Failed;
    }

    if (!ResolvedTag.IsValid())
    {
        UE_LOG(LogVigilCombat, Error,
            TEXT("ActivateAbilityByTag[%s]: FAIL no-tag — neither AbilityTagKey nor AbilityTag "
                 "yielded a tag on node '%s'"),
            *Ctx, *NodeName);
        ReleaseCommitment(&OwnerComp);
        return EBTNodeResult::Failed;
    }

    // Find the spec ourselves rather than using TryActivateAbilitiesByTag —
    // we need the handle to (a) watch for this specific activation's end and
    // (b) cancel exactly this activation on abort, nothing else.
    const FGameplayAbilitySpec* FoundSpec = nullptr;
    for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
    {
        if (Spec.Ability && Spec.Ability->GetAssetTags().HasTag(ResolvedTag))
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
        ReleaseCommitment(&OwnerComp);
        return EBTNodeResult::Failed;
    }

    if (FoundSpec->IsActive())
    {
        // Already mid-execution — don't stack activations; let the Selector
        // pick something else (usually Move To). Legitimate and common, so
        // Verbose: this is a tree-timing observation, not a defect.
        //
        // DELIBERATELY does not release. The commitment this Failed belongs to
        // is genuinely still running; clearing the key here would open the lock
        // mid-swing and let the movement layer overwrite an attack that is
        // actively hitting the player. The selector's own dispatched-latch
        // covers this case, and the real end of the ability arrives on the
        // OnAbilityEnded binding of whichever run owns it.
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
        ReleaseCommitment(&OwnerComp);
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
        ReleaseCommitment(&OwnerComp);
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
        ResolvedTag.IsValid() ? *ResolvedTag.ToString() : TEXT("(no tag set)"),
        bCancelled ? TEXT("Failed/cancelled") : TEXT("Succeeded"));

    Cleanup();

    // The commitment release, on the one event that actually means the attack
    // is over. Before FinishLatentTask, so the selector's next tick already
    // sees an open lock however fast the tree re-plans.
    ReleaseCommitment(OwnerComp);

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

    // Hard interrupt — stagger, phase change, death, leash, or a
    // higher-priority branch taking over. The commitment ends with the branch;
    // leaving the key set would latch the selector on an attack that is no
    // longer running and can only be recovered by the dispatch timeout.
    ReleaseCommitment(&OwnerComp);

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
    return FString::Printf(TEXT("Activate: %s%s%s"),
        AbilityTagKey.IsNone()
            ? (AbilityTag.IsValid() ? *AbilityTag.ToString() : TEXT("(no tag set)"))
            : *FString::Printf(TEXT("<%s>"), *AbilityTagKey.SelectedKeyName.ToString()),
        bWaitForAbilityEnd ? TEXT("\nwaits for ability end") : TEXT("\nfire and forget"),
        ReleaseKey.IsNone()
            ? TEXT("")
            : *FString::Printf(TEXT("\nreleases %s on finish"), *ReleaseKey.SelectedKeyName.ToString()));
}