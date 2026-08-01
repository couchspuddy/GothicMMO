// GothicAbilitySystemComponent.cpp

#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"

UGothicAbilitySystemComponent::UGothicAbilitySystemComponent()
{
    SetIsReplicated(true);
    ReplicationMode = EGameplayEffectReplicationMode::Full;
}

void UGothicAbilitySystemComponent::GrantStartupAbilities(
    const TArray<TSubclassOf<UGothicGameplayAbility>>& AbilitiesToGrant,
    int32 Level)
{
    if (!GetOwnerActor() || !GetOwnerActor()->HasAuthority())
    {
        return;
    }


    for (TSubclassOf<UGothicGameplayAbility> AbilityClass : AbilitiesToGrant)
    {
        if (!AbilityClass)
        {
            continue;
        }

        FGameplayAbilitySpec Spec(AbilityClass, Level);
        const UGothicGameplayAbility* AbilityCDO = AbilityClass.GetDefaultObject();
        Spec.GetDynamicSpecSourceTags().AddTag(AbilityCDO->GetAbilityInputTag());

        FGameplayAbilitySpecHandle Handle = GiveAbility(Spec);
        SlotToAbilityMap.Add(AbilityCDO->GetAbilitySlot(), Handle);

    }

}

bool UGothicAbilitySystemComponent::TryActivateAbilityBySlot(EGothicAbilitySlot Slot)
{

    if (const FGameplayAbilitySpecHandle* Handle = SlotToAbilityMap.Find(Slot))
    {
        return TryActivateAbility(*Handle);
    }

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

float UGothicAbilitySystemComponent::GetCooldownTotalForSlot(EGothicAbilitySlot Slot) const
{
    // Read the designed duration from the cooldown GE's CDO — not from the
    // active effect instance. The old implementation queried
    // GetActiveEffectsDuration, which only returns a value while the cooldown
    // is running. Every frame the ability is off cooldown, Total arrived as
    // 0.f, producing a divide-by-zero in the HUD's Remaining/Total math.
    //
    // The total duration is a property of the design, not the runtime state.
    // GetCooldownGameplayEffect() returns the CDO of whatever GE class is set
    // in the Blueprint's CooldownGameplayEffectClass field.
    //
    // That fix was only half of one. GetStaticMagnitudeIfPossible fails for a
    // SetByCaller duration, and the ability that matters most here — GA_Fire,
    // whose interval is the equipped weapon's fire rate, further modified by
    // AbilityHaste — is exactly that case. It fell through to the active-effect
    // query, which returns nothing while the ability is READY, so Total came
    // back 0.f every frame Fire was off cooldown: the same wrong answer the
    // comment above claims to have retired.
    //
    // So the query result is now cached. While a cooldown runs we learn its full
    // duration and remember it; once it ends we answer from the cache instead of
    // zero. First-ever call before the ability has been used still returns 0 —
    // unavoidable without asking the ability to compute its own duration, which
    // would mean a virtual on UGothicGameplayAbility and an override in GA_Fire.

    if (const FGameplayAbilitySpecHandle* Handle = SlotToAbilityMap.Find(Slot))
    {
        if (const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(*Handle))
        {
            const UGothicGameplayAbility* AbilityCDO = Cast<UGothicGameplayAbility>(Spec->Ability);
            if (AbilityCDO)
            {
                const UGameplayEffect* CooldownGE = AbilityCDO->GetCooldownGameplayEffect();
                if (CooldownGE)
                {
                    float DesignedDuration = 0.f;
                    if (CooldownGE->DurationMagnitude.GetStaticMagnitudeIfPossible(
                            1.f, DesignedDuration))
                    {
                        return DesignedDuration;
                    }

                    // If the magnitude isn't a static float (SetByCaller, curve,
                    // custom calc), fall back to querying the active instance —
                    // better than zero, and only abilities with non-static
                    // cooldowns would reach this path.
                    const FGameplayTagContainer* CooldownTags = AbilityCDO->GetCooldownTags();
                    if (CooldownTags && CooldownTags->Num() > 0)
                    {
                        FGameplayEffectQuery Query =
                            FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(*CooldownTags);
                        TArray<float> Durations = GetActiveEffectsDuration(Query);
                        if (Durations.Num() > 0)
                        {
                            float MaxDuration = 0.f;
                            for (float D : Durations)
                            {
                                MaxDuration = FMath::Max(MaxDuration, D);
                            }

                            // Learn it while we can see it.
                            LastObservedCooldownTotals.Add(Slot, MaxDuration);
                            return MaxDuration;
                        }

                        // Off cooldown: nothing to query. Answer with the last
                        // duration this slot actually ran, if we ever saw one.
                        if (const float* Cached = LastObservedCooldownTotals.Find(Slot))
                        {
                            return *Cached;
                        }
                    }
                }
            }
        }
    }
    return 0.f;
}

void UGothicAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
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
    SlotToAbilityMap.Add(Slot, Handle);
}

FGameplayEffectContextHandle UGothicAbilitySystemComponent::MakeDamageContext(
    UAbilitySystemComponent* SourceASC,
    AActor* SourceAvatar)
{
    if (!SourceASC)
    {
        return FGameplayEffectContextHandle();
    }

    FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();

    if (SourceAvatar)
    {
        Context.AddSourceObject(SourceAvatar);

        // Both slots are the avatar: instigator drives AttackPower and the
        // Killer attribution, effect causer drives hit feedback. See the header.
        Context.AddInstigator(SourceAvatar, SourceAvatar);
    }

    return Context;
}

void UGothicAbilitySystemComponent::ApplyEffectToASC(
    UAbilitySystemComponent* TargetASC,
    TSubclassOf<UGameplayEffect> EffectClass,
    AActor* SourceActor)
{
    if (!TargetASC || !EffectClass) return;

    // The context has to be built from the SOURCE. This used to call
    // TargetASC->MakeEffectContext() and apply to self, which named the VICTIM as
    // its own instigator: any GE routed through here that reads source AttackPower
    // or attribution would have read the target's. Inert only because the one
    // caller's GE (GE_Stun_Shock) has no modifiers.
    UAbilitySystemComponent* SourceASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(SourceActor);

    if (SourceASC)
    {
        FGameplayEffectContextHandle Context = MakeDamageContext(SourceASC, SourceActor);
        FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
        if (Spec.IsValid())
        {
            SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
        }
        return;
    }

    // No source ASC to name — every caller today passes a real pawn avatar
    // (GA_Fire, GA_BestialLucidRoar, GA_BestialLucidCry, GA_FeralBreakout), so
    // this is the "environment applied it" path rather than a normal one. Fall
    // back to the self-application shape, but say so rather than pretending the
    // target instigated it.
    UE_LOG(LogTemp, Warning,
        TEXT("ApplyEffectToASC: no ASC on source %s — applying %s with no instigator"),
        SourceActor ? *SourceActor->GetName() : TEXT("null"),
        *EffectClass->GetName());

    FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
    Context.AddSourceObject(SourceActor);

    FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
    if (Spec.IsValid())
    {
        TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    }
}