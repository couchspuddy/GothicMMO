// GothicBTService_WeightedActionSelect.cpp

#include "AI/GothicBTService_WeightedActionSelect.h"
#include "AIController.h"
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
    }
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
                    : MinMovementCommitDuration);

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
                if (IsAbilityActive(ASC, CurrentEntry->AbilityTag) || bWithinCommitWindow)
                {
                    return;
                }
            }
            else if (bWithinCommitWindow)
            {
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
            Score = FMath::Max(0.f, Entry.BaseWeight + Entry.RecklessnessWeightBonus * RecklessFactor);
        }

        Scores.Add(Score);
        TotalWeight += Score;
    }

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
    for (int32 i = 0; i < Actions.Num(); ++i)
    {
        if (Scores[i] <= 0.f)
        {
            continue;
        }

        if (Roll <= Scores[i])
        {
            BB->SetValue<UBlackboardKeyType_Name>(ChosenActionKey.GetSelectedKeyID(), Actions[i].ActionID);
            if (Memory)
            {
                Memory->LastDecisionTime = Now;
            }
            return;
        }

        Roll -= Scores[i];
    }
}

FString UGothicBTService_WeightedActionSelect::GetStaticDescription() const
{
    TArray<FString> Parts;
    for (const FGothicWeightedActionEntry& Entry : Actions)
    {
        Parts.Add(FString::Printf(TEXT("%s (w=%.1f%s)"),
            *Entry.ActionID.ToString(), Entry.BaseWeight,
            Entry.RecklessnessWeightBonus != 0.f ? TEXT(", +reckless") : TEXT("")));
    }
    return FString::Printf(TEXT("%s\nPool: %s"),
        *Super::GetStaticDescription(),
        Parts.Num() > 0 ? *FString::Join(Parts, TEXT(", ")) : TEXT("(empty)"));
}