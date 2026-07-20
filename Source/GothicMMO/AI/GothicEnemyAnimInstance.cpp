// GothicEnemyAnimInstance.cpp

#include "AI/GothicEnemyAnimInstance.h"
#include "AI/GothicEnemyBase.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "KismetAnimationLibrary.h"
#include "Animation/AnimMontage.h"

void UGothicEnemyAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    OwnerEnemy = Cast<AGothicEnemyBase>(TryGetPawnOwner());
    if (OwnerEnemy)
    {
        CachedASC = OwnerEnemy->GetGothicASC();
    }

    if (!OwnerEnemy)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicEnemyAnimInstance: Owner is not AGothicEnemyBase — animation state will not update"));
    }
}

void UGothicEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!OwnerEnemy)
    {
        return;
    }

    // -- Locomotion ----------------------------------------------------------

    if (UCharacterMovementComponent* Movement = OwnerEnemy->GetCharacterMovement())
    {
        const FVector Velocity = Movement->Velocity;
        GroundSpeed = Velocity.Size2D();
        bIsInAir = Movement->IsFalling();

        // Direction relative to facing. CalculateDirection returns degrees:
        // 0 = forward along the actor's facing, positive = right.
        if (GroundSpeed > 1.f)
        {
            MovementDirection = UKismetAnimationLibrary::CalculateDirection(Velocity, OwnerEnemy->GetActorRotation());
        }
        else
        {
            MovementDirection = 0.f;
        }
    }

    // -- GAS tag reads -------------------------------------------------------
    // These are replicated tags, so remote clients get the correct values
    // without any additional RPC. The tag checks run every frame, which is
    // standard for AnimInstance — the alternative (delegate-driven) saves a
    // few tag lookups per frame but introduces ordering bugs with replication.

    if (CachedASC)
    {
        bIsDead = CachedASC->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Dead")));

        bIsInCombat = CachedASC->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.InCombat")));

        bIsStunned = CachedASC->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Stunned")));

        bIsAttacking = CachedASC->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Attacking")));
    }

}

void UGothicEnemyAnimInstance::PlayHitReact(const FVector& HitWorldLocation)
{
    if (!OwnerEnemy)
    {
        return;
    }

    // Compute direction of incoming hit relative to the enemy's facing.
    // Front = 0, Right = 90, Left = -90, Back = ±180.
    const FVector ToHit = (HitWorldLocation - OwnerEnemy->GetActorLocation()).GetSafeNormal2D();
    const FVector Forward = OwnerEnemy->GetActorForwardVector().GetSafeNormal2D();

    const float Dot = FVector::DotProduct(Forward, ToHit);
    const float Cross = FVector::CrossProduct(Forward, ToHit).Z;
    const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));

    // Pick the montage based on quadrant
    UAnimMontage* MontageToPlay = nullptr;

    if (Angle >= -45.f && Angle < 45.f)
    {
        MontageToPlay = HitReactFrontMontage;
    }
    else if (Angle >= 45.f && Angle < 135.f)
    {
        MontageToPlay = HitReactRightMontage;
    }
    else if (Angle >= -135.f && Angle < -45.f)
    {
        MontageToPlay = HitReactLeftMontage;
    }
    else
    {
        MontageToPlay = HitReactBackMontage;
    }

    if (MontageToPlay)
    {
        Montage_Play(MontageToPlay, 1.0f);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicEnemyAnimInstance: Hit react montage not assigned for angle %.1f on %s"),
            Angle, *GetOwningActor()->GetName());
    }
}