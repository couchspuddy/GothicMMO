// GA_Reckoning.cpp

#include "AbilitySystem/GA_Reckoning.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "TimerManager.h"
#include "Engine/World.h"

UGA_Reckoning::UGA_Reckoning()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Reckoning::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    CachedASC = GetAbilitySystemComponentFromActorInfo();

    if (!CachedASC || !ReckoningStateEffect)
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_Reckoning: Missing ASC or ReckoningStateEffect — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))   // <-- new
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_Reckoning: CommitAbility failed — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }
    FGameplayEffectContextHandle Context = CachedASC->MakeEffectContext();
    FGameplayEffectSpecHandle Spec = CachedASC->MakeOutgoingSpec(ReckoningStateEffect, 1.f, Context);

    if (Spec.IsValid())
    {
        ActiveReckoningHandle = CachedASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    }

    RemainingDuration = BaseDuration;
    RestartDurationTimer();
}

void UGA_Reckoning::RestartDurationTimer()
{
    GetWorld()->GetTimerManager().ClearTimer(DurationTimerHandle);
    GetWorld()->GetTimerManager().SetTimer(
        DurationTimerHandle,
        this,
        &UGA_Reckoning::OnDurationExpired,
        RemainingDuration,
        false);
}

void UGA_Reckoning::ExtendReckoningDuration(float ExtensionAmount)
{
    if (!GetWorld() || !GetWorld()->GetTimerManager().IsTimerActive(DurationTimerHandle))
    {
        // Reckoning isn't currently active — no-op
        return;
    }

    const float CurrentRemaining = GetWorld()->GetTimerManager().GetTimerRemaining(DurationTimerHandle);
    const float ElapsedSoFar = BaseDuration - CurrentRemaining;
    const float NewRemaining = FMath::Min(CurrentRemaining + ExtensionAmount,
        MaxExtendedDuration - ElapsedSoFar);

    if (NewRemaining > 0.f)
    {
        RemainingDuration = NewRemaining;
        RestartDurationTimer();

    }
}

void UGA_Reckoning::OnDurationExpired()
{
    EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
        GetCurrentActivationInfo(), true, false);
}

void UGA_Reckoning::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(DurationTimerHandle);
    }

    if (CachedASC && ActiveReckoningHandle.IsValid())
    {
        CachedASC->RemoveActiveGameplayEffect(ActiveReckoningHandle);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
