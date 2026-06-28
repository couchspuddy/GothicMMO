// GA_HuntersStrike.h
// "Hunter's Strike" — the default light melee attack.
// This is a C++ ability that:
//   1. Plays a montage (set in Blueprint child)
//   2. Opens a damage window via an anim notify
//   3. Sphere-traces for targets in front of the player
//   4. Applies GE_MeleeDamage to each target hit
//   5. Ends cleanly (cancels if interrupted)
//
// Blueprint child: BP_GA_HuntersStrike
//   - Set MontageToPlay to your melee anim montage
//   - Set DamageEffectClass to GE_MeleeDamage
//   - Bind to slot LightAttack

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
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

    virtual void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled) override;

protected:
    /** Montage to play. Assign in Blueprint child (BP_GA_HuntersStrike). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    UAnimMontage* MontageToPlay;

    /** The GameplayEffect that deals the damage. Assign in Blueprint. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UGameplayEffect> DamageEffectClass;

    /**
     * Radius of the melee hit sphere trace (cm).
     * Keep small (~80) for a dagger feel, larger (~120) for a two-hander.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    float HitSphereRadius = 80.f;

    /**
     * How far forward the sphere trace reaches (cm).
     * Represents the effective melee range.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    float HitRange = 200.f;

    /** Damage multiplier. 1.0 = base AttackPower. Increase for heavy variants. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    float DamageMultiplier = 1.0f;

    /**
     * Tags that, if present on a potential target, prevent them from being hit.
     * Use this to prevent friendly fire: add "Team.Player" here if this is a
     * PvE-only ability, or leave empty for PvP.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    FGameplayTagContainer ImmunityTags;
    
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UGameplayEffect> SuperGainOnHitEffect;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UGameplayEffect> SuperGainOnKillEffect;

    // -------------------------------------------------------------------------
    // Called by the WaitGameplayEvent task when the anim notify fires.
    // The notify should send event tag "Event.Montage.HitWindow.Open".
    // -------------------------------------------------------------------------
    UFUNCTION()
    void OnHitWindowOpened(FGameplayEventData Payload);

    /** Performs the actual sphere trace and applies damage. Server only. */
    void PerformMeleeTrace();
};
