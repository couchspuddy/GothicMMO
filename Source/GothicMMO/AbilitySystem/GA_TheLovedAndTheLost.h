// GA_TheLovedAndTheLost.h
// "The Loved and The Lost" — Hunter passive.
// As the Hunter engages in combat, the names of those they have lost begin
// to manifest through the Prior Flame. While in active combat, grants
// increased ability regeneration and increased Steadfast accumulation rate.
// Both gains are capped and ramp up through sustained engagement.
//
// Implementation: this is a passive ability, granted at character spawn via
// the ability set and activated once (bActivateAbilityOnGranted-style),
// never explicitly triggered by player input. It listens for a
// "State.InCombat" tag being added/removed on the owning ASC (assumes such
// a tag exists and is managed elsewhere — e.g. set on taking or dealing
// damage, cleared after N seconds without either). While that tag is
// present, a ramping buff GameplayEffect is kept active and its magnitude
// increases over time up to a cap. When the tag is removed, the effect is
// removed and the ramp resets.
//
// Blueprint child: BP_GA_TheLovedAndTheLost
//   - Set InCombatTag (default State.InCombat — confirm this matches your
//     project's actual in-combat tag)
//   - Set RampEffect (an infinite GE with a SetByCaller magnitude this class
//     updates over time via SetByCallerMagnitude)
//   - Set RampUpDuration (time to reach max ramp, default 8s)
//   - Set RampTickInterval (how often magnitude updates, default 0.5s)

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_TheLovedAndTheLost.generated.h"

UCLASS()
class GOTHICMMO_API UGA_TheLovedAndTheLost : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_TheLovedAndTheLost();

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
     * Ramp progress 0..1 for the HUD's passive meter. 0 when not in combat,
     * climbs to 1 over RampUpDuration. Server-authoritative — the value is only
     * meaningful on the machine running the ramp (fine for standalone/listen).
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Passives")
    float GetRampAlpha() const
    {
        return RampUpDuration > 0.f
            ? FMath::Clamp(CurrentRampTime / RampUpDuration, 0.f, 1.f)
            : 0.f;
    }

    /** True while the ramp buff is applied (i.e. the Hunter is in combat). */
    UFUNCTION(BlueprintPure, Category = "Gothic|Passives")
    bool IsRampActive() const { return ActiveRampHandle.IsValid(); }

protected:
    /** Tag that indicates the character is currently in combat. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TheLovedAndTheLost")
    FGameplayTag InCombatTag;

    /**
     * Infinite-duration GameplayEffect providing the ability regen / Steadfast
     * gain bonus. Its magnitude is driven via SetByCallerMagnitude using tag
     * "Data.RampMagnitude" — the associated GE should read this SetByCaller
     * value for whatever attributes it modifies.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TheLovedAndTheLost")
    TSubclassOf<UGameplayEffect> RampEffect;

    /** Time in seconds to ramp from 0 to full magnitude while in combat. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TheLovedAndTheLost")
    float RampUpDuration = 8.f;

    /** Cap on the ramp magnitude — tune alongside RampEffect's attribute modifiers. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TheLovedAndTheLost")
    float MaxRampMagnitude = 1.f;

    /** How often the ramp magnitude is recalculated and pushed to the effect. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TheLovedAndTheLost")
    float RampTickInterval = 0.5f;

private:
    UPROPERTY()
    TObjectPtr<class UAbilitySystemComponent> CachedASC;

    FActiveGameplayEffectHandle ActiveRampHandle;
    FDelegateHandle CombatTagDelegateHandle;
    FTimerHandle RampTickHandle;
    float CurrentRampTime = 0.f;

    void OnCombatTagChanged(const FGameplayTag Tag, int32 NewCount);
    void StartRamping();
    void StopRamping();
    void TickRamp();
};
