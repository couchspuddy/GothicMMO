// GA_HuntersStrike.h
// "Hunter's Strike" — the default light melee attack.
// This is a C++ ability that:
//   1. Plays a montage (set in Blueprint child via base class MontageToPlay)
//   2. Opens a damage window via an anim notify (Event.Montage.HitWindow)
//   3. Sphere-traces for targets in front of the player
//   4. Applies GE_MeleeDamage to each target hit
//   5. Ends cleanly when the montage completes (cancels if interrupted)
//
// Blueprint child: BP_GA_HuntersStrike
//   - Set MontageToPlay to your melee anim montage (inherited from base)
//   - Set DamageEffectClass to GE_MeleeDamage
//   - Bind to slot LightAttack

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "Camera/CameraShakeBase.h"
#include "GA_HuntersStrike.generated.h"

UCLASS()
class GOTHICMMO_API UGA_HuntersStrike : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_HuntersStrike();

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

protected:
    // MontageToPlay is inherited from UGothicGameplayAbility — set in Blueprint.

    /** The GameplayEffect that deals the damage. Assign in Blueprint. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UGameplayEffect> DamageEffectClass;

    /** Radius of the melee hit sphere trace (cm). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    float HitSphereRadius = 80.f;

    /** How far forward the sphere trace reaches (cm). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    float HitRange = 200.f;

    /** Damage multiplier. 1.0 = base AttackPower. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    float DamageMultiplier = 1.0f;

    /** Tags on targets that prevent them from being hit. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    FGameplayTagContainer ImmunityTags;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UGameplayEffect> SuperGainOnHitEffect;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UGameplayEffect> SuperGainOnKillEffect;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UCameraShakeBase> CameraShakeClass;

    // Base class montage callbacks — override hit window for our damage logic
    virtual void OnMontageHitWindow(FGameplayEventData Payload) override;

    /** Performs the actual sphere trace and applies damage. Server only. */
    void PerformMeleeTrace();
};