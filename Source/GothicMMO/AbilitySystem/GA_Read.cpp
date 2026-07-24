// GA_Read.cpp

#include "AbilitySystem/GA_Read.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

UGA_Read::UGA_Read()
{
    // Instant self-buff, no per-activation state to hold between shots.
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
}

void UGA_Read::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    if (!ASC || !ReadStateEffect)
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_Read: missing ASC or ReadStateEffect — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    // Apply the buff and end immediately. The GameplayEffect owns the duration,
    // so State.Read lingers after this activation returns and auto-expires — no
    // trace, no channel, no manual timer (cf. Reckoning, which needs a timer only
    // because Not At All can extend it).
    FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
    FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(ReadStateEffect, 1.f, Context);
    if (Spec.IsValid())
    {
        ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    }

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
