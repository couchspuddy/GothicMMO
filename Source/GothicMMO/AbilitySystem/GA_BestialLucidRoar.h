// GA_BestialLucidRoar.h
// AOE stun. Applies State.Stunned to every player in range.
// Now supports an optional attack montage via the base class — assign
// a roar/scream montage in BP_GA_BestialLucid_Roar and the stun fires
// at the hit window frame, not instantly.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_BestialLucidRoar.generated.h"

UCLASS()
class GOTHICMMO_API UGA_BestialLucidRoar : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_BestialLucidRoar();

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
    /** Radius around the boss that gets hit. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roar")
    float StunRadius = 600.f;

    /** The GameplayEffect that grants State.Stunned. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roar")
    TSubclassOf<UGameplayEffect> StunEffectClass;

    /**
     * The tell, in seconds. With no montage assigned the stun used to land on
     * the same frame the ability activated: a 600-radius 2s stun with zero
     * warning, which is not a mechanic a player can play around, it is just a
     * tax on standing near her. This is the window they get to leave in.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roar")
    float WindupDelay = 0.9f;

    /**
     * Fires at the START of the windup, WindupDelay before the stun lands.
     * The hook for the roar's audio and VFX — implement it in BP_GA_BestialLucid_Roar.
     * If nothing implements it the ability still delays, it is just silent about it.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Roar")
    void OnStunWindup();

    // Base class montage callback — stun logic fires at the hit window
    virtual void OnMontageHitWindow(FGameplayEventData Payload) override;

private:
    /** The actual stun logic — shared between instant and montage paths. */
    void PerformRoarStun();

    /** Timer target: the windup has elapsed, land the stun and finish. */
    void OnWindupElapsed();

    FTimerHandle WindupTimerHandle;
};