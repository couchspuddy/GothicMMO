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
class UGothicLootTable;
class UGothicMeleeHitboxComponent;
class UGothicVitalPointComponent;
class AGothicEncounterVolume;

// Forward-declared here so both GothicEnemyBase and GothicEncounterVolume
// resolve the same delegate type from a single declaration.
class AGothicEnemyBase;  // needed before the delegate macro expands

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnEnemyDied,
    AGothicEnemyBase*, DeadEnemy);

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

    // -----------------------------------------------------------------
    // Encounter integration — the encounter volume binds to OnEnemyDied
    // to track roster deaths without polling.
    // -----------------------------------------------------------------

    /** Broadcast from OnDeath — the encounter volume listens to this. */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|Enemy")
    FOnEnemyDied OnEnemyDied;

    /** Set by the encounter volume in BeginPlay. Null for free-roaming enemies. */
    UPROPERTY()
    TObjectPtr<AGothicEncounterVolume> OwningEncounter;

    // -----------------------------------------------------------------
    // Accursed identity — revealed during the Selah moment, not combat.
    // -----------------------------------------------------------------

    /** The human name this Accursed had before the Bleed took them.
     *  Set per-instance in the level or per-Blueprint default. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|Lore")
    FText AccursedName;

    UFUNCTION(BlueprintPure, Category = "Gothic|Lore")
    FText GetAccursedName() const { return AccursedName; }

    // -----------------------------------------------------------------
    // Pack coordination — optional stamp for Thrall pack grouping.
    // -----------------------------------------------------------------

    /** Moves this enemy between packs, keeping GothicPackSubsystem's registry in
     *  step. Was an inline setter that only wrote the field — so the subsystem's
     *  Packs map stayed permanently empty and no pack ever regrouped. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI")
    void SetPackID(FName NewPackID);

    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    FName GetPackID() const { return PackID; }

    /** Public read access for encounter volume Selah accumulation. */
    float GetSelahAwardAmount() const { return SelahAwardAmount; }
    TSubclassOf<UGameplayEffect> GetSelahGainEffect() const { return SelahGainEffect; }
    
    /** Multicast RPC — server broadcasts hit feedback to all clients. */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastOnHit(FVector HitLocation, bool bVitalHit, float DamageAmount);
    
    /** Per-enemy regroup pause duration after a pack member dies. Set in Blueprint. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI")
    float PackRegroupDuration = 1.5f;

    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetPackRegroupDuration() const { return PackRegroupDuration; }

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

    /** Moving vital point + amber overlay. Configure BoneName locations and
     *  VitalOverlayMaterial per enemy Blueprint (rig-agnostic). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
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
     * Loot table for this enemy type. Assign per Blueprint child.
     * On death, RollDrop() picks a weighted random item and spawns a world pickup.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    TObjectPtr<UGothicLootTable> LootTable;

    /**
     * Halts or resumes this enemy when State.Stunned is gained or lost.
     *
     * State.Stunned previously did nothing to an enemy. It drove an animation
     * flag on GothicEnemyAnimInstance and appeared in a couple of PLAYER
     * abilities' ActivationBlockedTags, so an enemy that "got stunned" played a
     * stun pose while continuing to path and swing. This stops the behaviour
     * tree and the movement so the tag means what it looks like.
     */
    UFUNCTION()
    void HandleStunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

    // SelahAwardRadius (500uu) and AwardSelahToNearbyEmbers() lived here. The
    // per-kill award was replaced by encounter-based collection; the function had
    // no callers and was not BlueprintCallable, so nothing could reach it. Both
    // removed so the remaining Selah surface is the one that actually runs.

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

    /**
     * How fast (deg/sec) the enemy turns to keep facing its focus target.
     * Higher tracks harder; lower is more flankable. A big boss wants this
     * lowish (~180) so getting behind her is real counterplay; trash can be
     * snappier. Applied to RotationRate.Yaw in BeginPlay.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Combat")
    float TurnRateDegrees = 300.f;

private:
    UPROPERTY()
    TObjectPtr<AActor> CombatTarget;

    /** Pack group identifier — enemies with the same PackID coordinate. */
    UPROPERTY(EditAnywhere, Category = "Gothic|AI")
    FName PackID = NAME_None;

    UFUNCTION()
    void OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors);

    /** Delayed destruction after death animation plays out. */
    void DestroyCorpse();
    void SpawnLootDrop();
};