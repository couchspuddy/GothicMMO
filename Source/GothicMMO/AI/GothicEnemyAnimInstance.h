// GothicEnemyAnimInstance.h
// Thin C++ AnimInstance for all enemy characters.
// Caches game-state values that Animation Blueprints read every frame:
// speed, direction, alive/dead, in-combat, stunned.
//
// This class does NOT define the state machine — that lives in the
// per-skeleton Animation Blueprint (ABP_EnemySevarog, ABP_EnemyKhaimera).
// Each ABP reads from these properties to drive its own locomotion
// blendspace, state transitions, and montage slots.
//
// When final art replaces Paragon placeholders, only the ABP changes.
// This class transfers unchanged.
//
// Usage: set Anim Class on the enemy Blueprint's Mesh component to
// the ABP that parents this class (e.g. ABP_EnemySevarog).

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GothicEnemyAnimInstance.generated.h"

class AGothicEnemyBase;
class UGothicAbilitySystemComponent;
class UAnimMontage;

UCLASS()
class GOTHICMMO_API UGothicEnemyAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // -------------------------------------------------------------------------
    // Locomotion — read by the ABP's blendspace and state machine
    // -------------------------------------------------------------------------

    /** Ground speed in cm/s. Drives the locomotion blendspace axis. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation")
    float GroundSpeed = 0.f;

    /**
     * Movement direction relative to the actor's facing, in degrees.
     * 0 = forward, 90 = right, -90 = left, ±180 = backward.
     * Drives directional locomotion if the ABP uses a 2D blendspace.
     * For Paragon's 1D blendspaces (speed only), this is available but
     * unused — it's here for when final rigs support strafing.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation")
    float MovementDirection = 0.f;

    /** True when the character is in the air (jump/fall). */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation")
    bool bIsInAir = false;

    // -------------------------------------------------------------------------
    // Combat state — read by ABP transitions and montage blending
    // -------------------------------------------------------------------------

    /** True while State.InCombat tag is active on the ASC. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation")
    bool bIsInCombat = false;

    /** True while State.Dead tag is active. Drives death state entry. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation")
    bool bIsDead = false;

    /** True while State.Stunned tag is active. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation")
    bool bIsStunned = false;

    /** True while State.Attacking tag is active (ability owns the upper body). */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation")
    bool bIsAttacking = false;

    // -------------------------------------------------------------------------
    // Hit react — plays the correct directional montage directly from C++.
    // No flag, no Blueprint dispatch, no frame-ordering issue.
    // -------------------------------------------------------------------------

    /**
     * Call from GothicEnemyBase::MulticastOnHit.
     * Computes the hit direction and immediately plays the matching
     * hit react montage on DefaultSlot.
     *
     * @param HitWorldLocation  The impact point of the damage source.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Animation")
    void PlayHitReact(const FVector& HitWorldLocation);

    // -------------------------------------------------------------------------
    // Hit react montages — assign in ABP_Enemy Class Defaults.
    // Create these from Khaimera's HitReact_Front/Back/Left/Right anims.
    // All four must use DefaultGroup.DefaultSlot.
    // -------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|HitReact")
    TObjectPtr<UAnimMontage> HitReactFrontMontage;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|HitReact")
    TObjectPtr<UAnimMontage> HitReactBackMontage;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|HitReact")
    TObjectPtr<UAnimMontage> HitReactLeftMontage;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|HitReact")
    TObjectPtr<UAnimMontage> HitReactRightMontage;

protected:
    UPROPERTY()
    TObjectPtr<AGothicEnemyBase> OwnerEnemy;

    UPROPERTY()
    TObjectPtr<UGothicAbilitySystemComponent> CachedASC;
};