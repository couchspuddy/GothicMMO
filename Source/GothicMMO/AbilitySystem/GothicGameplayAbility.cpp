// GothicGameplayAbility.cpp

#include "AbilitySystem/GothicGameplayAbility.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"

UGothicGameplayAbility::UGothicGameplayAbility()
{
    // Default instancing: one instance per execution (safer for networking).
    // Switch to InstancedPerActor if the ability needs persistent state between
    // activations (e.g., a channeled beam ability tracking a hold timer).
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;

    // Default: replicate the ability activation to all clients so they
    // can play VFX/SFX client-side via GameplayCues.
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
}

void UGothicGameplayAbility::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    // Always call Super so GAS internal bookkeeping runs.
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    // Blueprint children override K2_ActivateAbility (exposed by UGameplayAbility).
    // This C++ path is for abilities implemented entirely in C++.
}

UGothicAbilitySystemComponent* UGothicGameplayAbility::GetGothicASC() const
{
    return Cast<UGothicAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
}

void UGothicGameplayAbility::ApplyDamageToTarget(
    AActor* Target,
    TSubclassOf<UGameplayEffect> DamageEffect,
    float DamageMultiplier)
{
    if (!Target || !DamageEffect)
    {
        return;
    }

    UAbilitySystemComponent* TargetASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);

    if (!TargetASC)
    {
        return;
    }

    // Build an effect context so the target knows who hit them.
    FGameplayEffectContextHandle Context = GetAbilitySystemComponentFromActorInfo()->MakeEffectContext();
    Context.AddSourceObject(GetAvatarActorFromActorInfo());

    // Create a spec from the damage effect class at this ability's level.
    FGameplayEffectSpecHandle SpecHandle =
        GetAbilitySystemComponentFromActorInfo()->MakeOutgoingSpec(
            DamageEffect,
            GetAbilityLevel(),
            Context);

    if (SpecHandle.IsValid())
    {
        // Pass the multiplier as a "Set By Caller" magnitude.
        // In your GE_MeleeDamage, set the "IncomingDamage" modifier to use
        // SetByCaller with tag "Data.Damage". This lets us reuse one GE class
        // for light and heavy attacks just by changing the multiplier.
        UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(
            SpecHandle,
            FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
            DamageMultiplier);

        GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(
            *SpecHandle.Data.Get(),
            TargetASC);
    }
}
