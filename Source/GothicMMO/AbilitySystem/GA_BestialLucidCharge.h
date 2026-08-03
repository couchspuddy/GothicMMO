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
//   4. Rewire the graph to arm the window at the LAUNCH — see below.
//
// THE WINDOW IS ANCHORED TO THE LAUNCH, NOT TO ACTIVATION
//
// It used to be armed in ActivateAbility, which is the wrong instant by the
// whole length of the windup. The graph waits 0.8s standing still before it
// calls LaunchCharacter, so 62% of a 1.3s window was spent sweeping the boss's
// own start position, and the window then expired roughly half a second before
// the leap actually finished — the back half of the charge could not damage
// anything, and the front half swept an empty patch of floor.
//
// So BeginChargeDamageWindow is BlueprintCallable and the graph calls it
// immediately after LaunchCharacter. The window closes on whichever comes
// first: the controller's OnLeapLanded, or ChargeDamageWindow elapsing.
//
// The Blueprint graph keeps doing the movement — LaunchCharacter, the waits,
// EndAbility. This class only owns the damage window and the unwind.

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

    /**
     * Arms the repeating damage sweep. Call from the graph IMMEDIATELY after
     * LaunchCharacter — that is the instant the charge becomes dangerous, and
     * arming it anywhere earlier spends the window on the windup.
     *
     * Idempotent: a second call while the window is already open is ignored, so
     * a graph that is wired twice, or a re-entrant branch, cannot restart the
     * clock or clear the already-hit set mid-charge.
     */
    UFUNCTION(BlueprintCallable, Category = "Charge")
    void BeginChargeDamageWindow();

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
     * BACKSTOP on how long the charge stays dangerous, measured from the LAUNCH
     * (seconds). Not from activation — see the file header.
     *
     * This is no longer the primary closer. The window normally ends on the
     * controller's OnLeapLanded, which is the real end of the travel; this only
     * catches a leap whose landing never arrives. So it wants to be an upper
     * bound on the traversal, and over-running it is cheap where undershooting
     * is not.
     *
     * WHY IT IS NOT 1.3. That number was justified as MaxChargeDistance /
     * ChargeSpeed = 1500 / 1200 = 1.25s, which is arithmetically fine and wrong
     * twice over: it counted the 0.8s stationary windup as travel, and 1200 is
     * the BT task's speed, not the graph's. The graph launches at 1800 XY /
     * 400 Z. Redone against the real numbers:
     *
     *   horizontal  1500 / 1800                     = 0.83s
     *   ballistic   2 * 400 / 980 (default gravity) = 0.82s
     *
     * Both say under a second, and both disagree with the measurement: she was
     * observed still in the air and ARRIVING 1.2-1.5s after the old ability end
     * at ~1.43s, i.e. roughly 1.8-2.0s of travel after the launch. Neither
     * derivation accounts for her GravityScale, her 1.5x capsule, or a landing
     * on a lower floor than the launch.
     *
     * 2.0 is the top of that measured arrival band. It is a STARTING POINT for a
     * measured pass, not a tuned value — measure the launch-to-landed interval
     * over several charges and set this a little above the worst one.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")
    float ChargeDamageWindow = 2.0f;

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

    /** Disarms the sweep and unbinds the landing hook. Safe when nothing is armed. */
    void EndChargeDamageWindow();

    /**
     * The controller's leap ended — stop sweeping. A charge that is over must
     * not keep running people over while the ability finishes its recovery.
     */
    UFUNCTION()
    void HandleLeapLanded(bool bLanded);

    /** The enemy controller of the avatar, or null. */
    class AGothicEnemyAIController* GetEnemyController() const;

    /** True between BeginChargeDamageWindow and EndChargeDamageWindow. */
    bool bChargeWindowOpen = false;

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
