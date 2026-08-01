// GA_BestialLucidCharge.cpp

#include "AbilitySystem/GA_BestialLucidCharge.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "Character/GothicPlayerCharacter.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"
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
    // Before Super, deliberately. Super runs the Blueprint graph, and a graph
    // that reaches EndAbility synchronously would otherwise leave the sweep
    // timer armed on an instance that has already finished — EndAbility can
    // only tear down a window that exists by the time it runs.
    BeginChargeDamageWindow();

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

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BestialLucidCharge::BeginChargeDamageWindow()
{
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

    ChargeWindowStartSeconds = World->GetTimeSeconds();

    // Sweep once immediately: anyone already standing on the boss when she
    // commits should be hit by the start of the charge, not by its second
    // sample 50ms later.
    SweepChargeDamage();

    World->GetTimerManager().SetTimer(
        ChargeSweepTimerHandle, this,
        &UGA_BestialLucidCharge::SweepChargeDamage,
        FMath::Max(0.01f, ChargeSweepInterval), true);
}

void UGA_BestialLucidCharge::EndChargeDamageWindow()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ChargeSweepTimerHandle);
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
