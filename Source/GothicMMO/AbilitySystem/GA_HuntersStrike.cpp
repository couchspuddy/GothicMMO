// GA_HuntersStrike.cpp

#include "AbilitySystem/GA_HuntersStrike.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AI/GothicEnemyBase.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"

UGA_HuntersStrike::UGA_HuntersStrike()
{
    AbilitySlot = EGothicAbilitySlot::Ability1;

    ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Dead")));
    ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Stunned")));
    ActivationOwnedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Attacking")));
}

void UGA_HuntersStrike::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    if (PlayOptionalMontage())
    {
        // Montage started — damage fires in OnMontageHitWindow when the
        // anim notify reaches the swing contact frame. Base class handles
        // EndAbility when the montage completes or is interrupted.
        return;
    }

    // No montage assigned — instant fallback (same as before montage support)
    if (GetOwningActorFromActorInfo()->HasAuthority())
    {
        PerformMeleeTrace();
    }

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_HuntersStrike::OnMontageHitWindow(FGameplayEventData Payload)
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

    // AttackPowerCoefficient is a coefficient on the wielder's AttackPower, so
    // it has to be resolved into a number of hit points before it goes out as
    // the flat Data.Damage SetByCaller. Read off the source ASC — the player's
    // lives on the PlayerState — so a weapon or buff that raises AttackPower
    // raises the strike with it.
    const UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
    const float AttackPower = SourceASC
        ? SourceASC->GetNumericAttribute(UGothicAttributeSet::GetAttackPowerAttribute())
        : 0.f;

    const float RawDamage = AttackPowerCoefficient * AttackPower;

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

        // Resolve the coefficient against AttackPower here. ApplyDamageToTarget
        // takes FLAT damage — the GEs have no multiplier semantics — so passing
        // the raw coefficient made the strike deal 0.5 damage, floored to 1.
        ApplyDamageToTarget(HitActor, DamageEffectClass, RawDamage);
        AlreadyHit.Add(HitActor);

        // Super meter gain on hit
        if (SuperGainOnHitEffect)
        {
            FGameplayEffectContextHandle Context =
                GetAbilitySystemComponentFromActorInfo()->MakeEffectContext();
            FGameplayEffectSpecHandle Spec =
                GetAbilitySystemComponentFromActorInfo()->MakeOutgoingSpec(
                    SuperGainOnHitEffect, 1.f, Context);
            if (Spec.IsValid())
            {
                UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(
                    Spec,
                    FGameplayTag::RequestGameplayTag(FName("Data.SuperMeter")),
                    15.f);

                GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToSelf(
                    *Spec.Data.Get());
            }
        }
    }
}