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

    // Same validation philosophy as CombatSync: catch an AssetTag that
    // matches nothing at startup, loudly, instead of a permanently-zero-weight
    // entry nobody notices until the fight feels thinner than it should.
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
                TEXT("WeightedActionSelect[%s]: entry '%s' AssetTag '%s' matches no granted ability. "
                     "Check AssetTags on the ability, not AbilityInputTag."),
                *GetNameSafe(Pawn), *Entry.ActionID.ToString(), *Entry.AbilityTag.ToString());
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
            if (CurrentEntry->AbilityTag.IsValid())
            {
                if (IsAbilityActive(ASC, CurrentEntry->AbilityTag))
                {
                    return;
                }
            }
            else if (Memory && Memory->LastDecisionTime >= 0.f
                && (Now - Memory->LastDecisionTime) < MinMovementCommitDuration)
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

    const float DistanceToTarget = Target
        ? FVector::Dist(Pawn->GetActorLocation(), Target->GetActorLocation())
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

    // Nothing eligible — leave ChosenAction untouched rather than force a
    // bad pick. The tree's own fallback (Move To, same as the boss's root
    // Selector already does) takes over gracefully.
    if (TotalWeight <= 0.f)
    {
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