// GA_HuntersStrike.h
// "Hunter's Strike" — the default light melee attack.
// This is a C++ ability that:
//   1. Plays a montage (set in Blueprint child via base class MontageToPlay)
//   2. Opens a damage window via an anim notify (Event.Montage.HitWindow)
//   3. Sphere-traces for targets in front of the player
//   4. Applies GE_MeleeDamage to each target hit
//   5. Ends cleanly when the montage completes (cancels if interrupted)
//
// Blueprint child: BP_GA_HuntersStrike
//   - Set MontageToPlay to your melee anim montage (inherited from base)
//   - Set DamageEffectClass to GE_MeleeDamage
//   - Bind to slot LightAttack

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "Camera/CameraShakeBase.h"
#include "GA_HuntersStrike.generated.h"

UCLASS()
class GOTHICMMO_API UGA_HuntersStrike : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_HuntersStrike();

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

protected:
    // MontageToPlay is inherited from UGothicGameplayAbility — set in Blueprint.

    /** The GameplayEffect that deals the damage. Assign in Blueprint. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UGameplayEffect> DamageEffectClass;

    /** Radius of the melee hit sphere trace (cm). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    float HitSphereRadius = 80.f;

    /** How far forward the sphere trace reaches (cm). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    float HitRange = 200.f;

    /**
     * Coefficient on the wielder's AttackPower. 1.0 = one AttackPower's worth
     * of raw damage; 0.5 = half.
     *
     * Renamed from DamageMultiplier, which described the intent but not the
     * behaviour: the value was passed straight through as the flat Data.Damage
     * SetByCaller, and no damage GE in the project has multiplier semantics
     * (all three bind Data.Damage to IncomingDamage as a plain AddBase). At the
     * Blueprint's 0.5 that meant the strike sent 0.5 raw damage and landed
     * max(1, 0.5 + AttackPower - Defense) — measured as exactly 1.00 against
     * everything in PIE, because AttackPower was also resolving to 0.
     *
     * Now resolved against AttackPower before it is sent, so 0.5 with the
     * player's AttackPower of 15 sends 7.5 raw. Note the pipeline adds
     * AttackPower AGAIN as the attacker's flat contribution — that is the
     * project-wide shape of every damage source, not something specific here.
     *
     * A CoreRedirect in DefaultEngine.ini carries the Blueprint's 0.5 across
     * the rename.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    float AttackPowerCoefficient = 1.0f;

    /** Tags on targets that prevent them from being hit. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    FGameplayTagContainer ImmunityTags;

    /**
     * How long State.Attacking is held on the player ASC as a timed loose tag on the
     * INSTANT-FALLBACK path (no montage assigned), so it survives at least one enemy
     * BT re-evaluation. Without it the fallback resolves and EndAbility()s in one
     * frame, ActivationOwnedTags' State.Attacking lives exactly that frame, and
     * GothicBTDecorator_PlayerHasAnyTag's deferred re-check always sees it already
     * gone (PIE-verified). Applied count-based, so it rides alongside the
     * ActivationOwnedTags instance a future montage path would hold without stripping it.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike|Reaction", meta = (ClampMin = "0.0"))
    float AttackingTagDuration = 0.4f;

    /**
     * How long State.Whiffed is held on the player ASC when the swing's trace damages
     * nothing — the Retaliator counter-window (ENEMY_AFFIXES.md Retaliator row). Tags
     * only this pass: GA_EnemyRetaliate is not authored here.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike|Reaction", meta = (ClampMin = "0.0"))
    float WhiffTagDuration = 0.4f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UGameplayEffect> SuperGainOnHitEffect;

    /**
     * Super meter granted per enemy hit by a strike, sent to SuperGainOnHitEffect
     * on the GothicTags::Data_SuperMeter SetByCaller. Was a hardcoded 15 in the .cpp;
     * it is the reference point the ranged per-shot value was derived from, so it
     * needs to be authorable alongside it.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike", meta = (ClampMin = "0.0"))
    float SuperGainOnHit = 15.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UGameplayEffect> SuperGainOnKillEffect;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hunter's Strike")
    TSubclassOf<UCameraShakeBase> CameraShakeClass;

    // Base class montage callbacks — override hit window for our damage logic
    virtual void OnMontageHitWindow(FGameplayEventData Payload) override;

    /**
     * Performs the actual sphere trace and applies damage. Server only.
     *
     * This is an AoE, which nothing previously said out loud: it is a
     * SweepMultiByChannel and it damages EVERY distinct actor the swept sphere
     * touches, once each. Confirmed in PIE — one swing hit two Thralls standing
     * ~180uu apart. With HitSphereRadius 80 and HitRange 200 the swept volume is
     * a 160uu-wide capsule reaching 250uu forward, so a cluster of enemies takes
     * the strike together. Deliberate, and left as is.
     */
    void PerformMeleeTrace();
};