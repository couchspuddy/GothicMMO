// GA_TheLovedAndTheLost.cpp

#include "AbilitySystem/GA_TheLovedandTheLost.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "TimerManager.h"
#include "Engine/World.h"

UGA_TheLovedAndTheLost::UGA_TheLovedAndTheLost()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_TheLovedAndTheLost::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    CachedASC = GetAbilitySystemComponentFromActorInfo();

    if (!CachedASC || !InCombatTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_TheLovedAndTheLost: Missing ASC or InCombatTag — this passive will not function"));
        return;
    }

    // Bind to combat tag changes for the lifetime of this passive
    CombatTagDelegateHandle = CachedASC->RegisterGameplayTagEvent(
        InCombatTag, EGameplayTagEventType::NewOrRemoved)
        .AddUObject(this, &UGA_TheLovedAndTheLost::OnCombatTagChanged);

    // If already in combat at grant time, start ramping immediately
    if (CachedASC->HasMatchingGameplayTag(InCombatTag))
    {
        StartRamping();
    }

}

void UGA_TheLovedAndTheLost::OnCombatTagChanged(const FGameplayTag Tag, int32 NewCount)
{
    if (NewCount > 0)
    {
        StartRamping();
    }
    else
    {
        StopRamping();
    }
}

void UGA_TheLovedAndTheLost::StartRamping()
{
    if (!RampEffect || !CachedASC)
    {
        return;
    }

    CurrentRampTime = 0.f;

    if (!ActiveRampHandle.IsValid())
    {
        FGameplayEffectContextHandle Context = CachedASC->MakeEffectContext();
        FGameplayEffectSpecHandle Spec = CachedASC->MakeOutgoingSpec(RampEffect, 1.f, Context);

        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(
                GothicTags::Data_RampMagnitude, 0.f);

            ActiveRampHandle = CachedASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        }
    }

    GetWorld()->GetTimerManager().SetTimer(
        RampTickHandle, this, &UGA_TheLovedAndTheLost::TickRamp,
        RampTickInterval, true);

}

void UGA_TheLovedAndTheLost::TickRamp()
{
    CurrentRampTime = FMath::Min(CurrentRampTime + RampTickInterval, RampUpDuration);
    const float Alpha = RampUpDuration > 0.f ? (CurrentRampTime / RampUpDuration) : 1.f;
    const float NewMagnitude = MaxRampMagnitude * Alpha;

    if (CachedASC && ActiveRampHandle.IsValid())
    {
        // Update the SetByCaller magnitude on the active effect's spec.
        // Simplest robust approach: remove and reapply with new magnitude.
        CachedASC->RemoveActiveGameplayEffect(ActiveRampHandle);

        FGameplayEffectContextHandle Context = CachedASC->MakeEffectContext();
        FGameplayEffectSpecHandle Spec = CachedASC->MakeOutgoingSpec(RampEffect, 1.f, Context);
        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(
                GothicTags::Data_RampMagnitude, NewMagnitude);
            ActiveRampHandle = CachedASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        }
    }

    if (CurrentRampTime >= RampUpDuration)
    {
        GetWorld()->GetTimerManager().ClearTimer(RampTickHandle);
    }
}

void UGA_TheLovedAndTheLost::StopRamping()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(RampTickHandle);
    }

    if (CachedASC && ActiveRampHandle.IsValid())
    {
        CachedASC->RemoveActiveGameplayEffect(ActiveRampHandle);
        ActiveRampHandle.Invalidate();
    }

    CurrentRampTime = 0.f;

}

void UGA_TheLovedAndTheLost::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    StopRamping();

    if (CachedASC && CombatTagDelegateHandle.IsValid())
    {
        CachedASC->RegisterGameplayTagEvent(InCombatTag, EGameplayTagEventType::NewOrRemoved)
            .Remove(CombatTagDelegateHandle);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
