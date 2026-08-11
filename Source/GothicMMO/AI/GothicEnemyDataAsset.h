// GothicEnemyDataAsset.h
// Data-driven enemy classification: Tier × Shape × Role.
// One asset per enemy KIND (DA_Enemy_Thrall, DA_Enemy_Retained, …), assigned on
// the enemy Blueprint's EnemyData field. On spawn the pawn configures itself from
// it — base attributes from the numbers here, Behaviour Tree from the Role map —
// so a new enemy variant is a new data asset, not a new C++ branch.
//
// Scope (this pass): Tier drives HP/defense; Role drives BT assignment. Shape,
// and both flinch thresholds, are STORED but not yet evaluated — flinch lives in
// a later pass, shape damage modifiers in the one after. The fields exist now so
// the data authored against this asset does not have to be revisited when those
// systems arrive.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GothicEnemyDataAsset.generated.h"

class UBehaviorTree;

/**
 * Durability/threat class. Drives base HP and defense now; the flinch thresholds
 * below are read against it in a later pass. Boss is the top tier AND the marker
 * for "no vital flinch" — see AllowsVitalFlinch below, which is why there is no
 * separate bIsBoss bool (a redundant flag that could disagree with the tier).
 */
UENUM(BlueprintType)
enum class EGothicEnemyTier : uint8
{
    Fodder   UMETA(DisplayName = "Fodder"),
    Standard UMETA(DisplayName = "Standard"),
    Elite    UMETA(DisplayName = "Elite"),
    Boss     UMETA(DisplayName = "Boss"),
};

/**
 * Material/body class. STORED ONLY this pass — the per-shape incoming-damage
 * modifiers (e.g. Plated shrugging off body shots, Shrouded resisting until
 * revealed) land in a later pass and will read this off the pawn.
 */
UENUM(BlueprintType)
enum class EGothicEnemyShape : uint8
{
    Flesh    UMETA(DisplayName = "Flesh"),
    Plated   UMETA(DisplayName = "Plated"),
    Shrouded UMETA(DisplayName = "Shrouded"),
};

/**
 * Combat role. Drives which Behaviour Tree the AI controller runs. No ranged
 * roles this pass — Swarmer (rush) and Bruiser (heavy) both map to existing
 * melee trees today.
 */
UENUM(BlueprintType)
enum class EGothicEnemyRole : uint8
{
    Swarmer UMETA(DisplayName = "Swarmer"),
    Bruiser UMETA(DisplayName = "Bruiser"),
};

/**
 * The classification + tuning for one enemy kind. Everything is EditDefaultsOnly:
 * the asset IS the data, there are no per-instance overrides here.
 */
UCLASS(BlueprintType)
class GOTHICMMO_API UGothicEnemyDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // -------------------------------------------------------------------------
    // Classification
    // -------------------------------------------------------------------------

    /** Durability/threat tier. Drives base HP and defense (see below). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Class")
    EGothicEnemyTier Tier = EGothicEnemyTier::Standard;

    /** Body/material shape. Stored now; damage modifiers are a later pass. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Class")
    EGothicEnemyShape Shape = EGothicEnemyShape::Flesh;

    /** Combat role. Selects the Behaviour Tree via RoleBehaviorTrees. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Class")
    EGothicEnemyRole Role = EGothicEnemyRole::Swarmer;

    // -------------------------------------------------------------------------
    // Base attributes — applied to the ASC at spawn (server), base values.
    // -------------------------------------------------------------------------

    /** Spawn MaxHealth (and starting Health). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Attributes",
        meta = (ClampMin = "1.0"))
    float MaxHealth = 100.f;

    /** Base AttackPower attribute value. Feeds the standard damage pipeline. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Attributes",
        meta = (ClampMin = "0.0"))
    float BaseAttack = 10.f;

    /** Base Defense attribute value (flat mitigation, per GothicAttributeSet). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Attributes",
        meta = (ClampMin = "0.0"))
    float BaseDefense = 0.f;

    // -------------------------------------------------------------------------
    // Flinch thresholds — STORED ONLY this pass (flinch eval is a later pass).
    // Fraction (0..1) of a hit / accumulated poise that trips the reaction.
    // -------------------------------------------------------------------------

    /** Threshold for a body-shot flinch. Not evaluated yet. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Flinch",
        meta = (ClampMin = "0.0"))
    float BodyFlinchThreshold = 0.f;

    /** Threshold for a vital-shot flinch. Not evaluated yet, and only meaningful
     *  where AllowsVitalFlinch() is true (i.e. non-Boss tiers). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Flinch",
        meta = (ClampMin = "0.0"))
    float VitalFlinchThreshold = 0.f;

    // -------------------------------------------------------------------------
    // Behaviour — Role → Behaviour Tree.
    // -------------------------------------------------------------------------

    /**
     * Which tree each role runs. The AI controller looks this up by the asset's
     * Role at possession. A map rather than one BT field so a single data asset
     * kind can be re-roled without rewiring, and so future roles slot in as data.
     * Populate in the editor: today both Swarmer and Bruiser point at
     * BT_EnemyCombat (no dedicated Bruiser tree exists yet).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Behaviour")
    TMap<EGothicEnemyRole, TObjectPtr<UBehaviorTree>> RoleBehaviorTrees;

    /** The Behaviour Tree for this asset's Role, or null if unmapped. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy|Behaviour")
    UBehaviorTree* ResolveBehaviorTree() const;

    /**
     * Whether a vital hit can flinch this enemy. False for Boss tier — bosses do
     * not stagger on a vital by design. Codified now so the deferred flinch pass
     * has a single, findable rule rather than re-deriving it from the tier.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy|Flinch")
    bool AllowsVitalFlinch() const { return Tier != EGothicEnemyTier::Boss; }
};
