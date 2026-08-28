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
class UAnimMontage;
class UGothicWeaponData;

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

    /**
     * Fire animation, played on the character's own mesh. Assign
     * MM_Pistol_Fire_Montage (or the rifle equivalent) in BP_GA_Fire.
     *
     * Deliberately NOT routed through the base class's MontageToPlay /
     * PlayOptionalMontage pair. That pair holds the ability open and defers its
     * payload to an Event.Montage.HitWindow notify, which is right for a swing and
     * wrong for a gun: the shot must land on the trigger pull, not several frames
     * later when an animation says so. So the trace still runs immediately and this
     * is purely cosmetic — a montage missing its notify cannot cost you the shot.
     *
     * Plays on the locally controlled pawn only, alongside the recoil kick. Remote
     * players will not see another player's fire animation until this is promoted to
     * a multicast; for a single-player slice that trade is invisible.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire|Montage")
    TObjectPtr<UAnimMontage> FireMontage;

    /** Playback rate for FireMontage. Raise it if the animation outlasts the
     *  weapon's fire rate and shots start swallowing each other's animation. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire|Montage")
    float FireMontagePlayRate = 1.f;

private:
    /** Plays FireMontage on the character's mesh. Null-safe; no-op if unassigned. */
    void PlayFireMontage(AGothicPlayerCharacter* Char) const;

protected:

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
     * Half-angle of the fire cone while NOT aiming, in degrees.
     *
     * ZERO BY DEFAULT — a perfect ray, exactly as the weapon behaved before spread
     * existed. The plumbing is here so accuracy can become the reason to aim after
     * the test build, but turning it on is a real accuracy change: the measured
     * 39 DPS baseline against Thralls was established with a perfect ray, so raising
     * this lowers effective DPS and wants re-measuring rather than eyeballing.
     *
     * A sane starting point when you do want it: 1.5 here against 0.2 aimed.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire|Spread",
              meta = (ClampMin = "0.0", ClampMax = "15.0"))
    float HipFireSpreadDegrees = 0.f;

    /** Half-angle of the fire cone while aiming — the mechanical payoff for giving
     *  up movement speed. Also zero for now; see HipFireSpreadDegrees. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire|Spread",
              meta = (ClampMin = "0.0", ClampMax = "15.0"))
    float ADSSpreadDegrees = 0.f;

    // BaselineGearPower removed 2026-08-04. It divided the ARMOR-average Gear
    // Power into weapon damage; WEAPON_ARCHETYPES.md now scales a weapon by its
    // own instance instead, and the Tier-1 divisor lives with the data that
    // defines it (AGothicPlayerCharacter::WeaponTierBaselineGearPower).

    /**
     * Extra vital-damage multiplier applied while the State.Read tag is active,
     * on top of the weapon's own vital multiplier. Default 1.5 turns a normal
     * 2.0x vital into 3.0x during the window. Set by the redesigned UGA_Read via
     * GE_ReadState; 1.0 here disables the bonus.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire|Scaling", meta = (ClampMin = 1))
    float ReadVitalDamageMultiplier = 1.5f;

    /**
     * How long State.Firing is held on the player ASC as a timed loose tag per shot,
     * so reactive enemy affixes have an observable "is shooting" window. GA_Fire
     * carries no ActivationOwnedTags and resolves in one frame, so nothing else
     * survives a deferred BT re-check. Applied on the AUTHORITY path; rapid fire just
     * restarts the window (UGothicAbilitySystemComponent::ApplyTimedLooseTag never
     * stacks the count).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire|Reaction", meta = (ClampMin = "0.0"))
    float FiringTagDuration = 0.4f;

    /** Ceiling on AbilityHaste's cooldown reduction, so stacked rolls can never
     *  drive the fire interval to zero. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire|Scaling", meta = (ClampMin = 0, ClampMax = 90))
    float MaxAbilityHastePercent = 60.f;

    /**
     * Fire rate used when the pawn has no weapon data to read — same fallback role
     * as Damage and TraceRange above. A weapon with a data asset always wins.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire", meta = (ClampMin = 1))
    float FallbackRoundsPerMinute = 171.f;

    /**
     * Noise loudness used when the pawn has no weapon data to read — same fallback
     * role as Damage, TraceRange and FallbackRoundsPerMinute above. A weapon with a
     * data asset always wins via UGothicWeaponData::FireNoiseLoudness, which is
     * where the meaning of the number is documented.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire", meta = (ClampMin = "0.0", UIMax = "3.0"))
    float FallbackFireNoiseLoudness = 1.f;

    /**
     * The GE the Jolt and Drumbeat weapon perks apply to a staggered victim
     * (WEAPON_PERK_TABLES.md, Verb Bucket A). Both perks want "stagger 1s", and
     * the duration is the ASSET's — nothing here can shorten a GE that is
     * authored for longer.
     *
     * Null by default and null is survivable: with nothing assigned here the
     * perks fall back to the active weapon's ShockStunEffect, and with neither
     * set they are inert and log once per shot at Verbose rather than failing
     * silently. Assign a real stagger GE in BP_GA_Fire — GE_Stagger exists in
     * content but is EMPTY, so pointing at it as-is buys nothing.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Fire|Perks")
    TSubclassOf<UGameplayEffect> PerkStaggerEffect;

    /**
     * The stagger GE Jolt and Drumbeat should apply, or null when neither this
     * ability nor the weapon has one. Prefers PerkStaggerEffect; falls back to
     * the weapon's ShockStunEffect so a Rig already carrying a stun asset works
     * before the dedicated one is authored.
     */
    TSubclassOf<UGameplayEffect> ResolvePerkStaggerEffect(const UGothicWeaponData* WeaponData) const;

    /**
     * Server-only. Traces on ECC_Weapon (the mesh, not the capsule), resolves the
     * vital point, applies damage, and multicasts impact feedback.
     */
    void PerformFireTrace(AGothicPlayerCharacter* Char);

    /**
     * Server-only. Reports the gunshot to the AI hearing sense so enemies that
     * cannot SEE the shot still learn about it. Runs on every shot, before the
     * trace and independent of it — a missed shot is exactly as loud as a hit.
     */
    void ReportFireNoise(AGothicPlayerCharacter* Char) const;
};
