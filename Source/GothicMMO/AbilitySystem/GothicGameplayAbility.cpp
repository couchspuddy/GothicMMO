// GothicGameplayAbility.cpp

#include "AbilitySystem/GothicGameplayAbility.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AI/GothicEnemyBase.h"

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
    float DamageValue,
    FVector ImpactPoint,
    bool bWasVital)
{
    if (!Target || !DamageEffect)
    {
        return;
    }

    UAbilitySystemComponent* TargetASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
    UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();

    if (!TargetASC || !SourceASC)
    {
        return;
    }

    AActor* Avatar = GetAvatarActorFromActorInfo();

    // Context carries both source object and instigator so the damage
    // pipeline (and its Killer attribution) always knows who hit them.
    FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
    Context.AddSourceObject(Avatar);
    Context.AddInstigator(GetOwningActorFromActorInfo(), Avatar);

    FGameplayEffectSpecHandle SpecHandle =
        SourceASC->MakeOutgoingSpec(DamageEffect, GetAbilityLevel(), Context);

    if (!SpecHandle.IsValid())
    {
        return;
    }

    SpecHandle.Data->SetSetByCallerMagnitude(GothicTags::Data_Damage, DamageValue);

    SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);

    // Hit feedback for every client — centralized so a new damage site can't
    // be silent by omission. No-op location fallback keeps callers that don't
    // have a precise impact point (melee sweeps) honest enough for VFX.
    if (AGothicEnemyBase* HitEnemy = Cast<AGothicEnemyBase>(Target))
    {
        const FVector FeedbackPoint =
            ImpactPoint.IsNearlyZero() ? Target->GetActorLocation() : ImpactPoint;
        HitEnemy->MulticastOnHit(FeedbackPoint, bWasVital);
    }
}