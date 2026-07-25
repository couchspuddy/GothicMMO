// GA_Reckoning.h
// "The Reckoning" — Hunter Covenant ability.
// The Flame comes out fully. For a brief window, every strike and shot
// lands on the exact current vital point — no misses, no evasion works.
// "Not a rage. Not a burst of power. A conclusion."
//
// Implementation: grants a GameplayTag (State.Reckoning) for the duration.
// GA_Slicer, OnFire, and GA_HuntersStrike should each check for this tag
// and, if present, treat every hit as a guaranteed vital hit regardless of
// actual impact location. This keeps The Reckoning's "always true" behavior
// centralized as a single check other abilities opt into, rather than
// duplicating hit-forcing logic in this class.
//
// Duration is extended by Not At All — eliminating a stunned enemy while
// this tag is active should call ExtendReckoningDuration(), up to
// MaxExtendedDuration.
//
// Blueprint child: BP_GA_Reckoning
//   - Set BaseDuration (default 6s)
//   - Set MaxExtendedDuration (default 12s)
//   - Bind to slot SuperAbility / Covenant

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_Reckoning.generated.h"

UCLASS()
class GOTHICMMO_API UGA_Reckoning : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_Reckoning();

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

    /**
     * Called externally (e.g. by Not At All) when a stunned enemy is
     * eliminated while The Reckoning is active. Extends remaining duration
     * up to MaxExtendedDuration. Safe to call even if Reckoning isn't active
     * (no-ops in that case).
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Reckoning")
    void ExtendReckoningDuration(float ExtensionAmount);

protected:
    /** Base duration of The Reckoning, in seconds. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reckoning")
    float BaseDuration = 6.f;

    /** Hard ceiling on duration even with extensions from Not At All. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reckoning")
    float MaxExtendedDuration = 12.f;

    /**
     * GameplayEffect that grants State.Reckoning for the ability's duration.
     * Other abilities (Slicer, OnFire, HuntersStrike) check for this tag to
     * force guaranteed vital hits. Assign in Blueprint — should be an
     * infinite-duration effect; this ability manually removes it on end.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reckoning")
    TSubclassOf<UGameplayEffect> ReckoningStateEffect;

    /**
     * How often (seconds) the super meter is repainted while The Reckoning runs.
     * The meter is refilled at activation and bled back to zero across the
     * duration so the bar itself reads as the remaining Reckoning window.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reckoning")
    float DrainTickInterval = 0.1f;

private:
    FTimerHandle DurationTimerHandle;
    float RemainingDuration = 0.f;

    UPROPERTY()
    TObjectPtr<class UAbilitySystemComponent> CachedASC;

    FActiveGameplayEffectHandle ActiveReckoningHandle;

    /** Repaints the super meter to match the fraction of duration remaining. */
    FTimerHandle DrainTimerHandle;
    void TickSuperDrain();

    void OnDurationExpired();
    void RestartDurationTimer();
};
