// GA_Lunge.h
// "The Lunge" — Hunter movement ability.
// Input-directional, fixed-distance repositioning tool. Not momentum based,
// not target locked. Doubles as a last-second dodge — the Hunter stays in
// the fight by moving through danger rather than away from it.
//
// Direction is read from the player's current movement input at activation
// time. If no input is present, defaults to the character's forward vector.
//
// Blueprint child: BP_GA_Lunge
//   - Set LungeDistance (default 600 units)
//   - Set LungeDuration (default 0.2s)
//   - Optionally set LungeStateEffect for i-frames during the traversal
//   - Bind to slot Movement/Dodge

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_Lunge.generated.h"

class ACharacter;
class UAbilitySystemComponent;

UCLASS()
class GOTHICMMO_API UGA_Lunge : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_Lunge();

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
    /** Fixed distance the Lunge covers, in cm. Not momentum based. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lunge")
    float LungeDistance = 600.f;

    /** How long the traversal takes, in seconds. Shorter = snappier dodge feel. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lunge")
    float LungeDuration = 0.2f;

    /**
     * Fraction of the lunge's traversal speed handed back to the character as real
     * velocity when the lunge ends. Horizontal only — the lunge never injects lift.
     *
     * The traversal itself is a SetActorLocation lerp with movement stopped, so the
     * character exits with a velocity of exactly zero unless something puts it back.
     * That zero is what reads as dropping dead in the air at the end of a dash.
     *
     * 0.0 reproduces the pre-fix behaviour exactly.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lunge",
              meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float DashEndMomentumRetention = 0.6f;

    /**
     * Optional GameplayEffect granted for the duration of the lunge.
     * Use to grant brief damage immunity/i-frames for the dodge use-case.
     * Leave unset for a pure repositioning tool with no i-frames.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lunge")
    TSubclassOf<UGameplayEffect> LungeStateEffect;

private:
    FVector GetLungeDirection(ACharacter* Character) const;
    void StartLungeMovement(ACharacter* Character, const FVector& Direction);

    /** Hands the retained fraction of the traversal speed back as real velocity. */
    void ApplyExitMomentum(ACharacter* Character) const;

    FTimerHandle LungeTickHandle;
    FVector LungeStartLocation;
    FVector LungeTargetLocation;
    /** Normalised horizontal direction this lunge is travelling. */
    FVector LungeDirection = FVector::ZeroVector;
    float LungeElapsed = 0.f;
    /** True between StartLungeMovement and the EndAbility that consumes it — gates
     *  the exit momentum so an ability that never moved cannot launch anyone. */
    bool bLungeMovementActive = false;

    UFUNCTION()
    void TickLunge();

    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> CachedASC;

    FActiveGameplayEffectHandle ActiveLungeStateHandle;
};
