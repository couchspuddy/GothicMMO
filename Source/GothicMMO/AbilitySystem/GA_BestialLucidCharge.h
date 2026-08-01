// GA_BestialLucidCharge.h
// The Bestial Lucid's charge, with the part that hurts.
//
// WHERE THIS CAME FROM
//
// The charge's player damage was written once already, in
// UBTTask_BossCharge::ApplyChargeDamage — a per-tick sphere sweep around the
// boss, once per pawn per charge, 45 raw through the same Data.Damage
// SetByCaller contract as the melee hitbox. It builds, it is correct, and it
// has never run: no Behavior Tree in the project references that task. The
// charge is driven by an ability instead (BP_GA_BestialLucid_Charge), which
// launches the boss and then applies damage to the single Blackboard target if
// it happens to be close enough at one sampled moment.
//
// The difference matters. A charge is a line you have to not be standing in,
// for its whole length; a distance check against one actor after a fixed delay
// is a coin flip that ignores everyone else in the lane and misses the player
// it is aimed at whenever the timing is off.
//
// So the sweep moves here, to the ability that actually runs, mirroring
// BTTask_BossCharge.cpp's implementation. UBTTask_BossCharge is deliberately
// left in place and untouched — it is still the reference, and the numbers
// below are its numbers.
//
// SETUP
//   1. Reparent BP_GA_BestialLucid_Charge to this class.
//   2. Assign ChargeDamageEffect (GE_EnemyMeleeDamage, or the charge variant).
//      Without it the sweep is inert and says so once, loudly.
//   3. Set bUseDirectDamageFallback = false on that Blueprint, so its graph's
//      ApplyDamageToTarget node stops applying a second, range-free hit on top
//      of this one. (The automatic hitbox suppression in UGothicGameplayAbility
//      cannot catch it: the charge has no montage, so there is no notify state
//      to detect.)
//
// The Blueprint graph keeps doing the movement — LaunchCharacter, the waits,
// EndAbility. This class only adds the damage window, and starts it before
// Super::ActivateAbility runs the graph so a graph that ends the ability
// synchronously still tears the window down cleanly.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_BestialLucidCharge.generated.h"

UCLASS()
class GOTHICMMO_API UGA_BestialLucidCharge : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_BestialLucidCharge();

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
    /**
     * Raw damage sent as the Data.Damage SetByCaller when the charge connects.
     *
     * 45 raw becomes 57 after the pipeline (+20 boss AttackPower, -8 player
     * Defense), roughly 24% of the player's ~242 pool. Sized — in
     * UBTTask_BossCharge, from which this is carried over unchanged — to make
     * the charge the most expensive thing the boss does that the player can see
     * coming, without two-shotting anyone.
     *
     * True only since 2026-08-01. The context used to name the AIController as
     * instigator; a Controller has no ASC, so the +20 was silently dropped and
     * the charge landed 37. Fixed by stamping the boss pawn as instigator.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")
    float ChargeDamage = 45.f;

    /** Radius around the boss that counts as being run over (cm). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")
    float ChargeHitRadius = 120.f;

    /**
     * GE used for the charge hit. Same Data.Damage SetByCaller contract as the
     * melee hitbox. Unassigned means the charge moves the boss and nothing else.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")
    TSubclassOf<UGameplayEffect> ChargeDamageEffect;

    /**
     * How long the charge stays dangerous after activation (seconds).
     *
     * Wants to cover the travel, not the recovery. The Blueprint's own waits
     * total ~1.3s at a launch velocity of 1800, and the BT task's equivalent is
     * MaxChargeDistance/ChargeSpeed = 1500/1200 = 1.25s. The window closes on
     * its own even if the ability outlives the movement, so an overrun costs a
     * few harmless sweeps rather than a boss who damages on contact forever.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")
    float ChargeDamageWindow = 1.3f;

    /**
     * Seconds between sweeps.
     *
     * At 1800 uu/s the boss covers 90uu per 0.05s — comfortably less than
     * ChargeHitRadius, so nobody is tunnelled through between samples. This is
     * the ability-side stand-in for the BT task's per-tick check.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge",
              meta = (ClampMin = "0.01"))
    float ChargeSweepInterval = 0.05f;

private:
    /** One sweep: overlap pawns around the boss, damage each at most once. */
    void SweepChargeDamage();

    /** Arms the repeating sweep. Authority only. */
    void BeginChargeDamageWindow();

    /** Disarms it. Safe to call when nothing is armed. */
    void EndChargeDamageWindow();

    FTimerHandle ChargeSweepTimerHandle;

    /** World time the window opened, so the sweep can close it on schedule. */
    double ChargeWindowStartSeconds = 0.0;

    /**
     * Who this charge has already hit. A charge lasts over a second; without
     * this the player takes a hit every sweep they stay in contact, which is a
     * wall of damage rather than a single body check.
     *
     * Weak pointers rather than the BT task's raw fixed array — that array
     * exists because Behavior Tree node memory is a zeroed byte block with no
     * constructors run over it. An ability instance is an ordinary UObject and
     * has no such constraint.
     */
    TSet<TWeakObjectPtr<AActor>> HitPawnsThisCharge;
};
