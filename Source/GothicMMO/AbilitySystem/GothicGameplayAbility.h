// GothicGameplayAbility.h
// Base class for every active ability in GothicMMO.
// Subclass this in C++ for complex abilities, or create Blueprint children
// for designer-driven abilities. The key properties every ability needs
// are exposed to Blueprint here.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"  // for EGothicAbilitySlot
#include "GothicGameplayAbility.generated.h"

class UAnimMontage;

UCLASS(Abstract, Blueprintable)
class GOTHICMMO_API UGothicGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UGothicGameplayAbility();

    /** Which input slot activates this ability. Set per-ability in Blueprint. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Ability")
    EGothicAbilitySlot AbilitySlot;

    /**
     * Input tag for this ability. Must match an Enhanced Input action tag.
     * Example: "Input.Ability.LightAttack"
     * Set this in the Blueprint child class.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Ability")
    FGameplayTag AbilityInputTag;

    /** Returns the slot this ability belongs to. */
    EGothicAbilitySlot GetAbilitySlot() const { return AbilitySlot; }

    /** Returns the input tag for ASC binding. */
    FGameplayTag GetAbilityInputTag() const { return AbilityInputTag; }

    /**
     * Override to provide custom activation logic in Blueprint.
     * Always call the parent if you override in C++.
     */
    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

protected:
    /**
     * Convenience: returns the owning character's ability system component.
     * Null-safe — returns nullptr if called outside a valid actor context.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Ability")
    UGothicAbilitySystemComponent* GetGothicASC() const;

    // -------------------------------------------------------------------------
    // Optional montage lifecycle — any ability can play an attack montage
    // by assigning MontageToPlay in its Blueprint child. If unset, the
    // ability fires instantly as before.
    //
    // Flow: ActivateAbility → CommitAbility → PlayOptionalMontage()
    //   Montage set:    task starts → hit window notify → OnMontageHitWindow()
    //                   montage ends → OnMontageEnd() → EndAbility
    //   Montage unset:  PlayOptionalMontage() returns false → do damage
    //                   immediately → EndAbility
    //
    // The hit window fires via a WaitGameplayEvent task listening for
    // Event.Montage.HitWindow — add an AnimNotify_SendGameplayEvent at
    // the point in the montage where the swing connects.
    // -------------------------------------------------------------------------

    /**
     * Optional attack montage. If set, PlayOptionalMontage() plays it
     * and the ability stays alive for the montage's duration. If null,
     * the ability fires instantly.
     * Assign in the Blueprint child (BP_GA_X).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Ability|Montage")
    TObjectPtr<UAnimMontage> MontageToPlay;

    /** Playback rate for the attack montage. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Ability|Montage")
    float MontagePlayRate = 1.0f;

    /**
     * Starts PlayMontageAndWait if MontageToPlay is assigned.
     * Returns true if a montage was started (ability should NOT EndAbility
     * immediately — the montage callbacks will handle it).
     * Returns false if no montage is set (caller should do its work and
     * EndAbility itself).
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Ability|Montage")
    bool PlayOptionalMontage();

    /**
     * Called when the montage's hit window notify fires.
     * Override in derived classes to perform damage, AOE, stun, etc.
     * Default implementation does nothing — override is not required
     * (some montages are purely cosmetic with no hit window).
     */
    UFUNCTION()
    virtual void OnMontageHitWindow(FGameplayEventData Payload);

    /** Called when the montage finishes normally (OnCompleted). Default: EndAbility. */
    UFUNCTION()
    virtual void OnMontageEnd();

    /** Called when the montage is interrupted or cancelled. Default: EndAbility(cancelled). */
    UFUNCTION()
    virtual void OnMontageCancel();

    /**
     * Called when the montage STARTS blending out. Does not end the ability —
     * it only arms the watchdog below. Ending here was the bug that cut every
     * swing's recovery short.
     */
    UFUNCTION()
    virtual void OnMontageBlendOut();

    /** Watchdog fallback: ends the ability if no end callback ever arrives. */
    UFUNCTION()
    void OnMontageWatchdog();

    /** Cancels a pending watchdog. Called from both real end paths. */
    void ClearMontageWatchdog();

    // -------------------------------------------------------------------------
    // Centralized damage application
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // The legacy direct-damage path
    //
    // Melee abilities in this project have two ways to hurt somebody, and for a
    // while several of them used both at once:
    //
    //   1. The anim-notify hitbox. UAnimNotifyState_MeleeHitbox opens a
    //      UGothicMeleeHitboxComponent across the swing's active frames, and
    //      anything overlapping it takes damage. This is the real, spatial,
    //      dodgeable one — it has a reach, an arc, and a window.
    //
    //   2. ApplyDamageToTarget called straight from the ability graph. No
    //      trace, no reach, no arc: if the ability activated, the target is
    //      damaged, wherever they are standing.
    //
    // BP_GA_BestialLucid_Claw does both. PIE: the boss landed clean hits at
    // ~430uu — far outside any claw — and a single swing paid out as a burst
    // (225 HP in 0.66s across six applications, 22s among them appearing
    // twice). The second 22 is the graph's copy of the first.
    //
    // So the fallback now switches itself off wherever the hitbox is genuinely
    // wired: if THIS ability's montage carries the hitbox notify state AND the
    // avatar is an enemy that actually has a hitbox component, the graph's
    // direct application is a duplicate by construction and is dropped. The
    // decision is made per ability instance from the assets themselves, so an
    // ability whose montage has no notify — or has no montage at all, like
    // BP_GA_FeralPounce — keeps its only means of dealing damage.
    //
    // Player abilities cannot be affected: the check requires an
    // AGothicEnemyBase avatar, and GA_Fire / GA_Slicer / GA_HuntersStrike run
    // on the player.
    // -------------------------------------------------------------------------

    /**
     * Set false to suppress ApplyDamageToTarget unconditionally, whatever the
     * montage does. Only needed for an ability that damages from its graph and
     * ALSO drives a hitbox by some route this class cannot see — the charge,
     * for instance, whose sweep lives in C++ rather than in a notify.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Ability|Damage")
    bool bUseDirectDamageFallback = true;

    /**
     * True when this ability's montage opens a melee hitbox on this avatar, so
     * graph-side damage would double up. Resolved once per activation and
     * cached — InstancingPolicy is InstancedPerExecution, so "once per
     * activation" is once per instance.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Ability|Damage")
    bool IsHitboxDrivenMelee() const;

    /**
     * THE way an ability damages a target. Every damage site routes through
     * here — GA_Fire, GA_Slicer, GA_HuntersStrike, and every future kit.
     *
     * Owns the whole damage contract in one place:
     *   - Context carries SourceObject AND Instigator — the July 15 fix that
     *     closed "Killer: Unknown" in GA_Fire and silently never reached the
     *     other seven hand-rolled copies. Now it can't miss.
     *   - Magnitude goes out on GothicTags::Data_Damage as a SetByCaller.
     *     Semantics belong to the GE: whatever its Data.Damage modifier does
     *     with the number. Pass flat damage (GA_Fire's FinalDamage) or a
     *     multiplier (Hunter's Strike) per the GE's design.
     *   - Hit feedback fans out to every client via the enemy's existing
     *     MulticastOnHit → OnHitFeedback path. Four of five damage sites told
     *     no client anything until July 17; with the fanout living here, a
     *     silent damage site is no longer a bug a new ability can have.
     *     (The multicast only propagates when this runs with authority, which
     *     is where every damage trace already runs.)
     *
     * @param Target        The actor to damage.
     * @param DamageEffect  The GameplayEffect class that defines the damage.
     * @param DamageValue   SetByCaller magnitude on Data.Damage.
     * @param ImpactPoint   Where the hit VFX should appear. ZeroVector =
     *                      fall back to the target's actor location.
     * @param bWasVital     Drives the binary vital-hit tell in OnHitFeedback.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Ability")
    void ApplyDamageToTarget(
        AActor* Target,
        TSubclassOf<UGameplayEffect> DamageEffect,
        float DamageValue = 1.0f,
        FVector ImpactPoint = FVector::ZeroVector,
        bool bWasVital = false);

private:
    /** Tri-state cache for IsHitboxDrivenMelee: -1 unresolved, 0 no, 1 yes. */
    mutable int8 CachedHitboxDrivenMelee = -1;

    /** Timer for the montage end watchdog armed at blend-out. */
    FTimerHandle MontageWatchdogHandle;
};