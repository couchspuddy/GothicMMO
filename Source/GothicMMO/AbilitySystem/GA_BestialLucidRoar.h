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

protected:
    /** Radius around the boss that gets hit. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roar")
    float StunRadius = 600.f;

    /** The GameplayEffect that grants State.Stunned. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roar")
    TSubclassOf<UGameplayEffect> StunEffectClass;

    // Base class montage callback — stun logic fires at the hit window
    virtual void OnMontageHitWindow(FGameplayEventData Payload) override;

private:
    /** The actual stun logic — shared between instant and montage paths. */
    void PerformRoarStun();
};