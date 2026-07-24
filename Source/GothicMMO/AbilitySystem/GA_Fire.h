// GA_Fire.h
// The primary weapon fire ability. Replaces AGothicPlayerCharacter::OnFire(),
// which was a raw input-bound C++ function living entirely outside GAS.
//
// What the conversion buys, none of which OnFire() had:
//   - ActivationBlockedTags gating (a dead or stunned player could previously fire)
//   - A real cooldown via CommitAbility, so fire rate is data-driven not implicit
//   - Server-authoritative trace and damage (OnFire ran client-local and no-opped
//     on a remote client — the whole chain was standalone/listen-host only)
//   - A clean cosmetic/authoritative split, so muzzle flash and camera kick fire
//     instantly on the owning client without waiting for a server round trip
//
// Ammo is NOT a GAS cost. CostGameplayEffect requires an attribute, and magazine
// ammo is deliberately per-weapon int32 state on the pawn (attributes are
// per-character singletons and break the moment a second weapon exists). Ammo is
// gated in CanActivateAbility and consumed in ActivateAbility instead.
//
// Blueprint child: BP_GA_Fire
//   - Set DamageEffectClass to GE_WeaponDamage
//   - Set CooldownGameplayEffectClass to GE_Cooldown_Fire
//   - Implement OnFireCosmetic for muzzle flash / camera kick / sound
//
// GRANT THE BLUEPRINT, NOT THIS CLASS. DA_HunterAbilitySet must point at
// BP_GA_Fire — pointing it at GA_Fire silently discards every Blueprint value
// with no compile error and no warning. This has already happened once on GA_Read.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "DrawDebugHelpers.h"
#include "GA_Fire.generated.h"

class AGothicPlayerCharacter;

UCLASS()
class GOTHICMMO_API UGA_Fire : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_Fire();

    /**
     * Gates activation on having a round chambered, on top of the normal tag and
     * cooldown checks. This is where ammo lives instead of a CostGameplayEffect —
     * see the class comment. Runs on both client (prediction) and server.
     */
    virtual bool CanActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr,
        const FGameplayTagContainer* TargetTags = nullptr,
        OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

    /**
     * Applies the cooldown with its duration taken from the active weapon's rounds
     * per minute, via the Data.Cooldown SetByCaller. The base implementation applies
     * a fixed-duration GE, which meant every weapon fired at the same rate no matter
     * what its data asset said.
     *
     * The cooldown GE stays the gate rather than a timer on the pawn: GA_Fire is
     * LocalPredicted, and it is the Cooldown.PrimaryFire tag that keeps the client's
     * predicted rejection and the server's ruling in agreement.
     */
    virtual void ApplyCooldown(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo) const override;


protected:
    /**
     * Fired on the owning client the instant the ability activates, before any
     * server confirmation. Muzzle flash, camera kick, weapon animation, gunshot.
     *
     * This is the whole point of NetExecutionPolicy = LocalPredicted. If cosmetics
     * waited for the server, the gun would feel like it fires through mud no matter
     * how good the recoil curve is.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Fire")
    void OnFireCosmetic();

    /** The GameplayEffect that deals the damage. Assign GE_WeaponDamage in BP_GA_Fire. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire")
    TSubclassOf<UGameplayEffect> DamageEffectClass;

    /** Base damage before the vital multiplier. Was PistolDamage on the character. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire")
    float Damage = 15.f;

    /** Damage multiplier applied on a confirmed vital hit. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire")
    float VitalDamageMultiplier = 2.f;

    /** Hitscan range in cm. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire")
    float TraceRange = 5000.f;

    /**
     * Fire rate used when the pawn has no weapon data to read — same fallback role
     * as Damage and TraceRange above. A weapon with a data asset always wins.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire", meta = (ClampMin = 1))
    float FallbackRoundsPerMinute = 171.f;

    /**
     * Server-only. Traces on ECC_Weapon (the mesh, not the capsule), resolves the
     * vital point, applies damage, and multicasts impact feedback.
     */
    void PerformFireTrace(AGothicPlayerCharacter* Char);
};
