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
#include "Engine/World.h"
#include "TimerManager.h"

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

    // bAllowInterruptAfterBlendOut = true is load-bearing, not a preference.
    // With it false (the engine default), a montage that gets interrupted
    // AFTER its blend-out has already begun broadcasts nothing at all:
    // OnMontageEnded takes neither the OnCompleted branch (bInterrupted) nor
    // the OnInterrupted branch (gated on this very flag) and simply EndTasks.
    // Since we no longer end on OnBlendOut, that silence would leave the
    // ability alive forever and deadlock the BT task waiting on it. With the
    // flag true, that case reaches OnInterrupted. See UE 5.8
    // AbilityTask_PlayMontageAndWait.cpp OnMontageEnded/OnMontageBlendingOut.
    UAbilityTask_PlayMontageAndWait* MontageTask =
        UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
            this, NAME_None, MontageToPlay, MontagePlayRate,
            NAME_None, /*bStopWhenAbilityEnds=*/true,
            /*AnimRootMotionTranslationScale=*/1.f, /*StartTimeSeconds=*/0.f,
            /*bAllowInterruptAfterBlendOut=*/true);

    // OnBlendOut is deliberately NOT bound to OnMontageEnd. Blend-out is the
    // START of the montage's tail, not its end: on Melee_A_Montage at 0.85
    // rate it fires at 0.921s of a 1.216s swing, so ending there truncated
    // every attack's recovery and let the next activation restart the montage
    // mid-swing. The ability now lives until the montage actually ends.
    MontageTask->OnCompleted.AddDynamic(this, &UGothicGameplayAbility::OnMontageEnd);
    MontageTask->OnInterrupted.AddDynamic(this, &UGothicGameplayAbility::OnMontageCancel);
    MontageTask->OnCancelled.AddDynamic(this, &UGothicGameplayAbility::OnMontageCancel);
    MontageTask->OnBlendOut.AddDynamic(this, &UGothicGameplayAbility::OnMontageBlendOut);

    MontageTask->ReadyForActivation();

    // Listen for the hit window anim notify.
    // The montage needs an AnimNotify_SendGameplayEvent at the swing
    // contact frame, sending tag Event.Montage.HitWindow.
    UAbilityTask_WaitGameplayEvent* EventTask =
        UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
            this,
            GothicTags::Event_Montage_HitWindow);

    EventTask->EventReceived.AddDynamic(this, &UGothicGameplayAbility::OnMontageHitWindow);
    EventTask->ReadyForActivation();


    return true;
}

void UGothicGameplayAbility::OnMontageHitWindow(FGameplayEventData Payload)
{
    // Default does nothing. Derived classes override to do damage,
    // AOE stun, etc. at the moment the animation connects.
}

void UGothicGameplayAbility::OnMontageBlendOut()
{
    // Blend-out no longer ends the ability. It only arms a watchdog: from here
    // the montage has at most its blend-out duration left to live, so if
    // neither OnCompleted nor OnInterrupted has arrived a comfortable margin
    // after that, something swallowed the callback and the ability would hang.
    // Belt and braces for the one path the engine only covers while
    // AbilitySystem.PlayMontage.FireInterruptOnAnimEndInterrupt is on.
    UWorld* World = GetWorld();
    if (!World || !MontageToPlay)
    {
        return;
    }

    const float Rate = FMath::Max(MontagePlayRate, KINDA_SMALL_NUMBER);
    const float Grace = 0.5f;
    const float Delay = (MontageToPlay->GetDefaultBlendOutTime() / Rate) + Grace;

    World->GetTimerManager().SetTimer(
        MontageWatchdogHandle, this,
        &UGothicGameplayAbility::OnMontageWatchdog,
        FMath::Max(Delay, 0.1f), false);
}

void UGothicGameplayAbility::OnMontageWatchdog()
{
    // Reaching this at all means the montage's end callback never landed.
    // EndAbility is a no-op if the ability already ended, so this is safe.
    UE_LOG(LogTemp, Warning,
        TEXT("%s: montage '%s' ended without an end callback — watchdog ending the ability"),
        *GetName(), *GetNameSafe(MontageToPlay));

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UGothicGameplayAbility::OnMontageEnd()
{
    ClearMontageWatchdog();

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGothicGameplayAbility::OnMontageCancel()
{
    ClearMontageWatchdog();

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UGothicGameplayAbility::ClearMontageWatchdog()
{
    if (MontageWatchdogHandle.IsValid())
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(MontageWatchdogHandle);
        }
        MontageWatchdogHandle.Invalidate();
    }
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

    AActor* AvatarActor = GetAvatarActorFromActorInfo();

    // Structural double-apply guard. IsHitboxDrivenMelee() above reads the
    // montage's notify list, which makes the safety a property of the ASSET:
    // FeralRend is suppressed today only because it shares Melee_A_Montage with
    // the Claw, and retiming or removing that notify would silently restore
    // Rend's graph-side rangeless damage ON TOP of the hitbox. This asks the
    // hitbox itself instead — if it already paid out on this target inside the
    // current swing, the graph's copy is a duplicate whatever the montage says.
    if (const AGothicEnemyBase* AvatarEnemy = Cast<AGothicEnemyBase>(AvatarActor))
    {
        if (const UGothicMeleeHitboxComponent* Hitbox = AvatarEnemy->GetMeleeHitbox())
        {
            if (Hitbox->HasRecentlyDamaged(Target))
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("%s on %s: hitbox already damaged %s this swing — graph-side copy dropped"),
                    *GetName(), *AvatarEnemy->GetName(), *Target->GetName());
                return;
            }
        }
    }

    // Reach. Without this the fallback is not a melee attack at all: no trace,
    // no arc, no distance — if the ability activated, the target is damaged
    // wherever they are standing. BP_GA_FeralPounce is exactly that today
    // (bUseDirectDamageFallback true, MontageToPlay None, so the notify check
    // above can never catch it) and pays out across the whole arena.
    //
    // Horizontal, matching every other combat range in this project: the
    // avatar's origin sits at capsule centre, so a 3D distance quietly shrinks
    // every reach by the capsule half-height.
    if (AvatarActor && DirectDamageMaxRange > 0.f)
    {
        const float Distance = FVector::Dist2D(
            AvatarActor->GetActorLocation(), Target->GetActorLocation());

        if (Distance > DirectDamageMaxRange)
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("%s: %s is %.0fuu away, past DirectDamageMaxRange %.0f — no damage"),
                *GetName(), *Target->GetName(), Distance, DirectDamageMaxRange);
            return;
        }
    }

    UAbilitySystemComponent* TargetASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
    UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();

    if (!TargetASC || !SourceASC)
    {
        return;
    }

    // The instigator is the AVATAR, not GetOwningActorFromActorInfo(). The
    // owning actor is the Controller (InitializeGAS passes GetOwner()), which
    // has no ASC, so AttackPower resolved to 0 on every ability that damaged
    // through here — measured as a 7-damage boss claw. See MakeDamageContext.
    FGameplayEffectContextHandle Context =
        UGothicAbilitySystemComponent::MakeDamageContext(SourceASC, AvatarActor);

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
        HitEnemy->MulticastOnHit(
            FeedbackPoint, bWasVital, DamageValue, GetAvatarActorFromActorInfo());
    }
}