// GA_Lunge.cpp

#include "AbilitySystem/GA_Lunge.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
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
    bool bCommitted = CommitAbility(Handle, ActorInfo, ActivationInfo);
    
    if (!bCommitted)
    {
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }
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

}

FVector UGA_Lunge::GetLungeDirection(ACharacter* Character) const
{
    if (!Character)
    {
        return FVector::ForwardVector;
    }

    if (!bDashUsesMovementInputDirection)
    {
        // Legacy behaviour: travel along current velocity. Reads fine on the ground,
        // but in the air velocity is whatever you were carrying before the dash, so
        // the lunge ignores both the camera and the stick.
        FVector LegacyDir = Character->GetVelocity().GetSafeNormal2D();
        if (LegacyDir.IsNearlyZero())
        {
            LegacyDir = Character->GetActorForwardVector().GetSafeNormal2D();
        }
        return LegacyDir;
    }

    // Held movement input wins. Pending input first — that is this frame's stick,
    // before CharacterMovement consumes it — then last consumed, which covers the
    // frame ordering where the ability activates after the move input was eaten.
    if (const UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
    {
        FVector InputDir = MoveComp->GetPendingInputVector().GetSafeNormal2D();
        if (InputDir.IsNearlyZero())
        {
            InputDir = MoveComp->GetLastInputVector().GetSafeNormal2D();
        }

        if (!InputDir.IsNearlyZero())
        {
            return InputDir;
        }
    }

    // No input held — dash where the player is looking. Control rotation rather than
    // the actor's forward vector: with the camera free of the mesh, where you look is
    // what the player reads as "forward".
    if (const AController* Controller = Character->GetController())
    {
        const FVector CameraForward =
            FRotationMatrix(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f))
                .GetUnitAxis(EAxis::X);
        if (!CameraForward.IsNearlyZero())
        {
            return CameraForward.GetSafeNormal2D();
        }
    }

    return Character->GetActorForwardVector().GetSafeNormal2D();
}

void UGA_Lunge::StartLungeMovement(ACharacter* Character, const FVector& Direction)
{
    LungeStartLocation = Character->GetActorLocation();
    LungeTargetLocation = LungeStartLocation + (Direction * LungeDistance);
    LungeDirection = Direction.GetSafeNormal2D();
    LungeElapsed = 0.f;
    bLungeMovementActive = true;

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

void UGA_Lunge::ApplyExitMomentum(ACharacter* Character) const
{
    if (!Character || DashEndMomentumRetention <= 0.f || LungeDirection.IsNearlyZero())
    {
        return;
    }

    UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
    if (!MoveComp || LungeDuration <= 0.f)
    {
        return;
    }

    // The traversal speed the lerp was actually running at, not a separate number
    // to keep in sync: distance over duration is the lunge's real velocity.
    const float TraversalSpeed = LungeDistance / LungeDuration;

    // Horizontal only. Z is left at zero rather than carried or boosted — the lunge
    // is a ground-plane reposition, and any vertical component here turns it into a
    // jump the player never asked for.
    const FVector ExitVelocity = LungeDirection * TraversalSpeed * DashEndMomentumRetention;

    MoveComp->Velocity = FVector(ExitVelocity.X, ExitVelocity.Y, 0.f);
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

    // Hand momentum back before anything else — including on a cancel, because a
    // lunge interrupted mid-flight leaves the character just as motionless as one
    // that finished.
    if (bLungeMovementActive)
    {
        bLungeMovementActive = false;
        ApplyExitMomentum(Cast<ACharacter>(GetAvatarActorFromActorInfo()));
    }

    if (CachedASC && ActiveLungeStateHandle.IsValid())
    {
        CachedASC->RemoveActiveGameplayEffect(ActiveLungeStateHandle);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
