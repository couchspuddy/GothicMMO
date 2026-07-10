// GothicEnemyBase.h
// Base class for all enemy types (Draugr, Vampire Thrall, Wraith, etc.).
// Unlike players, enemies host their own ASC directly — they don't persist.
// The AI Controller drives behavior; the ASC handles damage/buffs/debuffs.
//
// Blueprint children: BP_Enemy_Draugr, BP_Enemy_VampireThrall, etc.
// Each sets its own DefaultAttributeEffect and StartupAbilities.

#pragma once

#include "CoreMinimal.h"
#include "Character/GothicCharacterBase.h"
#include "Perception/AIPerceptionTypes.h"
#include "GothicEnemyBase.generated.h"

class UAIPerceptionComponent;
class UWidgetComponent;
class UGothicVitalPointComponent;
class UGothicCombatStateComponent;

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
    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    void AwardSelahToNearbyEmbers();
    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    static void CollectAllNearbyCorpses(AActor* Collector, float Radius, UObject* WorldContext);
    UPROPERTY(BlueprintReadWrite, Category = "Gothic|Selah")
    bool bCollected = false;
    
    /** Amount of Selah awarded to each nearby player on death. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float SelahAwardAmount = 1.f;

    /** The GameplayEffect that awards Selah. Assign GE_SelahGain in BP_Enemy_Draugr. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    TSubclassOf<UGameplayEffect> SelahGainEffect;
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
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|VitalPoint")
    TObjectPtr<UGothicVitalPointComponent> VitalPointComponent;

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
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
    TObjectPtr<UGothicCombatStateComponent> CombatStateComponent;



private:
    UPROPERTY()
    TObjectPtr<AActor> CombatTarget;

    UFUNCTION()
    void OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors);

    /** Delayed destruction after death animation plays out. */
    void DestroyCorpse();
};
