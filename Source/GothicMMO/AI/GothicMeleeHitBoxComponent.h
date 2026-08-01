// GothicMeleeHitboxComponent.h
// A collision volume attached to an enemy's weapon or hand bone.
// Disabled by default. Anim notifies toggle it on during the
// active frames of an attack animation.
//
// When enabled, overlapping player capsules receive damage via
// the GAS pipeline (IncomingDamage attribute, same as all other
// damage sources in Vigil).
//
// Setup per-enemy Blueprint:
//   1. Add this component to the enemy Blueprint
//   2. Attach it to the appropriate bone (weapon_r, hand_r, etc.)
//   3. Set HitboxExtent to match the weapon's swing arc
//   4. Assign DamageEffect (GE_EnemyMeleeDamage)
//   5. Set BaseDamage per-enemy type
//   6. In the attack montage, add:
//        AN_EnableHitbox  at the start of active frames
//        AN_DisableHitbox at the end of active frames

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "GameplayTagContainer.h"
#include "GothicMeleeHitboxComponent.generated.h"

class UGameplayEffect;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GOTHICMMO_API UGothicMeleeHitboxComponent : public UBoxComponent
{
    GENERATED_BODY()

public:
    UGothicMeleeHitboxComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    /**
     * Called by anim notify to open the damage window.
     * Enables collision, clears the already-hit list.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Combat")
    void EnableHitbox();

    /**
     * Called by anim notify to close the damage window.
     * Disables collision.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Combat")
    void DisableHitbox();

    /** Whether the hitbox is currently in its active window. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Combat")
    bool IsHitboxActive() const { return bHitboxActive; }

    /**
     * True if this hitbox damaged Target within the last WindowSeconds.
     *
     * Exists so graph-side direct damage can tell that the swing already paid
     * out and stand down, WITHOUT inspecting the montage's notify list.
     * UGothicGameplayAbility::IsHitboxDrivenMelee does inspect that list, which
     * makes the anti-double-apply safety a property of the asset: FeralRend is
     * suppressed only because it shares Melee_A_Montage with the Claw, so
     * retiming or removing that notify would quietly re-arm the duplicate.
     * This answer comes from what actually happened instead.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Combat")
    bool HasRecentlyDamaged(const AActor* Target, float WindowSeconds = 1.0f) const;

protected:
    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Tuning — set per-enemy in Blueprint
    // -------------------------------------------------------------------------

    /** The GameplayEffect that applies IncomingDamage. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Combat")
    TSubclassOf<UGameplayEffect> DamageEffect;

    /**
     * Base damage for this attack. Passed as SetByCaller magnitude
     * on tag "Data.Damage", same as all other damage in Vigil.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Combat")
    float BaseDamage = 15.f;

    /**
     * Tags on the target that prevent damage (e.g. State.Dead, State.Invulnerable).
     * Targets with any of these tags are skipped.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Combat")
    FGameplayTagContainer ImmunityTags;

private:
    bool bHitboxActive = false;

    /** Actors already hit during this swing — prevents multi-hit per attack. */
    UPROPERTY()
    TArray<TObjectPtr<AActor>> AlreadyHitThisSwing;

    /**
     * When each actor was last damaged by this hitbox (world seconds).
     * Survives DisableHitbox — HasRecentlyDamaged has to answer honestly for a
     * graph-side call that arrives a frame or two after the window closed.
     *
     * Bounded by PruneDamageStamps, called at the top of every swing.
     */
    TMap<TWeakObjectPtr<const AActor>, double> LastDamagedTime;

    /**
     * How long a damage stamp is worth keeping (seconds).
     *
     * Not a tuning knob — a bookkeeping horizon. The only reader is
     * HasRecentlyDamaged, whose largest caller-supplied window is 1s, so 10 is
     * an order of magnitude of slack over anything that can be asked for.
     */
    static constexpr double DamageStampHorizon = 10.0;

    /** Drops expired and stale-weak-pointer entries from LastDamagedTime. */
    void PruneDamageStamps();

    UFUNCTION()
    void OnHitboxOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);
};