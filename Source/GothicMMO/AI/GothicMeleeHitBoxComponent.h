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

    UFUNCTION()
    void OnHitboxOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);
};