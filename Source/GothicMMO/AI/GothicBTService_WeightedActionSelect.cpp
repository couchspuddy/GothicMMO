// GothicBTService_WeightedActionSelect.cpp

#include "AI/GothicBTService_WeightedActionSelect.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AI/GothicAttackCommitmentComponent.h"
#include "AI/GothicBossArenaManager.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "Character/GothicCharacterBase.h"
#include "BehaviorTree/BehaviorTree.h"      // Asset.GetName() in InitializeFromAsset
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "GameFramework/Pawn.h"

namespace
{
    /** Per-instance decision-commitment tracking — when a movement entry
     *  (no ability to check IsActive() against) was last chosen, so it can
     *  hold for MinMovementCommitDuration instead of rerolling every tick. */
    struct FGothicWeightedActionMemory
    {
        float LastDecisionTime = -1.f;

        /** Deferred-validation state, same pattern as CombatSync. */
        float TimeSinceRelevant = 0.f;
        bool  bValidated        = false;

        /** Cached arena manager for the aggression read. Weak because the
         *  manager is a level actor and this memory outlives a seamless
         *  travel; the null is a legitimate answer (no manager in this level),
         *  so bSearchedForArena distinguishes "not found" from "not looked
         *  yet" and stops us re-scanning the actor list every tick. */
        TWeakObjectPtr<AGothicBossArenaManager> ArenaManager;
        bool bSearchedForArena = false;

        // ── Attack commitment (bSplitAttackPool) ─────────────────────────────
        //
        // NOT HERE ANY MORE. Phase 2 kept the commitment in this struct and it
        // worked for a lock around one ability, but the release/advance point
        // it was written to feed could never execute: the dispatch task returns
        // Succeeded, the branch unwinds, the subtree is re-entered, and
        // OnBecomeRelevant zeroes this memory in the very same event that was
        // supposed to be observed. Measured over a full PIE session: 13
        // ATTACK-commit, 47 ATTACK-committed, 0 ATTACK-release.
        //
        // The commitment now lives on UGothicAttackCommitmentComponent, whose
        // lifetime is the controller's possession rather than a subtree's
        // activation. See that file's header for why it beat Blackboard keys
        // and controller-class state. Everything left in this struct is
        // genuinely per-branch-activation and SHOULD die on re-entry.

        /**
         * Diagnostic only — hash of the last VigilTimeline line emitted for
         * this pawn, so a state that persists across ticks (a hold window, a
         * repeated identical pick) logs once instead of five times a second.
         * A hash rather than the string itself because BT node memory is a raw
         * byte block that is never constructed or destructed — an FString here
         * would leak its allocation on every branch deactivation. Zeroed memory
         * gives hash 0, which no real line collides with in practice, so the
         * first line of a branch activation always prints.
         */
        uint32 LastTimelineHash = 0;
    };
}

uint16 UGothicBTService_WeightedActionSelect::GetInstanceMemorySize() const
{
    return sizeof(FGothicWeightedActionMemory);
}

UGothicBTService_WeightedActionSelect::UGothicBTService_WeightedActionSelect()
{
    NodeName = TEXT("Gothic Weighted Action Select");

    // 5Hz, matching CombatSync — fast enough to feel reactive when a target
    // enters/leaves a range band, cheap at the enemy counts this project runs.
    Interval        = 0.2f;
    RandomDeviation = 0.05f;

    bNotifyBecomeRelevant = true;

    ChosenActionKey.AddNameFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTService_WeightedActionSelect, ChosenActionKey));
    TargetActorKey.AddObjectFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTService_WeightedActionSelect, TargetActorKey),
        AActor::StaticClass());
    // OPTIONAL — only the split path uses it, and it must read as unset on the
    // trees that still run combined. Without AllowNoneAsValue,
    // FBlackboardKeySelector::ResolveSelectedKey auto-binds an untouched
    // selector to the first blackboard key matching the type filter, which on
    // every tree here is ChosenAction — the key the movement decorators
    // equality-test. That is the catastrophic aliasing InitializeFromAsset now
    // guards against; this is the line that stops it happening by accident.
    ChosenAttackTagKey.AddNameFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTService_WeightedActionSelect, ChosenAttackTagKey));
    ChosenAttackTagKey.AllowNoneAsValue(true);
}

void UGothicBTService_WeightedActionSelect::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);

    if (UBlackboardData* BBAsset = GetBlackboardAsset())
    {
        ChosenActionKey.ResolveSelectedKey(*BBAsset);
        TargetActorKey.ResolveSelectedKey(*BBAsset);
        ChosenAttackTagKey.ResolveSelectedKey(*BBAsset);
    }

    // ── Aliasing guard ───────────────────────────────────────────────────────
    // Worse than an unset attack key, and it has to be checked by key ID rather
    // than by name because two selectors resolving to the same key is exactly
    // what "the same key" means here. If the attack layer writes a full ability
    // tag name into the key the movement decorators compare against ActionIDs,
    // every movement branch fails at once and the pawn freezes — while the
    // dispatch task and this service both look correctly configured.
    //
    // Still checked with AllowNoneAsValue in place: the auto-binding path is
    // closed, but a designer can still pick the same key by hand from the
    // dropdown, and trees serialized before the fix keep their bound value.
    bAttackLayerAliased =
        bSplitAttackPool
        && !ChosenAttackTagKey.IsNone()
        && !ChosenActionKey.IsNone()
        && ChosenAttackTagKey.GetSelectedKeyID() == ChosenActionKey.GetSelectedKeyID();

    if (bAttackLayerAliased)
    {
        UE_LOG(LogVigilCombat, Error,
            TEXT("WeightedActionSelect[%s|%s]: ChosenAttackTagKey ('%s') and ChosenActionKey ('%s') resolve to the "
                 "SAME blackboard key. The attack layer would write ability tag names into the key the movement "
                 "decorators equality-test. Disabling the attack layer for this tree — the pawn will position and "
                 "never attack until the two keys are set to different blackboard entries."),
            *Asset.GetName(), *GetNodeName(),
            *ChosenAttackTagKey.SelectedKeyName.ToString(),
            *ChosenActionKey.SelectedKeyName.ToString());
    }
}

void UGothicBTService_WeightedActionSelect::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    Super::OnBecomeRelevant(OwnerComp, NodeMemory);

    // Arm the deferred check rather than running it here — the other half of
    // CombatSync's pattern, which this node was missing. Running the scan now
    // reads the pawn before its BeginPlay has called GrantStartupAbilities, so
    // every tag reports as missing: 18 enemies × one false error on the spawn
    // frame, all within 4ms of the world coming up for play. The abilities were
    // granted correctly the whole time.
    if (FGothicWeightedActionMemory* Memory = CastInstanceNodeMemory<FGothicWeightedActionMemory>(NodeMemory))
    {
        Memory->TimeSinceRelevant = 0.f;
        Memory->bValidated        = false;

        // Re-resolve the arena manager per branch activation rather than
        // trusting a stale weak pointer across a level change.
        Memory->ArenaManager.Reset();
        Memory->bSearchedForArena = false;
        Memory->LastTimelineHash  = 0;

        // The attack commitment is deliberately NOT cleared here any more.
        //
        // Phase 2 treated every branch re-activation as a hard interrupt. That
        // read is wrong, and it is wrong in the most common case rather than an
        // edge one: the ordinary end of a successful attack IS a branch
        // re-activation. The dispatch task succeeds, the Selector succeeds, the
        // Sequence completes, the subtree is re-entered — so clearing here
        // erased every commitment exactly when the service needed to observe it
        // ending, and the release/advance point never ran once in a full
        // session. A chain cannot survive a single step under that rule.
        //
        // What still interrupts a commitment, all of it enforced in SelectSplit
        // where the world can actually be read: target loss, the per-step
        // recovery deadline (which is what turns "the branch went away for a
        // stagger or a phase change" into a chain exit), the dispatch timeout,
        // and an outside writer taking the key. Re-activation on its own is not
        // evidence of any of those — it is the sound of the tree working.
    }
}

float UGothicBTService_WeightedActionSelect::GetArenaAggression(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
    FGothicWeightedActionMemory* Memory =
        CastInstanceNodeMemory<FGothicWeightedActionMemory>(NodeMemory);

    UWorld* World = OwnerComp.GetWorld();
    if (!Memory || !World)
    {
        return 1.f;
    }

    if (!Memory->bSearchedForArena)
    {
        Memory->bSearchedForArena = true;
        Memory->ArenaManager = Cast<AGothicBossArenaManager>(
            UGameplayStatics::GetActorOfClass(World, AGothicBossArenaManager::StaticClass()));
    }

    // No manager is the normal case, not a failure: only the Rotunda has one.
    // 1.0 makes every aggression term below vanish, so the Thrall and Retained
    // trees run byte-identically to before this existed.
    const AGothicBossArenaManager* Arena = Memory->ArenaManager.Get();
    return Arena ? Arena->GetAggressionMultiplier() : 1.f;
}

void UGothicBTService_WeightedActionSelect::ValidateConfiguration(UBehaviorTreeComponent& OwnerComp) const
{
    // Same validation philosophy as CombatSync: catch an AssetTag that
    // matches nothing, loudly, instead of a permanently-zero-weight
    // entry nobody notices until the fight feels thinner than it should.
    // Runs once, after the grace period, so what it reports is the settled
    // state rather than a startup race.
    const AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* Pawn = AIC ? AIC->GetPawn() : nullptr;
    UAbilitySystemComponent* ASC =
        Pawn ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn) : nullptr;

    // The one way to turn this node on and get a pawn that positions forever and
    // never attacks. Cheap to check, impossible to spot in the editor.
    if (bSplitAttackPool && ChosenAttackTagKey.IsNone())
    {
        UE_LOG(LogTemp, Error,
            TEXT("WeightedActionSelect[%s]: bSplitAttackPool is on but ChosenAttackTagKey is unset — "
                 "the attack layer has nowhere to write and this pawn will never attack."),
            *GetNameSafe(Pawn));
    }

    if (!ASC)
    {
        UE_LOG(LogTemp, Error,
            TEXT("WeightedActionSelect[%s]: pawn has no ASC — every entry will be ineligible"),
            *GetNameSafe(Pawn));
        return;
    }

    for (const FGothicWeightedActionEntry& Entry : Actions)
    {
        if (!Entry.AbilityTag.IsValid())
        {
            continue;
        }

        bool bFound = false;
        for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
        {

            if (Spec.Ability && Spec.Ability->GetAssetTags().HasTag(Entry.AbilityTag))
            {
                bFound = true;
                break;
            }
            
        }

        if (!bFound)
        {
            UE_LOG(LogTemp, Error,
                TEXT("WeightedActionSelect[%s]: after %.1fs, entry '%s' AssetTag '%s' matches no granted ability. "
                     "Check AssetTags on the ability, not AbilityInputTag."),
                *GetNameSafe(Pawn), ValidationGraceSeconds,
                *Entry.ActionID.ToString(), *Entry.AbilityTag.ToString());
        }
    }
}

bool UGothicBTService_WeightedActionSelect::IsAbilityReady(
    UAbilitySystemComponent* ASC, const FGameplayTag& AbilityTag) const
{
    if (!ASC || !AbilityTag.IsValid())
    {
        return false;
    }

    for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
    {
        if (!Spec.Ability || !Spec.Ability->GetAssetTags().HasTag(AbilityTag))
        {
            continue;
        }

        return !Spec.IsActive()
            && Spec.Ability->CanActivateAbility(Spec.Handle, ASC->AbilityActorInfo.Get());
    }

    return false;
}

bool UGothicBTService_WeightedActionSelect::IsAbilityActive(
    UAbilitySystemComponent* ASC, const FGameplayTag& AbilityTag) const
{
    if (!ASC || !AbilityTag.IsValid())
    {
        return false;
    }

    for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
    {
        if (Spec.Ability && Spec.Ability->GetAssetTags().HasTag(AbilityTag))
        {
            return Spec.IsActive();
        }
    }

    return false;
}

void UGothicBTService_WeightedActionSelect::TickNode(UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    // One-shot config validation, once the grace period has passed and the
    // pawn's StartupAbilities have actually been granted.
    if (FGothicWeightedActionMemory* Memory = CastInstanceNodeMemory<FGothicWeightedActionMemory>(NodeMemory))
    {
        if (!Memory->bValidated)
        {
            Memory->TimeSinceRelevant += DeltaSeconds;
            if (Memory->TimeSinceRelevant >= ValidationGraceSeconds)
            {
                Memory->bValidated = true;
                ValidateConfiguration(OwnerComp);
            }
        }
    }

    SelectAndWrite(OwnerComp, NodeMemory);
}

float UGothicBTService_WeightedActionSelect::ScoreEntries(
    UAbilitySystemComponent* ASC, float DistanceToTarget,
    float RecklessFactor, float AggressionFactor, EPoolFilter Filter,
    TArray<int32>& OutIndices, TArray<float>& OutScores,
    TArray<FString>& OutGateParts) const
{
    float TotalWeight = 0.f;

    for (int32 i = 0; i < Actions.Num(); ++i)
    {
        const FGothicWeightedActionEntry& Entry = Actions[i];

        const bool bIsAttack = Entry.AbilityTag.IsValid();
        const bool bInFilter =
            Filter == EPoolFilter::All
            || (Filter == EPoolFilter::Attack   &&  bIsAttack)
            || (Filter == EPoolFilter::Movement && !bIsAttack);

        float Score = 0.f;

        const bool bReady = !bIsAttack || IsAbilityReady(ASC, Entry.AbilityTag);
        const bool bInRange =
            DistanceToTarget < 0.f
            || ((Entry.MinRange < 0.f || DistanceToTarget >= Entry.MinRange)
                && (Entry.MaxRange < 0.f || DistanceToTarget <= Entry.MaxRange));

        if (bInFilter && bReady && bInRange)
        {
            // Two bias terms of the same shape, both additive per entry and
            // both zero at their baseline. Additive and per-entry is the only
            // form that does anything: a uniform multiplier over every score
            // cancels out of the weighted roll entirely.
            Score = FMath::Max(0.f,
                Entry.BaseWeight
                + Entry.RecklessnessWeightBonus * RecklessFactor
                + Entry.AggressionWeightBonus   * AggressionFactor);
        }

        if (Score > 0.f)
        {
            OutIndices.Add(i);
            OutScores.Add(Score);
            TotalWeight += Score;
        }

        // Gate parts cover every entry in authored order whatever the filter,
        // so a diagnostic line reads the same in both paths and an entry that
        // is permanently r0 is still visible when it is out of the pool being
        // rolled. Out-of-filter entries report their gates and a zero weight.
        OutGateParts.Add(FString::Printf(TEXT("%s:r%d,R%d,w%.2f"),
            *Entry.ActionID.ToString(), bReady ? 1 : 0, bInRange ? 1 : 0, Score));
    }

    return TotalWeight;
}

int32 UGothicBTService_WeightedActionSelect::RollWeighted(
    const TArray<int32>& Indices, const TArray<float>& Scores,
    float TotalWeight, float& OutRoll, float& OutRemainder, bool& bOutWasTail) const
{
    bOutWasTail = false;
    OutRoll = 0.f;
    OutRemainder = 0.f;

    if (Indices.Num() == 0 || TotalWeight <= 0.f)
    {
        return INDEX_NONE;
    }

    float Roll = FMath::FRandRange(0.f, TotalWeight);
    OutRoll = Roll;

    for (int32 i = 0; i < Indices.Num(); ++i)
    {
        if (Roll <= Scores[i])
        {
            OutRemainder = Roll;
            return Indices[i];
        }
        Roll -= Scores[i];
    }

    // Fallthrough. TotalWeight is accumulated by repeated float addition while
    // Roll is drawn against that sum, so on the last entry the running
    // remainder can sit a few ULPs above its score purely from rounding. The
    // loop then exits WITHOUT a pick — and a Blackboard Name key is sticky, so
    // the enemy silently keeps running whatever it picked last time. Landing on
    // the last eligible entry is exactly what an exact-arithmetic roll of
    // TotalWeight would have selected.
    bOutWasTail = true;
    OutRemainder = Roll;
    return Indices.Last();
}

void UGothicBTService_WeightedActionSelect::SelectAndWrite(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    AAIController* AIC       = OwnerComp.GetAIOwner();
    APawn* Pawn              = AIC ? AIC->GetPawn() : nullptr;

    if (!BB || !Pawn || ChosenActionKey.IsNone())
    {
        return;
    }

    UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn);
    auto* Memory = reinterpret_cast<FGothicWeightedActionMemory*>(NodeMemory);
    const float Now = OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetTimeSeconds() : 0.f;

    // ── VigilTimeline ────────────────────────────────────────────────────────
    //
    // Diagnostic instrumentation, shared line format with
    // GothicBTService_CombatSync so `VigilTimeline` greps both services at once
    // and the two streams interleave by their `t=` stamp. The question these
    // two halves exist to answer together is: at the instants ChosenAction
    // reads a given action, what does that action's readiness key read — and do
    // the two ever hold TRUE SIMULTANEOUSLY, which is what an AND of two
    // decorators requires. A point-in-time Blackboard dump cannot answer that;
    // both keys reading true in one sample only proves they were both true when
    // sampled, not that either was true when the tree evaluated.
    //
    // Dedupe policy: a line is emitted whenever its phase+detail differs from
    // the previous one. Both services tick at 5Hz, so an unconditional line per
    // tick would be ~10 lines/sec/pawn of mostly-identical text; a hold window
    // or a repeated identical pick collapses to one line, and the reader takes
    // "the state held until the next line" as the meaning of a gap. The one
    // exception is bAlways below: a write that actually CHANGES ChosenAction is
    // always logged, because that transition is precisely the event being timed
    // and must never be swallowed by a hash collision or a repeat.
    auto LogTimeline = [&](const TCHAR* Phase, const FString& Detail, bool bAlways)
    {
        const FString Line = FString::Printf(TEXT("%s|%s"), Phase, *Detail);
        const uint32 Hash  = GetTypeHash(Line);

        if (!bAlways && Memory && Memory->LastTimelineHash == Hash)
        {
            return;
        }

        if (Memory)
        {
            Memory->LastTimelineHash = Hash;
        }

        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|ActionSelect|%s"), Now, *GetNameSafe(Pawn), *Line);
    };

    // Rotunda pillar escalation. 1.0 with four pillars up (and in every level
    // without an arena manager), 2.0 with all four down. Read once per tick and
    // used twice below: as a per-entry weight bias, and to shorten the movement
    // commit window.
    const float Aggression = GetArenaAggression(OwnerComp, NodeMemory);

    // (Aggression - 1), so the term contributes exactly nothing at baseline.
    const float AggressionFactor = FMath::Max(0.f, Aggression - 1.f);

    // A stripped arena shortens how long a movement pick can hold the key: at
    // x2.0 the boss commits to walking for half as long before the pool is
    // allowed to reconsider and pick an attack instead.
    const float EffectiveCommitDuration =
        (bScaleCommitDurationByAggression && Aggression > KINDA_SMALL_NUMBER)
            ? MinMovementCommitDuration / Aggression
            : MinMovementCommitDuration;

    // Recklessness bias: (1 - HealthPercent), 0 at full health, 1 at death.
    // Reads straight off the character's own attributes — no Blackboard float
    // key needed, one less thing to wire per BT asset.
    //
    // Hoisted above the two paths below. Pure reads with no logging and no
    // early return, so the legacy path's observable order is unchanged.
    float RecklessFactor = 0.f;
    if (const AGothicCharacterBase* Character = Cast<AGothicCharacterBase>(Pawn))
    {
        const float MaxHealth = Character->GetMaxHealth();
        if (MaxHealth > 0.f)
        {
            RecklessFactor = 1.f - FMath::Clamp(Character->GetHealth() / MaxHealth, 0.f, 1.f);
        }
    }

    AActor* Target = TargetActorKey.IsNone()
        ? nullptr
        : Cast<AActor>(BB->GetValue<UBlackboardKeyType_Object>(TargetActorKey.GetSelectedKeyID()));

    // HORIZONTAL distance. MinRange/MaxRange on the entries are authored against
    // the reach of an animation — the Bestial Lucid's Claw band is 120-160uu —
    // and an actor's location is its capsule CENTRE, so a 3D measurement carries
    // a constant |HalfHeightA - HalfHeightB| offset that has nothing to do with
    // how far apart the two creatures are on the floor. Her capsule going 88 ->
    // 253 put a 165uu floor under that number: at 150uu of real contact the 3D
    // distance read 327uu, outside every band in the pool, and this service
    // logged "nothing eligible (5 entries, all gated)" for the whole encounter.
    //
    // A range band must describe the world, not the rig.
    const float DistanceToTarget = Target
        ? FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation())
        : -1.f;

    if (bSplitAttackPool)
    {
        SelectSplit(OwnerComp, NodeMemory, BB, Pawn, ASC, Target, DistanceToTarget,
            RecklessFactor, AggressionFactor, EffectiveCommitDuration, Now, LogTimeline);
        return;
    }

    // ── Legacy combined path ────────────────────────────────────────────────
    // Everything below is the pre-split behaviour, unchanged, and is what
    // BT_EnemyCombat and BT_FeralRetained still run.

    // Don't interrupt a decision that's still genuinely running. This is the
    // whole fix for fast-cycling: without it, every 0.2s tick re-rolls and
    // aborts whatever just started, regardless of how long it actually needs.
    const FName CurrentAction = BB->GetValue<UBlackboardKeyType_Name>(ChosenActionKey.GetSelectedKeyID());
    if (!CurrentAction.IsNone())
    {
        const FGothicWeightedActionEntry* CurrentEntry = Actions.FindByPredicate(
            [&CurrentAction](const FGothicWeightedActionEntry& E) { return E.ActionID == CurrentAction; });

        if (CurrentEntry)
        {
            const bool bWithinCommitWindow = Memory
                && Memory->LastDecisionTime >= 0.f
                && (Now - Memory->LastDecisionTime) < (CurrentEntry->AbilityTag.IsValid()
                    ? AbilityActivationGrace
                    : EffectiveCommitDuration);

            // Diagnostic context for the hold lines below. A held key and a
            // freshly-rolled key are byte-identical in a Blackboard dump, and
            // the difference is the whole question here: "ChosenAction=Charge"
            // for 0.5s of activation grace after a pick that never reached the
            // task is a completely different fact from Charge winning the roll
            // on that tick.
            const float HoldElapsed = (Memory && Memory->LastDecisionTime >= 0.f)
                ? (Now - Memory->LastDecisionTime) : -1.f;
            const float HoldWindow  = CurrentEntry->AbilityTag.IsValid()
                ? AbilityActivationGrace : EffectiveCommitDuration;
            const FString HoldDetail = FString::Printf(
                TEXT("held='%s'|elapsed=%.3f|window=%.3f|tag=%s"),
                *CurrentAction.ToString(), HoldElapsed, HoldWindow,
                CurrentEntry->AbilityTag.IsValid()
                    ? *CurrentEntry->AbilityTag.ToString() : TEXT("(movement)"));

            if (CurrentEntry->AbilityTag.IsValid())
            {
                // Hold while it's genuinely running, AND while it's still on
                // its way to running. The second half is the important one:
                // an ability pick is not active on the tick after it's made,
                // because the tree still has to abort the current branch and
                // reach ActivateAbilityByTag. Without the grace, the pick was
                // reroll-eligible before it could ever start, while movement
                // picks locked the key the moment they were written — so the
                // key drifted to movement no matter how the weights were set.
                //
                // Hoisted into a local purely so the log can name WHICH of the
                // two clauses held the key. The original expression evaluated
                // IsAbilityActive first and unconditionally, so this is the
                // same call in the same order — no short-circuit was lost.
                const bool bStillActive = IsAbilityActive(ASC, CurrentEntry->AbilityTag);
                if (bStillActive || bWithinCommitWindow)
                {
                    LogTimeline(bStillActive ? TEXT("HOLD-ability-active")
                                             : TEXT("HOLD-activation-grace"),
                        HoldDetail, /*bAlways=*/false);
                    return;
                }
            }
            else if (bWithinCommitWindow)
            {
                LogTimeline(TEXT("HOLD-movement-commit"), HoldDetail, /*bAlways=*/false);
                return;
            }
        }
    }

    // TargetActorKey is configured but currently empty — no real combat
    // target exists right now (pre-combat, target lost/died, leashing).
    // Nothing in the pool should fire without one, not just range-gated
    // entries — leave ChosenAction untouched, same honest-degradation
    // pattern as the TotalWeight<=0 case below, and let the tree fall
    // through to whatever non-combat behavior it has.
    if (!TargetActorKey.IsNone() && !Target)
    {
        LogTimeline(TEXT("HOLD-no-target"),
            FString::Printf(TEXT("held='%s'|targetKey=%s"),
                *CurrentAction.ToString(), *TargetActorKey.SelectedKeyName.ToString()),
            /*bAlways=*/false);
        return;
    }

    // Score every entry, skip anything not ready/out of range, accumulate a
    // running total for the weighted roll below.
    //
    // Diagnostic: per-entry gate verdicts, so a pick line also says what the
    // pick was made FROM. An entry that is permanently `r0` while its readiness
    // key reads true in the CombatSync stream is the two services disagreeing;
    // an entry that is `r1 R1` on ticks where it never wins is just the roll.
    TArray<int32>   Indices;
    TArray<float>   Scores;
    TArray<FString> GateParts;
    Indices.Reserve(Actions.Num());
    Scores.Reserve(Actions.Num());
    GateParts.Reserve(Actions.Num());

    const float TotalWeight = ScoreEntries(ASC, DistanceToTarget, RecklessFactor,
        AggressionFactor, EPoolFilter::All, Indices, Scores, GateParts);

    const FString Breakdown = FString::Printf(TEXT("dist=%.1f|total=%.2f|gates=[%s]"),
        DistanceToTarget, TotalWeight, *FString::Join(GateParts, TEXT(" ")));

    // Nothing eligible — everything on cooldown, everything out of range.
    //
    // This used to return without writing, on the theory that leaving the key
    // alone was honest degradation. It is the opposite: the key is sticky, so
    // the LAST pick stays in it forever and the tree keeps running that branch.
    // Measured on the Bestial Lucid — once she rolled "Reposition" and then hit
    // a tick with nothing eligible, ChosenAction read "Reposition" for the rest
    // of the fight. Every ability came off cooldown into a Blackboard that
    // still said reposition, so she paced. Permanently.
    //
    // Falling back to a movement entry is safe in a way that falling back to an
    // ability is not: it has no cooldown to be wrong about and no range gate to
    // violate, so it is always a legal thing to be doing while waiting for the
    // pool to reopen. If the pool has no such entry, clear the key instead —
    // an empty ChosenAction fails every equality decorator and the tree's own
    // Selector fallback takes over, which is what the old comment claimed
    // happened but could not, because the key was never empty.
    if (TotalWeight <= 0.f)
    {
        const FGothicWeightedActionEntry* Fallback = Actions.FindByPredicate(
            [this](const FGothicWeightedActionEntry& E)
            {
                return E.ActionID == FallbackActionID && !E.AbilityTag.IsValid();
            });

        const FName NewAction = Fallback ? Fallback->ActionID : NAME_None;

        LogTimeline(TEXT("FALLBACK"),
            FString::Printf(TEXT("'%s'->'%s'|%s"),
                *CurrentAction.ToString(), *NewAction.ToString(), *Breakdown),
            /*bAlways=*/CurrentAction != NewAction);

        if (CurrentAction != NewAction)
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("WeightedActionSelect[%s]: nothing eligible (%d entries, all gated) — "
                     "'%s' -> '%s' rather than freezing the key"),
                *GetNameSafe(Pawn), Actions.Num(),
                *CurrentAction.ToString(), *NewAction.ToString());

            BB->SetValue<UBlackboardKeyType_Name>(ChosenActionKey.GetSelectedKeyID(), NewAction);
            if (Memory)
            {
                Memory->LastDecisionTime = Now;
            }
        }

        return;
    }

    float InitialRoll = 0.f;
    float Remainder   = 0.f;
    bool  bWasTail    = false;
    const int32 Winner = RollWeighted(Indices, Scores, TotalWeight, InitialRoll, Remainder, bWasTail);

    if (Actions.IsValidIndex(Winner))
    {
        LogTimeline(bWasTail ? TEXT("PICK-tail") : TEXT("PICK"),
            FString::Printf(TEXT("'%s'->'%s'|roll=%.3f|remainder=%.6f|%s"),
                *CurrentAction.ToString(), *Actions[Winner].ActionID.ToString(),
                InitialRoll, Remainder, *Breakdown),
            /*bAlways=*/CurrentAction != Actions[Winner].ActionID);

        BB->SetValue<UBlackboardKeyType_Name>(
            ChosenActionKey.GetSelectedKeyID(), Actions[Winner].ActionID);
        if (Memory)
        {
            Memory->LastDecisionTime = Now;
        }
    }
}

// ============================================================================
// Split path — the positioning layer and the attack layer
//
// The shape, in one paragraph. Both layers are scored from the SAME authored
// Actions array, partitioned on whether an entry carries an AbilityTag. One
// roll decides which layer acts this tick — at P(attack) = AttackTotal /
// (AttackTotal + MovementTotal), which reproduces the combined roll's odds
// exactly at AttackLayerWeightScale 1.0 — and a second roll picks within the
// winning layer. Movement writes ChosenAction and is protected only by
// MinMovementCommitDuration, because nothing about walking is a commitment.
// An attack writes its ABILITY TAG to ChosenAttackTagKey and LOCKS: from that
// instant until the key is cleared, neither layer is re-scored at all.
//
// Why the lock is the key and not a timer. Everything this phase replaces was
// a stopwatch approximating "is the decision still in flight" —
// AbilityActivationGrace guessed 0.5s from the service's own tick rate, and
// was wrong in both directions (too short for a slow dispatch, too long for a
// refused activation). The dispatch task now clears the key on every exit path
// it has, so "the ability is over" and "the lock is open" are the same event,
// observed rather than estimated. The only timer left is AttackDispatchTimeout,
// which is not a commitment window: it fires only when the ability NEVER went
// active, means the tree is miswired, and says so at Error.
// ============================================================================
// ── Chain helpers (phase 3) ──────────────────────────────────────────────────

FString UGothicBTService_WeightedActionSelect::DescribeChain(int32 ChainIndex) const
{
    if (!Chains.IsValidIndex(ChainIndex))
    {
        return TEXT("none");
    }

    const FName ID = Chains[ChainIndex].ChainID;
    return ID.IsNone() ? FString::Printf(TEXT("#%d"), ChainIndex) : ID.ToString();
}

float UGothicBTService_WeightedActionSelect::GetRecoveryWindow(int32 ChainIndex, int32 StepIndex) const
{
    if (Chains.IsValidIndex(ChainIndex) && Chains[ChainIndex].Steps.IsValidIndex(StepIndex))
    {
        const float Authored = Chains[ChainIndex].Steps[StepIndex].RecoveryWindow;
        if (Authored > 0.f)
        {
            return Authored;
        }
    }

    return ChainRecoveryWindow;
}

int32 UGothicBTService_WeightedActionSelect::PickChainForEntry(
    const FGothicWeightedActionEntry& Entry, float DistanceToTarget) const
{
    // The overwhelmingly common case, and the one that must cost nothing: no
    // chains authored, so the attack that just won the roll runs as a single
    // attack exactly as it did in phase 2.
    if (Chains.Num() == 0 || !Entry.AbilityTag.IsValid())
    {
        return INDEX_NONE;
    }

    TArray<int32> Eligible;
    TArray<float> Weights;

    // The fixed 1.0 for "no chain" is seeded first, so a chain's Weight is read
    // against it directly and the roll needs no normalisation.
    float Total = 1.f;

    for (int32 i = 0; i < Chains.Num(); ++i)
    {
        const FGothicAttackChain& Chain = Chains[i];

        // An empty EntryTags attaches to nothing, on purpose — see the struct.
        if (Chain.Steps.Num() == 0 || Chain.Weight <= 0.f
            || !Chain.EntryTags.HasTag(Entry.AbilityTag))
        {
            continue;
        }

        // Entry band, over and above the pool entry's own gate. Horizontal, for
        // the same reason every other range in this file is.
        if (DistanceToTarget >= 0.f)
        {
            if (Chain.EntryMinRange >= 0.f && DistanceToTarget < Chain.EntryMinRange)
            {
                continue;
            }
            if (Chain.EntryMaxRange >= 0.f && DistanceToTarget > Chain.EntryMaxRange)
            {
                continue;
            }
        }

        // A chain whose FIRST step cannot be dispatched is not eligible: entering
        // it would burn the commitment on a step that clears the key immediately.
        if (!Chains[i].Steps[0].AbilityTag.IsValid())
        {
            continue;
        }

        Eligible.Add(i);
        Weights.Add(Chain.Weight);
        Total += Chain.Weight;
    }

    if (Eligible.Num() == 0)
    {
        return INDEX_NONE;
    }

    float Roll = FMath::FRandRange(0.f, Total);

    for (int32 k = 0; k < Eligible.Num(); ++k)
    {
        Roll -= Weights[k];
        if (Roll <= 0.f)
        {
            return Eligible[k];
        }
    }

    // Fell through the chain weights into the fixed 1.0 remainder: no chain.
    return INDEX_NONE;
}

bool UGothicBTService_WeightedActionSelect::TryAdvanceChain(
    UBlackboardComponent* BB, FBlackboard::FKey AttackKeyID,
    UGothicAttackCommitmentComponent* Commit, UAbilitySystemComponent* ASC,
    float DistanceToTarget, float Now, EGothicChainExitReason& OutReason,
    FString& OutBranchReason,
    TFunctionRef<void(const TCHAR*, const FString&, bool)> LogTimeline) const
{
    OutReason = EGothicChainExitReason::Completed;
    OutBranchReason = TEXT("-");

    if (!BB || !Commit || !Chains.IsValidIndex(Commit->ChainIndex))
    {
        return false;
    }

    const FGothicAttackChain& Chain = Chains[Commit->ChainIndex];
    const int32 FromStep = Commit->ChainStepIndex;

    if (!Chain.Steps.IsValidIndex(FromStep))
    {
        return false;
    }

    // ── Where does this step go? ─────────────────────────────────────────────
    // First passing branch wins. No branches at all means the plain sequential
    // next step, which is what a straight three-hit combo authors as.
    const TArray<FGothicAttackStepBranch>& Branches = Chain.Steps[FromStep].Branches;

    int32 NextStep = FromStep + 1;
    FString BranchReason = TEXT("sequential");

    // Which of the two ways "no branch passed" can happen actually happened.
    // The loop treats both as `continue`, but they are different problems: a
    // range gate rejecting every branch is the dodge check working, while a
    // gate passed and then failed on bRequireAbilityReady is a cooldown
    // collision. Nothing downstream could tell them apart before this.
    bool bAnyBranchPassedRange = false;

    if (Branches.Num() > 0)
    {
        NextStep = INDEX_NONE;
        BranchReason = TEXT("no-branch-passed");

        for (int32 b = 0; b < Branches.Num(); ++b)
        {
            const FGothicAttackStepBranch& Branch = Branches[b];

            if (DistanceToTarget >= 0.f)
            {
                if (Branch.MinRange >= 0.f && DistanceToTarget < Branch.MinRange)
                {
                    continue;
                }
                if (Branch.MaxRange >= 0.f && DistanceToTarget > Branch.MaxRange)
                {
                    continue;
                }
            }

            bAnyBranchPassedRange = true;

            // An authored early finish. Passing the gate and naming no step is a
            // decision, not a failure, so it takes effect immediately rather
            // than letting a later branch answer for it.
            if (!Chain.Steps.IsValidIndex(Branch.NextStep))
            {
                NextStep = INDEX_NONE;
                BranchReason = FString::Printf(TEXT("branch%d-ends"), b);
                break;
            }

            if (Branch.bRequireAbilityReady
                && !IsAbilityReady(ASC, Chain.Steps[Branch.NextStep].AbilityTag))
            {
                continue;
            }

            NextStep = Branch.NextStep;
            BranchReason = FString::Printf(TEXT("branch%d"), b);
            break;
        }
    }

    if (Branches.Num() > 0 && NextStep == INDEX_NONE && bAnyBranchPassedRange
        && BranchReason == TEXT("no-branch-passed"))
    {
        // A gate was passed; what killed it was the readiness test after it.
        BranchReason = TEXT("no-branch-ready");
    }

    OutBranchReason = BranchReason;

    if (!Chain.Steps.IsValidIndex(NextStep))
    {
        // Ran off the end, or nothing passed. None of these is a fault — an
        // unreachable follow-up is a short version of the combo — but they are
        // three different stories and the exit line has to tell them apart.
        OutReason = bAnyBranchPassedRange || Branches.Num() == 0
            ? (BranchReason == TEXT("no-branch-ready")
                ? EGothicChainExitReason::StepOnCooldown
                : EGothicChainExitReason::AuthoredEnd)
            : EGothicChainExitReason::NoBranchInRange;
        return false;
    }

    const FName StepTag = Chain.Steps[NextStep].AbilityTag.GetTagName();

    if (StepTag.IsNone()
        || !FGameplayTag::RequestGameplayTag(StepTag, /*ErrorIfNotFound=*/false).IsValid())
    {
        // Authoring fault, and it must be loud: dispatching this would write a
        // tag the task cannot resolve, which presents as a chain that silently
        // stops one hit early.
        OutReason = EGothicChainExitReason::BadStep;
        return false;
    }

    // ── Advance ──────────────────────────────────────────────────────────────
    // The key is written but the commitment is NOT cleared and NOT re-taken —
    // this is the whole difference between a chain and three attacks in a row.
    BB->SetValue<UBlackboardKeyType_Name>(AttackKeyID, StepTag);

    Commit->ChainStepIndex  = NextStep;
    Commit->CommittedTagName = StepTag;
    Commit->CommitTime      = Now;      // re-arm the deadlock breaker per step
    Commit->bDispatched     = false;
    Commit->LastStepEndTime = -1.f;

    LogTimeline(TEXT("CHAIN-advance"),
        FString::Printf(TEXT("chain=%s|step=%d->%d/%d|tag=%s|via=%s|held=%.3f|dist=%.1f"),
            *DescribeChain(Commit->ChainIndex), FromStep, NextStep, Chain.Steps.Num(),
            *StepTag.ToString(), *BranchReason,
            Commit->ChainEnterTime >= 0.f ? (Now - Commit->ChainEnterTime) : -1.f,
            DistanceToTarget),
        /*bAlways=*/true);

    return true;
}

void UGothicBTService_WeightedActionSelect::SelectSplit(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
    UBlackboardComponent* BB, APawn* Pawn, UAbilitySystemComponent* ASC,
    AActor* Target, float DistanceToTarget, float RecklessFactor,
    float AggressionFactor, float EffectiveCommitDuration, float Now,
    TFunctionRef<void(const TCHAR*, const FString&, bool)> LogTimeline)
{
    auto* Memory = CastInstanceNodeMemory<FGothicWeightedActionMemory>(NodeMemory);

    // The commitment, on the controller so it outlives this subtree's
    // activation. Everything below reads Commit where phase 2 read Memory.
    UGothicAttackCommitmentComponent* Commit =
        UGothicAttackCommitmentComponent::FindOrAdd(OwnerComp.GetAIOwner());

    if (!Commit)
    {
        // No AI controller at all. Nothing can be committed to, so the attack
        // layer has no state to reason about; refuse loudly rather than run a
        // lock whose memory silently does not persist.
        LogTimeline(TEXT("SPLIT-no-controller"),
            TEXT("no AIController — attack commitment has nowhere to live"), /*bAlways=*/false);
        return;
    }

    // No output key means the attack layer has nowhere to write. Refusing here
    // rather than quietly falling back to the combined path: a silent fallback
    // would present as "the split does nothing", which is the same symptom as
    // the split being wrong, and this project has already lost sessions to two
    // failures that looked identical.
    if (ChosenAttackTagKey.IsNone())
    {
        LogTimeline(TEXT("SPLIT-misconfigured"),
            TEXT("bSplitAttackPool is on but ChosenAttackTagKey is unset — attack layer disabled"),
            /*bAlways=*/false);
        return;
    }

    // Aliased keys (see InitializeFromAsset) disable the ATTACK layer only. The
    // movement layer below is untouched and still writes ChosenActionKey, so a
    // misconfigured tree positions instead of freezing — and, critically, we
    // never read or write the attack key, because it IS the movement key.
    const bool bAttackLayerLive = !bAttackLayerAliased;
    if (!bAttackLayerLive)
    {
        LogTimeline(TEXT("SPLIT-aliased"),
            FString::Printf(TEXT("attackKey='%s' aliases actionKey='%s' — attack layer disabled, movement only"),
                *ChosenAttackTagKey.SelectedKeyName.ToString(),
                *ChosenActionKey.SelectedKeyName.ToString()),
            /*bAlways=*/false);
    }

    const FBlackboard::FKey AttackKeyID = ChosenAttackTagKey.GetSelectedKeyID();

    // Reads as "nothing committed" while aliased: the real value of that key is
    // the current movement ActionID, and every commitment branch below would
    // misread it as an attack in flight.
    const FName CommittedTag  = bAttackLayerLive
        ? BB->GetValue<UBlackboardKeyType_Name>(AttackKeyID)
        : NAME_None;
    const FName CurrentAction = BB->GetValue<UBlackboardKeyType_Name>(ChosenActionKey.GetSelectedKeyID());

    // The branch string from the last advance attempt, if the exit came from
    // one. The enum names the KIND of ending; this names which branch produced
    // it, which is the difference between "the chain ended on a range gate" and
    // "the chain ended on branch 1's range gate". Reset per exit path by
    // TryAdvanceChain; "-" for every exit that never asked it anything.
    FString ChainBranchReason = TEXT("-");

    // Emits CHAIN-exit when the commitment being released was part of a chain,
    // so a chain's end is always one line naming its cause — an abnormal cause
    // at Error, since those are wiring or authoring faults rather than fight
    // outcomes. A chain ending on a range gate or a cooldown is neither; see
    // IsAbnormalChainExit.
    auto ExitChain = [&](EGothicChainExitReason Reason)
    {
        if (!Commit->IsChaining())
        {
            return;
        }

        const FString Detail = FString::Printf(
            TEXT("chain=%s|step=%d/%d|reason=%s|branch=%s|held=%.3f"),
            *DescribeChain(Commit->ChainIndex),
            Commit->ChainStepIndex,
            Chains.IsValidIndex(Commit->ChainIndex) ? Chains[Commit->ChainIndex].Steps.Num() : -1,
            *UEnum::GetDisplayValueAsText(Reason).ToString(),
            *ChainBranchReason,
            Commit->ChainEnterTime >= 0.f ? (Now - Commit->ChainEnterTime) : -1.f);

        LogTimeline(TEXT("CHAIN-exit"), Detail, /*bAlways=*/true);

        if (IsAbnormalChainExit(Reason))
        {
            UE_LOG(LogVigilCombat, Error,
                TEXT("WeightedActionSelect[%s]: attack chain exited abnormally — %s"),
                *GetNameSafe(Pawn), *Detail);
        }

        Commit->ClearChain();
    };

    auto ReleaseCommitment = [&](bool bClearKey, EGothicChainExitReason Reason)
    {
        ExitChain(Reason);

        if (bClearKey && bAttackLayerLive)
        {
            BB->SetValue<UBlackboardKeyType_Name>(AttackKeyID, NAME_None);
        }

        Commit->ClearAll();

        if (Memory)
        {
            // Force the movement layer to re-roll on this very tick instead of
            // serving out the remainder of a commit window that was frozen for
            // the whole attack. The positioning decision that was current when
            // the swing started describes a fight that has since moved.
            Memory->LastDecisionTime = -1.f;
        }
    };

    // ── Hard interrupt: no target ────────────────────────────────────────────
    // Leash break, target death, disengage. This outranks the commitment lock —
    // "no bail-outs" means the player cannot make her abandon a swing, not that
    // she keeps swinging at nothing after the fight is over.
    if (!TargetActorKey.IsNone() && !Target)
    {
        if (Commit->bCommitted)
        {
            LogTimeline(TEXT("ATTACK-interrupt"),
                FString::Printf(TEXT("committed='%s'|reason=no-target"),
                    *Commit->CommittedActionID.ToString()),
                /*bAlways=*/true);
            ReleaseCommitment(/*bClearKey=*/true, EGothicChainExitReason::NoTarget);
        }

        LogTimeline(TEXT("HOLD-no-target"),
            FString::Printf(TEXT("held='%s'|targetKey=%s"),
                *CurrentAction.ToString(), *TargetActorKey.SelectedKeyName.ToString()),
            /*bAlways=*/false);
        return;
    }

    // ── Commitment gate ──────────────────────────────────────────────────────
    if (Commit->bCommitted)
    {
        if (CommittedTag.IsNone())
        {
            // The dispatch task cleared the key: this step is over, however it
            // ended (finished, refused, cancelled, aborted).
            //
            // THE CHAIN ADVANCE POINT. This is the branch that phase 2 wrote and
            // that never once executed, because the state it tested was zeroed
            // by OnBecomeRelevant in the same event that produced the
            // transition. With the commitment on the controller it survives the
            // re-entry and this is reachable.
            //
            // The stopwatch matters here and nowhere else in the lock: the gap
            // between one step ending and this tick observing it is normally one
            // service tick, but if the subtree was taken away by a stagger or a
            // phase transition it can be arbitrarily long, and resuming a combo
            // into a fight that has moved is not commitment, it is a bug that
            // looks like one. LastStepEndTime is stamped on the first tick that
            // sees the key clear, so the deadline measures the branch's absence
            // rather than the swing's length.
            if (Commit->LastStepEndTime < 0.f)
            {
                Commit->LastStepEndTime = Now;
            }

            const float SinceStepEnd = Now - Commit->LastStepEndTime;

            if (Commit->IsChaining())
            {
                const float Window = GetRecoveryWindow(Commit->ChainIndex, Commit->ChainStepIndex);

                if (SinceStepEnd > Window)
                {
                    ReleaseCommitment(/*bClearKey=*/false, EGothicChainExitReason::RecoveryLapsed);
                    // Fall through and re-plan.
                }
                else
                {
                    EGothicChainExitReason AdvanceFailure = EGothicChainExitReason::Completed;

                    if (TryAdvanceChain(BB, AttackKeyID, Commit, ASC, DistanceToTarget,
                            Now, AdvanceFailure, ChainBranchReason, LogTimeline))
                    {
                        // Locked, and the next step is already on the key.
                        // Neither layer is scored — the movement layer never
                        // gets a tick between two hits of a combo, which is the
                        // entire point of a chain being ONE commitment.
                        return;
                    }

                    ReleaseCommitment(/*bClearKey=*/false, AdvanceFailure);
                }
            }
            else
            {
                LogTimeline(TEXT("ATTACK-release"),
                    FString::Printf(TEXT("was='%s'|tag=%s|dispatched=%d|held=%.3f"),
                        *Commit->CommittedActionID.ToString(),
                        *Commit->CommittedTagName.ToString(),
                        Commit->bDispatched ? 1 : 0,
                        Commit->CommitTime >= 0.f ? (Now - Commit->CommitTime) : -1.f),
                    /*bAlways=*/true);

                ReleaseCommitment(/*bClearKey=*/false, EGothicChainExitReason::Completed);
            }
            // Fall through and re-plan on this tick — no dead frame between the
            // end of a swing and the next decision.
        }
        else
        {
            // Someone other than us wrote the key (a Blueprint, a scripted
            // sequence, a designer forcing an opener). Adopt it rather than
            // fight over it — re-arming the timeout from now is the honest
            // reading of "a new commitment just started."
            if (CommittedTag != Commit->CommittedTagName)
            {
                // An outside writer has taken the key from under an in-flight
                // chain. The chain's remaining steps describe a sequence that is
                // no longer what the pawn is doing, so it exits (loudly) rather
                // than resuming after whatever was forced in finishes.
                ExitChain(EGothicChainExitReason::Superseded);

                Commit->CommittedTagName  = CommittedTag;
                Commit->CommittedActionID = NAME_None;
                Commit->CommitTime        = Now;
                Commit->bDispatched       = false;
            }

            // A step is running again, so the next key-clear is a fresh
            // observation rather than one already stamped.
            Commit->LastStepEndTime = -1.f;

            const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(CommittedTag, /*ErrorIfNotFound=*/false);

            // Latching, not sampling. Once the ability has been seen active even
            // once, the deadlock breaker is disarmed for the rest of this
            // commitment: a swing that is between montage sections, or a chain
            // between steps, reads inactive and must not be mistaken for a
            // dispatch that never happened.
            if (Tag.IsValid() && IsAbilityActive(ASC, Tag))
            {
                Commit->bDispatched = true;
            }

            // Measured from the CURRENT step's dispatch, not from the chain's
            // entry. AttackDispatchTimeout is a deadlock breaker for "the tree
            // never reached the dispatch task", and each step gets to the task
            // independently, so a three-hit combo must not accumulate its steps'
            // durations into a timeout that fires mid-chain.
            const float Held = Commit->CommitTime >= 0.f ? (Now - Commit->CommitTime) : 0.f;

            if (!Commit->bDispatched && Held > AttackDispatchTimeout)
            {
                // Error, not Verbose. In a correctly wired tree this is
                // unreachable: the key is set, so the attack branch's decorator
                // passes, so the dispatch task runs and either activates or
                // clears the key itself. Reaching it means the BT never entered
                // the branch — which is exactly the failure that was diagnosed
                // by hand on July 30 and cost most of a session, so it gets a
                // line that names its own cause.
                UE_LOG(LogVigilCombat, Error,
                    TEXT("WeightedActionSelect[%s]: committed to attack '%s' (%s) %.2fs ago and it never went active. "
                         "Releasing. The tree never reached the dispatch task — check that the attack branch's "
                         "decorator is 'ChosenAttackTag Is Set' with Observer Aborts, and that the dispatch task's "
                         "AbilityTagKey points at the same Blackboard key this service writes."),
                    *GetNameSafe(Pawn), *Commit->CommittedActionID.ToString(),
                    *CommittedTag.ToString(), Held);

                LogTimeline(TEXT("ATTACK-dispatch-timeout"),
                    FString::Printf(TEXT("committed='%s'|tag=%s|held=%.3f|timeout=%.3f"),
                        *Commit->CommittedActionID.ToString(), *CommittedTag.ToString(),
                        Held, AttackDispatchTimeout),
                    /*bAlways=*/true);

                ReleaseCommitment(/*bClearKey=*/true, EGothicChainExitReason::DispatchTimeout);
                // Fall through and re-plan.
            }
            else
            {
                // THE LOCK. Neither layer is scored, neither key is written.
                // This is the whole of "once in those events it's locked to
                // those" — there is no weight, no range band and no better
                // option that can reach past this return.
                LogTimeline(TEXT("ATTACK-committed"),
                    FString::Printf(TEXT("committed='%s'|tag=%s|chain=%s|step=%d|dispatched=%d|held=%.3f|dist=%.1f"),
                        *Commit->CommittedActionID.ToString(), *CommittedTag.ToString(),
                        *DescribeChain(Commit->ChainIndex), Commit->ChainStepIndex,
                        Commit->bDispatched ? 1 : 0, Held, DistanceToTarget),
                    /*bAlways=*/false);
                return;
            }
        }
    }
    else if (!CommittedTag.IsNone())
    {
        // The key is set but we hold no commitment — a hand-set stray, or a key
        // written by something outside this service while the attack layer was
        // open. Clear it, or the attack branch's decorator passes forever on a
        // ghost.
        //
        // No longer reachable via branch re-activation, which used to be its
        // main cause: the commitment survives re-entry now, so the ordinary end
        // of an attack goes through the release path above instead of arriving
        // here as an orphan.
        LogTimeline(TEXT("ATTACK-stale-clear"),
            FString::Printf(TEXT("stale=%s"), *CommittedTag.ToString()), /*bAlways=*/true);
        BB->SetValue<UBlackboardKeyType_Name>(AttackKeyID, NAME_None);
    }

    // ── Score both layers ────────────────────────────────────────────────────
    //
    // One scoring pass over the whole array, then partitioned — rather than two
    // filtered passes — so there is exactly one gate breakdown per tick and it
    // covers every entry in authored order. Two passes would either duplicate
    // the diagnostic or make each half of it lie about the other half.
    TArray<int32>   Indices;
    TArray<float>   Scores;
    TArray<FString> GateParts;
    Indices.Reserve(Actions.Num());
    Scores.Reserve(Actions.Num());
    GateParts.Reserve(Actions.Num());

    ScoreEntries(ASC, DistanceToTarget, RecklessFactor, AggressionFactor,
        EPoolFilter::All, Indices, Scores, GateParts);

    TArray<int32> AttackIdx, MoveIdx;
    TArray<float> AttackScores, MoveScores;
    float AttackTotal = 0.f;
    float MoveTotal   = 0.f;

    for (int32 k = 0; k < Indices.Num(); ++k)
    {
        const int32 i = Indices[k];
        if (Actions[i].AbilityTag.IsValid())
        {
            AttackIdx.Add(i);
            AttackScores.Add(Scores[k]);
            AttackTotal += Scores[k];
        }
        else
        {
            MoveIdx.Add(i);
            MoveScores.Add(Scores[k]);
            MoveTotal += Scores[k];
        }
    }

    const float ScaledAttackTotal = AttackTotal * FMath::Max(0.f, AttackLayerWeightScale);
    const float LayerTotal = ScaledAttackTotal + MoveTotal;

    const FString Breakdown = FString::Printf(
        TEXT("dist=%.1f|atk=%.2f(x%.2f)|mov=%.2f|gates=[%s]"),
        DistanceToTarget, AttackTotal, AttackLayerWeightScale, MoveTotal,
        *FString::Join(GateParts, TEXT(" ")));

    // ── Nothing eligible at all ──────────────────────────────────────────────
    // Same reasoning as the combined path: the key is sticky, so leaving it
    // alone means running the last pick forever. Fall back to a movement entry
    // — it has no cooldown to be wrong about and no range gate to violate — or
    // clear the key and let the tree's own Selector fallback take over.
    if (LayerTotal <= 0.f)
    {
        const FGothicWeightedActionEntry* Fallback = Actions.FindByPredicate(
            [this](const FGothicWeightedActionEntry& E)
            {
                return E.ActionID == FallbackActionID && !E.AbilityTag.IsValid();
            });

        const FName NewAction = Fallback ? Fallback->ActionID : NAME_None;

        LogTimeline(TEXT("FALLBACK"),
            FString::Printf(TEXT("'%s'->'%s'|%s"),
                *CurrentAction.ToString(), *NewAction.ToString(), *Breakdown),
            /*bAlways=*/CurrentAction != NewAction);

        if (CurrentAction != NewAction)
        {
            BB->SetValue<UBlackboardKeyType_Name>(ChosenActionKey.GetSelectedKeyID(), NewAction);
            if (Memory)
            {
                Memory->LastDecisionTime = Now;
            }
        }
        return;
    }

    // ── Layer roll ───────────────────────────────────────────────────────────
    // At AttackLayerWeightScale 1.0 this is algebraically the combined roll:
    // P(entry) = P(its layer) * P(entry | layer) = (LayerTotal_L / LayerTotal)
    // * (Score / LayerTotal_L) = Score / LayerTotal. The split changes which
    // KEY the answer is written to and what protects it, not the odds — so a
    // regression here would be a lock bug, never a weighting one.
    const bool bAttackWins = bAttackLayerLive
        && ScaledAttackTotal > 0.f && FMath::FRandRange(0.f, LayerTotal) <= ScaledAttackTotal;

    float RollValue = 0.f;
    float Remainder = 0.f;
    bool  bWasTail  = false;

    if (bAttackWins)
    {
        const int32 Winner = RollWeighted(AttackIdx, AttackScores, AttackTotal,
            RollValue, Remainder, bWasTail);

        if (Actions.IsValidIndex(Winner))
        {
            const FGothicWeightedActionEntry& Entry = Actions[Winner];

            // Does this pick open a chain? Chains are selected by the SAME roll
            // that just happened — a chain is entered by its entry action
            // winning the attack layer, not by a second competing roll — so the
            // weighting is untouched by their existence and an empty Chains
            // array is exactly the pre-phase-3 behaviour.
            const int32 ChainIdx = PickChainForEntry(Entry, DistanceToTarget);

            FName TagName = Entry.AbilityTag.GetTagName();

            if (Chains.IsValidIndex(ChainIdx))
            {
                // The chain's first step replaces the entry's own tag. An entry
                // that opens a chain is a doorway, not a hit.
                const FName StepTag = Chains[ChainIdx].Steps[0].AbilityTag.GetTagName();
                if (!StepTag.IsNone())
                {
                    TagName = StepTag;
                }

                Commit->ChainIndex      = ChainIdx;
                Commit->ChainStepIndex  = 0;
                Commit->ChainEnterTime  = Now;
                Commit->LastStepEndTime = -1.f;

                LogTimeline(TEXT("CHAIN-enter"),
                    FString::Printf(TEXT("chain=%s|steps=%d|via='%s'|step=0|tag=%s|roll=%.3f"),
                        *DescribeChain(ChainIdx), Chains[ChainIdx].Steps.Num(),
                        *Entry.ActionID.ToString(), *TagName.ToString(), RollValue),
                    /*bAlways=*/true);
            }

            LogTimeline(bWasTail ? TEXT("ATTACK-commit-tail") : TEXT("ATTACK-commit"),
                FString::Printf(TEXT("'%s'|tag=%s|chain=%s|roll=%.3f|%s"),
                    *Entry.ActionID.ToString(), *TagName.ToString(),
                    *DescribeChain(Commit->ChainIndex), RollValue, *Breakdown),
                /*bAlways=*/true);

            BB->SetValue<UBlackboardKeyType_Name>(AttackKeyID, TagName);

            Commit->bCommitted        = true;
            Commit->bDispatched       = false;
            Commit->CommitTime        = Now;
            Commit->CommittedActionID = Entry.ActionID;
            Commit->CommittedTagName  = TagName;
        }
        return;
    }

    // ── Movement layer ───────────────────────────────────────────────────────
    // The one legitimate commit window left. A movement entry has no IsActive()
    // signal to hold a decision open with, so without this it would re-roll at
    // 5Hz and flicker between Approach and Reposition instead of walking
    // anywhere. It is a floor on how long a decision lasts, not a lock: an
    // attack that wins the layer roll above never reaches this code.
    if (!CurrentAction.IsNone() && Memory && Memory->LastDecisionTime >= 0.f
        && (Now - Memory->LastDecisionTime) < EffectiveCommitDuration)
    {
        const bool bStillInPool = Actions.ContainsByPredicate(
            [&CurrentAction](const FGothicWeightedActionEntry& E)
            {
                return E.ActionID == CurrentAction && !E.AbilityTag.IsValid();
            });

        if (bStillInPool)
        {
            LogTimeline(TEXT("HOLD-movement-commit"),
                FString::Printf(TEXT("held='%s'|elapsed=%.3f|window=%.3f"),
                    *CurrentAction.ToString(), Now - Memory->LastDecisionTime,
                    EffectiveCommitDuration),
                /*bAlways=*/false);
            return;
        }
    }

    const int32 MoveWinner = RollWeighted(MoveIdx, MoveScores, MoveTotal,
        RollValue, Remainder, bWasTail);

    if (Actions.IsValidIndex(MoveWinner))
    {
        LogTimeline(bWasTail ? TEXT("MOVE-tail") : TEXT("MOVE"),
            FString::Printf(TEXT("'%s'->'%s'|roll=%.3f|%s"),
                *CurrentAction.ToString(), *Actions[MoveWinner].ActionID.ToString(),
                RollValue, *Breakdown),
            /*bAlways=*/CurrentAction != Actions[MoveWinner].ActionID);

        BB->SetValue<UBlackboardKeyType_Name>(
            ChosenActionKey.GetSelectedKeyID(), Actions[MoveWinner].ActionID);

        if (Memory)
        {
            Memory->LastDecisionTime = Now;
        }
    }
}

FString UGothicBTService_WeightedActionSelect::GetStaticDescription() const
{
    TArray<FString> Parts;
    for (const FGothicWeightedActionEntry& Entry : Actions)
    {
        Parts.Add(FString::Printf(TEXT("%s (w=%.1f%s%s)"),
            *Entry.ActionID.ToString(), Entry.BaseWeight,
            Entry.RecklessnessWeightBonus != 0.f ? TEXT(", +reckless") : TEXT(""),
            Entry.AggressionWeightBonus != 0.f ? TEXT(", +aggression") : TEXT("")));
    }
    return FString::Printf(TEXT("%s\nPool: %s%s"),
        *Super::GetStaticDescription(),
        Parts.Num() > 0 ? *FString::Join(Parts, TEXT(", ")) : TEXT("(empty)"),
        bSplitAttackPool
            ? *FString::Printf(TEXT("\nSPLIT: attacks -> %s (locked until cleared), movement -> %s"),
                ChosenAttackTagKey.IsNone() ? TEXT("(UNSET!)") : *ChosenAttackTagKey.SelectedKeyName.ToString(),
                ChosenActionKey.IsNone() ? TEXT("(unset)") : *ChosenActionKey.SelectedKeyName.ToString())
            : TEXT(""));
}