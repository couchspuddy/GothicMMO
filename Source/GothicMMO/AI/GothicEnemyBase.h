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
#include "Engine/NetSerialization.h"   // FVector_NetQuantize
#include "GothicEnemyBase.generated.h"


class UAIPerceptionComponent;
class UWidgetComponent;
class UGothicVitalPointComponent;
class UGothicCombatStateComponent;
class AGothicEncounterVolume;
class UNiagaraSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyDied, AGothicEnemyBase*, DeadEnemy);

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

    /**
     * The Accursed's name — surfaced on the health bar and WBP_SelahPrompt.
     * Every enemy is someone the world failed, not an unnamed hitpoint bag;
     * this is the one-property cost of making that visible to the player.
     * Public alongside its sibling getters: GothicEnemyHealthBarWidget calls
     * this from an unrelated class, so it has to be reachable from outside
     * the AGothicEnemyBase hierarchy, same as GetEnemyTier()/GetExperienceReward().
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy")
    FText GetAccursedName() const { return AccursedName; }

    /** Called by the AI Controller when combat is entered/exited. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Enemy")
    void SetCombatTarget(AActor* NewTarget);

    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy")
    AActor* GetCombatTarget() const { return CombatTarget; }
    /**
     * Assigns this enemy to a pack, registering with UGothicPackSubsystem.
     * Handles re-registration: moving between packs unregisters from the old
     * one first. Exists as a setter (rather than BeginPlay reading the
     * property directly and nothing else) because wave-spawned enemies get
     * their PackID stamped AFTER SpawnActor completes — BeginPlay has
     * already run by then and saw NAME_None. Both paths converge here:
     * BeginPlay calls it with the serialized value; spawn code calls it
     * with the spawn point's stamp.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Pack")
    void SetPackID(FName NewPackID);

    UFUNCTION(BlueprintPure, Category = "Gothic|Pack")
    FName GetPackID() const { return PackID; }

    UFUNCTION(BlueprintPure, Category = "Gothic|Pack")
    float GetPackRegroupDuration() const { return PackRegroupDuration; }

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** Broadcast when this enemy dies. AGothicEncounterVolume subscribes to track encounter completion. */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|Enemy")
    FOnEnemyDied OnEnemyDied;
    /** Cosmetic hit reaction, server → everyone. Impact point is net-quantized; this is VFX placement, not hit validation. */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastOnHit(FVector_NetQuantize ImpactLocation, bool bWasVital, float DamageAmount);

    /** Blueprint hook for impact VFX/SFX. Runs everywhere. bWasVital drives the binary audio tell. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Feedback")
    void OnHitFeedback(FVector ImpactLocation, bool bWasVital);

    /**
     * Set by AGothicEncounterVolume at BeginPlay if this enemy belongs to a staged
     * encounter. Null means open-world behavior — individually collectible, no
     * gating, exactly as before. Non-null means CollectAllNearbyCorpses skips it;
     * its Selah is only available through the encounter's shared prompt.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Encounter")
    TObjectPtr<AGothicEncounterVolume> OwningEncounter;
    
    /**
     * Pack membership. NAME_None (default) = packless — the boss and the
     * Retained opt out by doing nothing. EditAnywhere so hand-placed
     * instances can be grouped in the level directly, same
     * grouping-is-a-level-design-decision philosophy as
     * bAggroEnemiesOnOverlap. Wave-spawned enemies get this stamped by
     * their spawn point via SetPackID — see AGothicEnemySpawnPoint.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|Pack")
    FName PackID = NAME_None;

    /** How long this enemy holds the guard pose when a packmate falls.
     *  Per-enemy rather than per-pack so a future heavier variant can
     *  recover slower than the Thralls around it. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|Pack")
    float PackRegroupDuration = 2.5f;
    
    /** Amount of Selah awarded to each nearby player on death. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float SelahAwardAmount = 1.f;

    /** The GameplayEffect that awards Selah. Assign GE_SelahGain in BP_Enemy_Draugr. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    TSubclassOf<UGameplayEffect> SelahGainEffect;
    
    /**
     * Cosmetic death reaction, fired by the server onto every machine including
     * itself. Exists because OnDeath_Implementation only ever runs on the server
     * (called from UGothicAttributeSet::PostGameplayEffectExecute), so anything
     * a client needs to *see* has to come through here, not through OnDeath.
     */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastOnDeath(AActor* Killer);

    /** Blueprint hook for death VFX/SFX. Runs everywhere. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Feedback")
    void OnDeathFeedback(AActor* Killer);
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

    /**
     * The Accursed's name. Raw storage stays protected, matching EnemyTier/
     * ExperienceReward's convention — external reads go through the public
     * GetAccursedName() getter above, not through this field directly.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    FText AccursedName;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    float ExperienceReward = 50.f;

    /**
     * Delay before the corpse is destroyed (seconds).
     * Gives time for death animations and loot interaction.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    float CorpseLifetime = 120.f;

    /**
     * Loot table defining what this enemy can drop on death.
     * Assign in Blueprint child (e.g. DA_LootTable_Thrall).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    TObjectPtr<class UGothicLootTable> LootTable;
    
    /** Radius within which players receive Selah on this enemy's death. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float SelahAwardRadius = 500.f;
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
    TObjectPtr<UGothicCombatStateComponent> CombatStateComponent;

    // -------------------------------------------------------------------------
    // Impact VFX — spawned on every client from MulticastOnHit.
    // Assign in the enemy Blueprint. Different effects for body vs vital.
    // -------------------------------------------------------------------------

    /** Niagara system for body (non-vital) hits. Spawned at the impact point. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Feedback")
    TObjectPtr<UNiagaraSystem> BodyHitEffect;

    /** Niagara system for vital hits. Spawned at the impact point — more dramatic. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Feedback")
    TObjectPtr<UNiagaraSystem> VitalHitEffect;

    /** Niagara system spawned at the enemy's location on death. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Feedback")
    TObjectPtr<UNiagaraSystem> DeathEffect;



private:
    UPROPERTY()
    TObjectPtr<AActor> CombatTarget;

    UFUNCTION()
    void OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors);
    /** Guards against the multicast and a local call both running cosmetics. */
    bool bDeathCosmeticsPlayed = false;

    /** The half of death that is purely visual — runs on server and clients alike. */
    void PlayDeathCosmetics();
    /** Delayed destruction after death animation plays out. */
    void DestroyCorpse();
};