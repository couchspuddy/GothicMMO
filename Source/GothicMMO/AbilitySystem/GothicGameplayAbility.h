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
};