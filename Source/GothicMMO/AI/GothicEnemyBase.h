// GothicEnemyBase.h
// Base class for all enemy types (Thrall, Retained, Feral, etc.).
// Unlike players, enemies host their own ASC directly — they don't persist.
// The AI Controller drives behavior; the ASC handles damage/buffs/debuffs.
//
// Attack model:
//   Enemies carry a GothicMeleeHitboxComponent attached to a weapon/hand bone.
//   Attack montages use AnimNotifyState_MeleeHitbox to open/close the damage
//   window. The hitbox applies damage via the standard GAS pipeline on overlap.
//   Binary range-check damage is gone — the player can dodge.
//
// Blueprint children: BP_Enemy_Thrall, BP_Enemy_Retained, etc.
// Each sets its own DefaultAttributeEffect, StartupAbilities, and hitbox tuning.

#pragma once

#include "CoreMinimal.h"
#include "Character/GothicCharacterBase.h"
#include "Perception/AIPerceptionTypes.h"
#include "GothicEnemyBase.generated.h"

class UAIPerceptionComponent;
class UWidgetComponent;
class UGothicMeleeHitboxComponent;

/** Enemy tier affects loot tables and XP reward. */
UENUM(BlueprintType)
enum class EEnemyTier : uint8
{
    Minion    UMETA(DisplayName = "Minion"),       // Basic fodder
    Elite     UMETA(DisplayName = "Elite"),        // Named, tougher variants
    Champion  UMETA(DisplayName = "Champion"),     // Mini-boss equivalent
    Boss      UMETA(DisplayName = "Boss"),         // Full boss encounter
};

UCLASS(Abstract)
class GOTHICMMO_API AGothicEnemyBase : public AGothicCharacterBase
{
    GENERATED_BODY()

public:
    AGothicEnemyBase();

    virtual void BeginPlay() override;

    // IGothicCombatInterface override — enemies ragdoll and drop loot on death.
    virtual void OnDeath_Implementation(AActor* Killer) override;

    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy")
    EEnemyTier GetEnemyTier() const { return EnemyTier; }

    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy")
    float GetExperienceReward() const { return ExperienceReward; }

    /** Called by the AI Controller when combat is entered/exited. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Enemy")
    void SetCombatTarget(AActor* NewTarget);

    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy")
    AActor* GetCombatTarget() const { return CombatTarget; }

    /** Returns the hitbox component for direct access from BT tasks if needed. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Combat")
    UGothicMeleeHitboxComponent* GetMeleeHitbox() const { return MeleeHitbox; }

protected:
    // -------------------------------------------------------------------------
    // Components
    // -------------------------------------------------------------------------

    /** For perceiving players (sight + hearing). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|AI")
    TObjectPtr<UAIPerceptionComponent> PerceptionComponent;

    /** World-space health bar widget, visible to all nearby players. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|UI")
    TObjectPtr<UWidgetComponent> HealthBarWidget;

    /**
     * Melee attack hitbox — attached to weapon/hand bone in Blueprint.
     * Anim notifies control the damage window.
     * Set DamageEffect, BaseDamage, and box extent per-enemy in Blueprint.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
    TObjectPtr<UGothicMeleeHitboxComponent> MeleeHitbox;

    // -------------------------------------------------------------------------
    // Data — set in Blueprint
    // -------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    EEnemyTier EnemyTier = EEnemyTier::Minion;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    float ExperienceReward = 50.f;

    /**
     * Delay before the corpse is destroyed (seconds).
     * Gives time for death animations and loot interaction.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    float CorpseLifetime = 10.f;

    /**
     * Optional loot table DataAsset.
     * Assign in Blueprint child to define what the enemy drops.
     * (Loot system implementation: future feature.)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    TObjectPtr<UDataAsset> LootTable;

    /** Radius within which players receive Selah on this enemy's death. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float SelahAwardRadius = 500.f;

    /** Amount of Selah awarded to each nearby player on death. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float SelahAwardAmount = 1.f;

    /** The GameplayEffect that awards Selah. Assign GE_SelahGain in BP_Enemy_Draugr. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    TSubclassOf<UGameplayEffect> SelahGainEffect;

    /**
     * Bone name to attach the hitbox to.
     * Override in Blueprint per-enemy type.
     * Common values: "weapon_r", "hand_r", "RightHand"
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Combat")
    FName HitboxAttachBone = FName("hand_r");

private:
    UPROPERTY()
    TObjectPtr<AActor> CombatTarget;

    UFUNCTION()
    void OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors);

    /** Delayed destruction after death animation plays out. */
    void DestroyCorpse();
    void AwardSelahToNearbyEmbers();
};