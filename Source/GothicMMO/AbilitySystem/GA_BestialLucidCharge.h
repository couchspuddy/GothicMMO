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

    /**
     * Extra reach beyond the boss's own body that counts as being run over (cm).
     *
     * NOT AN ABSOLUTE RADIUS. This used to be ChargeHitRadius = 120, the radius
     * of a sphere centred on the avatar's actor origin — and for a pawn this
     * size that sphere can never contain anybody. The Bestial Lucid's origin is
     * her capsule CENTRE, which measures ~291uu above a floor-standing player's
     * origin. 291 > 120 on the Z axis alone, so the sphere missed at every
     * horizontal distance including zero: six charges in a row, minimum 3D
     * separation 324-394uu, zero charge damage events. One leap that touched
     * down 143uu away horizontally still measured 324uu in 3D.
     *
     * The sweep now overlaps the avatar's actual CAPSULE — its scaled radius and
     * half-height, at the capsule's own world location — and this value inflates
     * that capsule on both axes. So the body check is sized by the creature: a
     * Thrall gets a Thrall-sized one, the boss gets a boss-sized one, and no
     * magic radius has to be retuned per enemy.
     *
     * 30 because the capsule IS the body; this is the bit of grace either side
     * of it, not the hit volume. The boss's effective capsule is 90uu radius
     * (60 * 1.5 scale) x 379.5uu half-height (253 * 1.5), so 30 makes her charge
     * a 120uu-radius body check that still cannot touch someone she flies past —
     * which is exactly what the old 120 was meant to be, and what raising it to
     * ~400 to clear the Z gap would have destroyed.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")
    float ChargeHitInflation = 30.f;

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
     * THIS IS NOT A TUNING VALUE. The window's normal close is the landing:
     * HandleLeapLanded fires on the controller's OnLeapLanded and the landing
     * has always arrived first, so this backstop has never actually fired. It
     * exists for the leap that never lands — one eaten by geometry, or a
     * controller that loses the flight — and its only job is to stop the boss
     * sweeping forever. Nothing about the charge's feel is set here.
     *
     * Measured, not derived. Launch-to-landing over nine guarded leaps ran
     * 0.824-0.943s, median 0.828. 1.3 is the worst of those plus ~35% headroom.
     *
     * The old 2.0 was reasoned from an observed ability-end time rather than
     * measured from launch to landing, and it put the backstop more than twice
     * the real traversal away — harmless while the landing keeps closing the
     * window, and a very long tail of free sweeping on the day it does not.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")
    float ChargeDamageWindow = 1.3f;

    /**
     * Seconds between sweeps.
     *
     * At 1800 uu/s the boss covers 90uu per 0.05s — comfortably less than the
     * width of her inflated capsule, so nobody is tunnelled through between
     * samples. This is the ability-side stand-in for the BT task's per-tick
     * check.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge",
              meta = (ClampMin = "0.01"))
    float ChargeSweepInterval = 0.05f;

private:
    /**
     * One sweep: overlap pawns around the boss, damage each at most once.
     *
     * bFinalSweep marks the synchronous sweep run from the close path. It skips
     * the backstop expiry check and never closes the window itself — the caller
     * is already closing it, and letting this re-enter EndChargeDamageWindow
     * would recurse.
     */
    void SweepChargeDamage(bool bFinalSweep = false);

    /**
     * Runs the touchdown sweep, disarms, and emits the window summary.
     * Safe when nothing is armed. ClosedBy names the cause for the summary line.
     */
    void EndChargeDamageWindow(const TCHAR* ClosedBy = TEXT("ability-end"));

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

    /**
     * Re-entrancy guard for the close path. The final sweep runs INSIDE
     * EndChargeDamageWindow and every failure branch of a sweep closes the
     * window, so without this a final sweep that finds no ASC would call back
     * into the function that is calling it.
     */
    bool bClosingChargeWindow = false;

    FTimerHandle ChargeSweepTimerHandle;

    /** World time the window opened, so the sweep can close it on schedule. */
    double ChargeWindowStartSeconds = 0.0;

    // ── Window telemetry ─────────────────────────────────────────────────────
    //
    // The closest the boss's body ever got to the player during one charge, in
    // both measurements, tracked per sweep so the summary line can state it
    // outright. This is the number that decides whether the capsule sweep is
    // geometrically capable of connecting at all, and the last two verification
    // runs had to reconstruct it by hand from 0.1s probe samples taken from
    // outside the ability — at a sampling rate coarser than the sweep's own.

    /** Sweeps run this window, final touchdown sweep included. */
    int32 ChargeSweepsThisWindow = 0;

    /** Closest horizontal (XY) actor-to-actor approach to a player pawn, uu. */
    float ChargeMinDist2D = TNumericLimits<float>::Max();

    /** Closest 3D actor-to-actor approach to a player pawn, uu. */
    float ChargeMinDist3D = TNumericLimits<float>::Max();

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
