// GA_BestialLucidCharge.cpp

#include "AbilitySystem/GA_BestialLucidCharge.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "AI/GothicEnemyAIController.h"
#include "Character/GothicPlayerCharacter.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"

UGA_BestialLucidCharge::UGA_BestialLucidCharge()
{
    AbilitySlot = EGothicAbilitySlot::Ability1;

    // The graph applies its own single-target hit today. Once this class owns
    // the damage, that one is a duplicate — and unlike the Claw's, it cannot be
    // detected automatically, because the charge has no montage and therefore
    // no hitbox notify to find. Default it off here so a Blueprint reparented
    // to this class is correct before anyone touches its properties.
    bUseDirectDamageFallback = false;
}

void UGA_BestialLucidCharge::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    // The damage window is NOT armed here. Activation is 0.8s of stationary
    // windup ahead of the launch, and arming it here spent nearly two thirds of
    // the window sweeping the boss's own start position while she stood still.
    // The graph calls BeginChargeDamageWindow immediately after LaunchCharacter
    // instead — see the header.
    //
    // The old ordering-before-Super concern is gone with it: there is now
    // nothing armed at the moment Super runs the graph, and a graph that reaches
    // EndAbility synchronously simply never opens a window.
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UGA_BestialLucidCharge::EndAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    // Cancelled mid-charge (staggered, killed, phase transition) stops being
    // dangerous immediately. A charge that is no longer happening must not keep
    // running people over.
    EndChargeDamageWindow();

    // And the boss must not be left mid-flight with a paused brain and zero air
    // control. Unconditional, not gated on bWasCancelled: a normal end that
    // happens after the landing finds nothing in flight and this is a no-op,
    // while ANY early end — cancel, stagger, phase transition, death — is
    // exactly the case that would otherwise wedge her. A pawn stuck brain-dead
    // is far worse than the mistimed window this class exists to fix.
    if (AGothicEnemyAIController* EnemyAIC = GetEnemyController())
    {
        EnemyAIC->AbortLeapFlight();
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

AGothicEnemyAIController* UGA_BestialLucidCharge::GetEnemyController() const
{
    const APawn* AvatarPawn = Cast<APawn>(GetAvatarActorFromActorInfo());
    return AvatarPawn ? Cast<AGothicEnemyAIController>(AvatarPawn->GetController()) : nullptr;
}

void UGA_BestialLucidCharge::HandleLeapLanded(bool bLanded)
{
    EndChargeDamageWindow();
}

void UGA_BestialLucidCharge::BeginChargeDamageWindow()
{
    // Idempotent. A graph wired to call this twice, or a branch that re-enters,
    // must not restart the clock or wipe the already-hit set mid-charge — that
    // would let the same victim be hit a second time by one charge.
    if (bChargeWindowOpen)
    {
        return;
    }

    HitPawnsThisCharge.Reset();

    const AActor* Owner = GetOwningActorFromActorInfo();
    UWorld* World = GetWorld();

    if (!World || !Owner || !Owner->HasAuthority())
    {
        return;
    }

    if (!ChargeDamageEffect)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("BestialLucidCharge[%s]: ChargeDamageEffect is unassigned — the charge moves the boss and damages nobody"),
            *GetNameSafe(GetAvatarActorFromActorInfo()));
        return;
    }

    bChargeWindowOpen = true;
    ChargeWindowStartSeconds = World->GetTimeSeconds();

    // The real closer. ChargeDamageWindow is only the backstop for a landing
    // that never comes — a leap cut short by geometry or a wall should stop
    // being dangerous the moment it stops travelling, not when its timer says
    // it might still be in the air.
    if (AGothicEnemyAIController* EnemyAIC = GetEnemyController())
    {
        EnemyAIC->OnLeapLanded.AddDynamic(this, &UGA_BestialLucidCharge::HandleLeapLanded);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("BestialLucidCharge[%s]: no AGothicEnemyAIController — the damage window can only "
                 "close on its %.2fs backstop, and the leap itself is unguarded"),
            *GetNameSafe(GetAvatarActorFromActorInfo()), ChargeDamageWindow);
    }

    // Sweep once immediately: anyone already standing on the boss when she
    // launches should be hit by the start of the charge, not by its second
    // sample 50ms later.
    SweepChargeDamage();

    // SweepChargeDamage can close the window on the spot (no ASC, no avatar), so
    // only arm the repeat if it is still open.
    if (bChargeWindowOpen)
    {
        World->GetTimerManager().SetTimer(
            ChargeSweepTimerHandle, this,
            &UGA_BestialLucidCharge::SweepChargeDamage,
            FMath::Max(0.01f, ChargeSweepInterval), true);
    }
}

void UGA_BestialLucidCharge::EndChargeDamageWindow()
{
    bChargeWindowOpen = false;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ChargeSweepTimerHandle);
    }

    // Unbind even if the landing is what closed us — an ability instance can be
    // re-activated, and a surviving binding would have the next charge's window
    // closed by the previous charge's delegate list.
    if (AGothicEnemyAIController* EnemyAIC = GetEnemyController())
    {
        EnemyAIC->OnLeapLanded.RemoveDynamic(this, &UGA_BestialLucidCharge::HandleLeapLanded);
    }
}

void UGA_BestialLucidCharge::SweepChargeDamage()
{
    UWorld* World = GetWorld();
    AActor* Avatar = GetAvatarActorFromActorInfo();

    if (!World || !Avatar || !ChargeDamageEffect)
    {
        EndChargeDamageWindow();
        return;
    }

    if (World->GetTimeSeconds() - ChargeWindowStartSeconds >= ChargeDamageWindow)
    {
        EndChargeDamageWindow();
        return;
    }

    UAbilitySystemComponent* BossASC = GetAbilitySystemComponentFromActorInfo();
    if (!BossASC)
    {
        EndChargeDamageWindow();
        return;
    }

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(Avatar);

    TArray<AActor*> Overlapping;
    UKismetSystemLibrary::SphereOverlapActors(
        Avatar,
        Avatar->GetActorLocation(),
        ChargeHitRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
        AGothicPlayerCharacter::StaticClass(),
        IgnoreActors,
        Overlapping);

    for (AActor* Victim : Overlapping)
    {
        if (!Victim || HitPawnsThisCharge.Contains(Victim))
        {
            continue;
        }

        UAbilitySystemComponent* TargetASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Victim);
        if (!TargetASC)
        {
            continue;
        }

        // Same construction as GothicMeleeHitboxComponent and the BT task the
        // sweep came from: flat damage rides Data.Damage and the target's
        // Defense is subtracted by the attribute set. Nothing charge-specific.
        //
        // The instigator is the boss AVATAR. It used to be
        // GetOwningActorFromActorInfo() — the AIController — which has no ASC,
        // so the boss's AttackPower never reached the pipeline and the charge
        // landed 37 rather than the 57 its tuning comment claimed.
        FGameplayEffectContextHandle Context =
            UGothicAbilitySystemComponent::MakeDamageContext(BossASC, Avatar);

        FGameplayEffectSpecHandle Spec = BossASC->MakeOutgoingSpec(
            ChargeDamageEffect, GetAbilityLevel(), Context);

        if (!Spec.IsValid())
        {
            continue;
        }

        Spec.Data->SetSetByCallerMagnitude(GothicTags::Data_Damage, ChargeDamage);
        BossASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);

        HitPawnsThisCharge.Add(Victim);

        UE_LOG(LogTemp, Log,
            TEXT("BestialLucidCharge[%s]: ran over %s for %.0f raw (%.2fs into the window)"),
            *Avatar->GetName(), *Victim->GetName(), ChargeDamage,
            World->GetTimeSeconds() - ChargeWindowStartSeconds);
    }
}
