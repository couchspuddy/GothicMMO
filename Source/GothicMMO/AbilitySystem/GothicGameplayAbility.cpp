// GothicGameplayAbility.cpp

#include "AbilitySystem/GothicGameplayAbility.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AI/GothicEnemyBase.h"
#include "AI/AnimNotifyState_MeleeHitbox.h"
#include "AI/GothicMeleeHitboxComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"

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

// ── Montage lifecycle ───────────────────────────────────────────────────────

bool UGothicGameplayAbility::PlayOptionalMontage()
{
    if (!MontageToPlay)
    {
        return false;
    }

    UAbilityTask_PlayMontageAndWait* MontageTask =
        UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
            this, NAME_None, MontageToPlay, MontagePlayRate);

    MontageTask->OnCompleted.AddDynamic(this, &UGothicGameplayAbility::OnMontageEnd);
    MontageTask->OnBlendOut.AddDynamic(this, &UGothicGameplayAbility::OnMontageEnd);
    MontageTask->OnInterrupted.AddDynamic(this, &UGothicGameplayAbility::OnMontageCancel);
    MontageTask->OnCancelled.AddDynamic(this, &UGothicGameplayAbility::OnMontageCancel);

    MontageTask->ReadyForActivation();

    // Listen for the hit window anim notify.
    // The montage needs an AnimNotify_SendGameplayEvent at the swing
    // contact frame, sending tag Event.Montage.HitWindow.
    UAbilityTask_WaitGameplayEvent* EventTask =
        UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
            this,
            FGameplayTag::RequestGameplayTag(FName("Event.Montage.HitWindow")));

    EventTask->EventReceived.AddDynamic(this, &UGothicGameplayAbility::OnMontageHitWindow);
    EventTask->ReadyForActivation();


    return true;
}

void UGothicGameplayAbility::OnMontageHitWindow(FGameplayEventData Payload)
{
    // Default does nothing. Derived classes override to do damage,
    // AOE stun, etc. at the moment the animation connects.
}

void UGothicGameplayAbility::OnMontageEnd()
{
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGothicGameplayAbility::OnMontageCancel()
{
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

bool UGothicGameplayAbility::IsHitboxDrivenMelee() const
{
    if (CachedHitboxDrivenMelee >= 0)
    {
        return CachedHitboxDrivenMelee != 0;
    }

    CachedHitboxDrivenMelee = 0;

    if (!MontageToPlay)
    {
        return false;
    }

    // Enemies only. The player's abilities damage through traces they own and
    // must never be second-guessed by this.
    const AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(GetAvatarActorFromActorInfo());
    if (!Enemy || !Enemy->GetMeleeHitbox())
    {
        return false;
    }

    for (const FAnimNotifyEvent& Notify : MontageToPlay->Notifies)
    {
        if (Notify.NotifyStateClass
            && Notify.NotifyStateClass->IsA(UAnimNotifyState_MeleeHitbox::StaticClass()))
        {
            CachedHitboxDrivenMelee = 1;

            UE_LOG(LogTemp, Verbose,
                TEXT("%s on %s: montage '%s' drives the melee hitbox — graph-side direct damage suppressed"),
                *GetName(), *Enemy->GetName(), *MontageToPlay->GetName());
            break;
        }
    }

    return CachedHitboxDrivenMelee != 0;
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

    // The swing's hitbox is already the authority on who this attack hits and
    // from how far. Applying here as well is the second helping of damage the
    // Claw was serving from ~430uu away. See the header.
    if (!bUseDirectDamageFallback || IsHitboxDrivenMelee())
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
        HitEnemy->MulticastOnHit(FeedbackPoint, bWasVital, DamageValue);
    }
}