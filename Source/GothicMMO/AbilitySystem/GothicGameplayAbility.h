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
     * Applies the ability's primary damage effect to a target.
     * Reads AttackPower from the source's attribute set.
     * @param Target        The actor to damage.
     * @param DamageEffect  The GameplayEffect class that defines the damage.
     * @param DamageMultiplier  Scales the raw damage (1.0 = standard hit).
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Ability")
    void ApplyDamageToTarget(
        AActor* Target,
        TSubclassOf<UGameplayEffect> DamageEffect,
        float DamageMultiplier = 1.0f);
};
