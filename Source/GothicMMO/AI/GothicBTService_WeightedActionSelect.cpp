// GothicBTService_WeightedActionSelect.cpp

#include "AI/GothicBTService_WeightedActionSelect.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AI/GothicBossArenaManager.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "Character/GothicCharacterBase.h"
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
}

void UGothicBTService_WeightedActionSelect::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);

    if (UBlackboardData* BBAsset = GetBlackboardAsset())
    {
        ChosenActionKey.ResolveSelectedKey(*BBAsset);
        TargetActorKey.ResolveSelectedKey(*BBAsset);
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

    // Recklessness bias: (1 - HealthPercent), 0 at full health, 1 at death.
    // Reads straight off the character's own attributes — no Blackboard float
    // key needed, one less thing to wire per BT asset.
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

    // Score every entry, skip anything not ready/out of range, accumulate a
    // running total for the weighted roll below.
    TArray<float> Scores;
    Scores.Reserve(Actions.Num());
    float TotalWeight = 0.f;

    // Diagnostic: per-entry gate verdicts, so a pick line also says what the
    // pick was made FROM. An entry that is permanently `r0` while its readiness
    // key reads true in the CombatSync stream is the two services disagreeing;
    // an entry that is `r1 R1` on ticks where it never wins is just the roll.
    TArray<FString> GateParts;
    GateParts.Reserve(Actions.Num());

    for (const FGothicWeightedActionEntry& Entry : Actions)
    {
        float Score = 0.f;

        const bool bReady = !Entry.AbilityTag.IsValid() || IsAbilityReady(ASC, Entry.AbilityTag);
        const bool bInRange =
            DistanceToTarget < 0.f
            || ((Entry.MinRange < 0.f || DistanceToTarget >= Entry.MinRange)
                && (Entry.MaxRange < 0.f || DistanceToTarget <= Entry.MaxRange));

        if (bReady && bInRange)
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

        Scores.Add(Score);
        TotalWeight += Score;

        GateParts.Add(FString::Printf(TEXT("%s:r%d,R%d,w%.2f"),
            *Entry.ActionID.ToString(), bReady ? 1 : 0, bInRange ? 1 : 0, Score));
    }

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

    float Roll = FMath::FRandRange(0.f, TotalWeight);

    // Captured before the loop consumes it — the log wants the roll as drawn.
    const float InitialRoll = Roll;

    // LastEligible is the fallthrough answer, and it is not defensive padding.
    // TotalWeight is accumulated by repeated float addition while Roll is drawn
    // against that sum, so on the last entry the running remainder can sit a few
    // ULPs above Scores[i] purely from rounding. The loop then exits WITHOUT
    // writing — and ChosenAction is sticky, so the enemy silently keeps running
    // whatever branch it picked last time. That is the same "frozen key" failure
    // the TotalWeight <= 0 case above was rewritten to kill, arriving by a
    // narrower door. Landing on the last eligible entry is exactly what an
    // exact-arithmetic roll of TotalWeight would have selected.
    int32 LastEligible = INDEX_NONE;

    for (int32 i = 0; i < Actions.Num(); ++i)
    {
        if (Scores[i] <= 0.f)
        {
            continue;
        }

        LastEligible = i;

        if (Roll <= Scores[i])
        {
            LogTimeline(TEXT("PICK"),
                FString::Printf(TEXT("'%s'->'%s'|roll=%.3f|%s"),
                    *CurrentAction.ToString(), *Actions[i].ActionID.ToString(),
                    InitialRoll, *Breakdown),
                /*bAlways=*/CurrentAction != Actions[i].ActionID);

            BB->SetValue<UBlackboardKeyType_Name>(ChosenActionKey.GetSelectedKeyID(), Actions[i].ActionID);
            if (Memory)
            {
                Memory->LastDecisionTime = Now;
            }
            return;
        }

        Roll -= Scores[i];
    }

    if (Actions.IsValidIndex(LastEligible))
    {
        LogTimeline(TEXT("PICK-tail"),
            FString::Printf(TEXT("'%s'->'%s'|roll=%.3f|remainder=%.6f|%s"),
                *CurrentAction.ToString(), *Actions[LastEligible].ActionID.ToString(),
                InitialRoll, Roll, *Breakdown),
            /*bAlways=*/CurrentAction != Actions[LastEligible].ActionID);

        BB->SetValue<UBlackboardKeyType_Name>(
            ChosenActionKey.GetSelectedKeyID(), Actions[LastEligible].ActionID);
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
    return FString::Printf(TEXT("%s\nPool: %s"),
        *Super::GetStaticDescription(),
        Parts.Num() > 0 ? *FString::Join(Parts, TEXT(", ")) : TEXT("(empty)"));
}