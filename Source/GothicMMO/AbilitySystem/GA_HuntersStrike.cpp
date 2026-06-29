// GA_HuntersStrike.cpp

#include "AbilitySystem/GA_HuntersStrike.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"

UGA_HuntersStrike::UGA_HuntersStrike()
{
    UE_LOG(LogTemp, Log, TEXT("GA_HuntersStrike: Constructor called"));

    // Slot still set here as a default — can be overridden by the ability set
    AbilitySlot = EGothicAbilitySlot::Ability1;

    // Input tag NOT set here — comes from DA_HunterAbilitySet at grant time
    // This keeps the ability class tag-agnostic and data-driven

    ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Dead")));
    ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Stunned")));
    ActivationOwnedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Attacking")));

    UE_LOG(LogTemp, Log, TEXT("GA_HuntersStrike: Constructor complete"));
}

void UGA_HuntersStrike::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    bool bCommitted = CommitAbility(Handle, ActorInfo, ActivationInfo);

    UE_LOG(LogTemp, Log, TEXT("CommitAbility result: %s"),
        bCommitted ? TEXT("SUCCESS") : TEXT("FAILED"));

    if (!bCommitted)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    // Debug cooldown after single commit
    if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
    {
        FGameplayTagContainer CooldownTags;
        CooldownTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Cooldown.LightAttack")));
        FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(CooldownTags);
        TArray<float> Durations = ActorInfo->AbilitySystemComponent->GetActiveEffectsTimeRemaining(Query);
        UE_LOG(LogTemp, Log, TEXT("Cooldown effects active after commit: %d"), Durations.Num());
    }

    if (GetOwningActorFromActorInfo()->HasAuthority())
    {
        PerformMeleeTrace();

        GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green,
            TEXT("Hunter's Strike fired!"));
    }

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_HuntersStrike::OnHitWindowOpened(FGameplayEventData Payload)
{
    if (GetOwningActorFromActorInfo()->HasAuthority())
    {
        PerformMeleeTrace();
    }
}

void UGA_HuntersStrike::PerformMeleeTrace()
{
    ACharacter* OwnerChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
    if (!OwnerChar)
    {
        return;
    }

    const FVector Forward = OwnerChar->GetActorForwardVector();
    const FVector Start   = OwnerChar->GetActorLocation() + (Forward * 50.f);
    const FVector End     = Start + (Forward * HitRange);

    TArray<FHitResult> HitResults;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(OwnerChar);
    QueryParams.bIgnoreTouches = true;

    GetWorld()->SweepMultiByChannel(
        HitResults,
        Start,
        End,
        FQuat::Identity,
        ECollisionChannel::ECC_Pawn,
        FCollisionShape::MakeSphere(HitSphereRadius),
        QueryParams);

#if WITH_EDITOR
    DrawDebugSphere(GetWorld(), End, HitSphereRadius, 12, FColor::Red, false, 1.0f);
#endif

    TArray<AActor*> AlreadyHit;

    for (const FHitResult& Hit : HitResults)
    {
        AActor* HitActor = Hit.GetActor();
        if (!HitActor || AlreadyHit.Contains(HitActor))
        {
            continue;
        }

        UAbilitySystemComponent* TargetASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);

        if (TargetASC && ImmunityTags.Num() > 0)
        {
            if (TargetASC->HasAnyMatchingGameplayTags(ImmunityTags))
            {
                continue;
            }
        }

        GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange,
            FString::Printf(TEXT("Hit: %s"), *HitActor->GetName()));

        ApplyDamageToTarget(HitActor, DamageEffectClass, DamageMultiplier);
        AlreadyHit.Add(HitActor);
        
        // After ApplyDamageToTarget
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, 
                TEXT("Camera shake triggered"));
            PC->ClientStartCameraShake(CameraShakeClass);
        }

        // Only gain super meter if we actually hit something
        if (SuperGainOnHitEffect)
        {
            FGameplayEffectContextHandle Context = 
                GetAbilitySystemComponentFromActorInfo()->MakeEffectContext();
            FGameplayEffectSpecHandle Spec = 
                GetAbilitySystemComponentFromActorInfo()->MakeOutgoingSpec(
                    SuperGainOnHitEffect, 1.f, Context);
            if (Spec.IsValid())
            {
                GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToSelf(
                    *Spec.Data.Get());
            }
        }
    }
}

void UGA_HuntersStrike::EndAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}