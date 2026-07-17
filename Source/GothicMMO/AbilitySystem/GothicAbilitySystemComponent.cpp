// GothicAbilitySystemComponent.cpp

#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayAbility.h"

UGothicAbilitySystemComponent::UGothicAbilitySystemComponent()
{
    SetIsReplicated(true);
    ReplicationMode = EGameplayEffectReplicationMode::Full;
}

FActiveGameplayEffectHandle UGothicAbilitySystemComponent::ApplyEffectToASC(
    UAbilitySystemComponent* ASC,
    TSubclassOf<UGameplayEffect> EffectClass,
    UObject* SourceObject,
    FGameplayTag SetByCallerTag,
    float SetByCallerValue,
    float Level)
{
    if (!ASC || !EffectClass)
    {
        return FActiveGameplayEffectHandle();
    }

    FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
    if (SourceObject)
    {
        Context.AddSourceObject(SourceObject);
    }

    FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, Level, Context);
    if (!Spec.IsValid())
    {
        return FActiveGameplayEffectHandle();
    }

    if (SetByCallerTag.IsValid())
    {
        Spec.Data->SetSetByCallerMagnitude(SetByCallerTag, SetByCallerValue);
    }

    return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

void UGothicAbilitySystemComponent::GrantStartupAbilities(
    const TArray<TSubclassOf<UGothicGameplayAbility>>& AbilitiesToGrant,
    int32 Level)
{
    if (!GetOwnerActor() || !GetOwnerActor()->HasAuthority())
    {
        UE_LOG(LogTemp, Log, TEXT("GrantStartupAbilities: No authority"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("GrantStartupAbilities: Granting %d abilities"), AbilitiesToGrant.Num());

    for (TSubclassOf<UGothicGameplayAbility> AbilityClass : AbilitiesToGrant)
    {
        if (!AbilityClass)
        {
            UE_LOG(LogTemp, Log, TEXT("GrantStartupAbilities: Null ability class skipped"));
            continue;
        }

        FGameplayAbilitySpec Spec(AbilityClass, Level);
        const UGothicGameplayAbility* AbilityCDO = AbilityClass.GetDefaultObject();
        Spec.GetDynamicSpecSourceTags().AddTag(AbilityCDO->GetAbilityInputTag());

        FGameplayAbilitySpecHandle Handle = GiveAbility(Spec);
        SlotToAbilityMap.Add(AbilityCDO->GetAbilitySlot(), Handle);

        UE_LOG(LogTemp, Log, TEXT("Granted ability: %s to slot %d"),
            *AbilityClass->GetName(),
            (int32)AbilityCDO->GetAbilitySlot());
    }

    UE_LOG(LogTemp, Log, TEXT("SlotToAbilityMap now has %d entries"), SlotToAbilityMap.Num());
}

bool UGothicAbilitySystemComponent::TryActivateAbilityBySlot(EGothicAbilitySlot Slot)
{
    UE_LOG(LogTemp, Log, TEXT("TryActivateAbilityBySlot: Slot %d | Map size: %d"), 
        (int32)Slot, SlotToAbilityMap.Num());

    if (const FGameplayAbilitySpecHandle* Handle = SlotToAbilityMap.Find(Slot))
    {
        UE_LOG(LogTemp, Log, TEXT("Handle found for slot %d"), (int32)Slot);
        return TryActivateAbility(*Handle);
    }

    UE_LOG(LogTemp, Log, TEXT("No handle found for slot %d"), (int32)Slot);
    return false;
}

float UGothicAbilitySystemComponent::GetCooldownRemainingForSlot(EGothicAbilitySlot Slot) const
{
    if (const FGameplayAbilitySpecHandle* Handle = SlotToAbilityMap.Find(Slot))
    {
        if (const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(*Handle))
        {
            const UGothicGameplayAbility* AbilityCDO = Cast<UGothicGameplayAbility>(Spec->Ability);
            if (AbilityCDO)
            {
                // Get cooldown tags from the CDO safely — GetCooldownTags() is safe on CDO
                const FGameplayTagContainer* CooldownTags = AbilityCDO->GetCooldownTags();
                if (CooldownTags && CooldownTags->Num() > 0)
                {
                    // Query active effects on THIS ASC directly — no CDO instance methods
                    FGameplayEffectQuery Query = 
                        FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(*CooldownTags);
                    
                    TArray<float> Durations = GetActiveEffectsTimeRemaining(Query);
                    
                    if (Durations.Num() > 0)
                    {
                        float MaxDuration = 0.f;
                        for (float D : Durations)
                        {
                            MaxDuration = FMath::Max(MaxDuration, D);
                        }
                        return MaxDuration;
                    }
                }
            }
        }
    }
    return 0.f;
}

void UGothicAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
    // ">>> AbilityInputTagPressed" debug log stripped July 17 — it was on the
    // housekeeping strip list and fired on every ability keypress.
    ABILITYLIST_SCOPE_LOCK();
    for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
    {
        // UE5.8: Use GetDynamicSpecSourceTags()
        if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
        {
            Spec.InputPressed = true;
            if (Spec.IsActive())
            {
                AbilitySpecInputPressed(Spec);
            }
            else
            {
                TryActivateAbility(Spec.Handle);
            }
        }
    }
}

void UGothicAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
    ABILITYLIST_SCOPE_LOCK();
    for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
    {
        // UE5.8: Use GetDynamicSpecSourceTags()
        if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
        {
            Spec.InputPressed = false;
            if (Spec.IsActive())
            {
                AbilitySpecInputReleased(Spec);
            }
        }
    }
}

void UGothicAbilitySystemComponent::RegisterAbilitySlot(
    EGothicAbilitySlot Slot,
    FGameplayAbilitySpecHandle Handle)
{
    UE_LOG(LogTemp, Log, TEXT("RegisterAbilitySlot: Slot %d registered"), (int32)Slot);
    SlotToAbilityMap.Add(Slot, Handle);
}