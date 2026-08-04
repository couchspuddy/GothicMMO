// GA_Reckoning.cpp

#include "AbilitySystem/GA_Reckoning.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystem/GothicGameplayTags.h"   // Perk.Weapon.VerbB.SpentWell
#include "AI/GothicSteadfastComponent.h"
#include "Character/GothicPlayerCharacter.h"
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

    // GE_SuperCost drained the meter to zero as the activation gate. Reckoning
    // spends it visibly instead of instantly: refill to full now, then bleed it
    // back down over the duration so the super bar doubles as the Reckoning
    // timer. TickSuperDrain re-reads the remaining time each tick, so a Not At
    // All extension pushes the bar back up on its own.
    const float MaxSuper = CachedASC->GetNumericAttribute(
        UGothicAttributeSet::GetMaxSuperMeterAttribute());
    CachedASC->SetNumericAttributeBase(
        UGothicAttributeSet::GetSuperMeterAttribute(), MaxSuper);

    // Spent Well — "Covenant activation instantly refills Steadfast to full"
    // (WEAPON_PERK_TABLES.md, Verb Bucket B). Here rather than in the Steadfast
    // component because activation is the trigger, and after CommitAbility so a
    // refused activation never pays out.
    //
    // The perk is read off the ACTIVE weapon, and the Steadfast meter is global —
    // the doc's Steadfast-scope note is what makes this legal on a Heavy Melee
    // Rig, which owns no ammo but can still carry the perk.
    if (AGothicPlayerCharacter* Char =
            Cast<AGothicPlayerCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
    {
        if (Char->HasWeaponPerk(GothicTags::Perk_Weapon_VerbB_SpentWell))
        {
            if (UGothicSteadfastComponent* Steadfast =
                    Char->FindComponentByClass<UGothicSteadfastComponent>())
            {
                Steadfast->RefillToFull();
            }
        }
    }

    GetWorld()->GetTimerManager().SetTimer(
        DrainTimerHandle, this, &UGA_Reckoning::TickSuperDrain, DrainTickInterval, true);
}

void UGA_Reckoning::TickSuperDrain()
{
    if (!CachedASC || !GetWorld())
    {
        return;
    }

    const float MaxSuper = CachedASC->GetNumericAttribute(
        UGothicAttributeSet::GetMaxSuperMeterAttribute());
    const float Remaining =
        GetWorld()->GetTimerManager().GetTimerRemaining(DurationTimerHandle);
    const float Frac = BaseDuration > 0.f
        ? FMath::Clamp(Remaining / BaseDuration, 0.f, 1.f)
        : 0.f;

    CachedASC->SetNumericAttributeBase(
        UGothicAttributeSet::GetSuperMeterAttribute(), MaxSuper * Frac);
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
        GetWorld()->GetTimerManager().ClearTimer(DrainTimerHandle);
    }

    // Reckoning is over — the meter is spent. Zero it so it starts recharging
    // from empty via damage and kills.
    if (CachedASC)
    {
        CachedASC->SetNumericAttributeBase(
            UGothicAttributeSet::GetSuperMeterAttribute(), 0.f);
    }

    if (CachedASC && ActiveReckoningHandle.IsValid())
    {
        CachedASC->RemoveActiveGameplayEffect(ActiveReckoningHandle);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
