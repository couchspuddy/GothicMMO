// GA_Lunge.cpp

#include "AbilitySystem/GA_Lunge.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "TimerManager.h"
#include "Engine/World.h"

UGA_Lunge::UGA_Lunge()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Lunge::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
    if (!Character)
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_Lunge: No character — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    CachedASC = GetAbilitySystemComponentFromActorInfo();

    const FVector Direction = GetLungeDirection(Character);

    // Apply optional i-frame / state effect for the duration
    if (LungeStateEffect && CachedASC)
    {
        FGameplayEffectContextHandle Context = CachedASC->MakeEffectContext();
        FGameplayEffectSpecHandle Spec = CachedASC->MakeOutgoingSpec(LungeStateEffect, 1.f, Context);
        if (Spec.IsValid())
        {
            ActiveLungeStateHandle = CachedASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        }
    }

    StartLungeMovement(Character, Direction);

    UE_LOG(LogTemp, Log, TEXT("GA_Lunge: Activated, direction %s, distance %.1f"),
        *Direction.ToString(), LungeDistance);
}

FVector UGA_Lunge::GetLungeDirection(ACharacter* Character) const
{
    if (!Character)
    {
        return FVector::ForwardVector;
    }

    // Prefer current input-driven velocity direction if the character is moving
    const FVector Velocity = Character->GetVelocity();
    FVector Dir = Velocity.GetSafeNormal2D();

    if (Dir.IsNearlyZero())
    {
        // No input — fall back to facing direction
        Dir = Character->GetActorForwardVector().GetSafeNormal2D();
    }

    return Dir;
}

void UGA_Lunge::StartLungeMovement(ACharacter* Character, const FVector& Direction)
{
    LungeStartLocation = Character->GetActorLocation();
    LungeTargetLocation = LungeStartLocation + (Direction * LungeDistance);
    LungeElapsed = 0.f;

    // Optional: disable normal movement input processing during the lunge
    // by briefly zeroing velocity so CharacterMovement doesn't fight the manual set.
    if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
    {
        MoveComp->StopMovementImmediately();
    }

    GetWorld()->GetTimerManager().SetTimer(
        LungeTickHandle,
        this,
        &UGA_Lunge::TickLunge,
        0.016f, // ~60fps tick
        true);
}

void UGA_Lunge::TickLunge()
{
    ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
    if (!Character)
    {
        GetWorld()->GetTimerManager().ClearTimer(LungeTickHandle);
        EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
            GetCurrentActivationInfo(), true, false);
        return;
    }

    LungeElapsed += 0.016f;
    const float Alpha = FMath::Clamp(LungeElapsed / LungeDuration, 0.f, 1.f);

    const FVector NewLocation = FMath::Lerp(LungeStartLocation, LungeTargetLocation, Alpha);
    Character->SetActorLocation(NewLocation, true); // sweep = true to respect collision

    if (Alpha >= 1.f)
    {
        GetWorld()->GetTimerManager().ClearTimer(LungeTickHandle);
        EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
            GetCurrentActivationInfo(), true, false);
    }
}

void UGA_Lunge::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(LungeTickHandle);
    }

    if (CachedASC && ActiveLungeStateHandle.IsValid())
    {
        CachedASC->RemoveActiveGameplayEffect(ActiveLungeStateHandle);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
