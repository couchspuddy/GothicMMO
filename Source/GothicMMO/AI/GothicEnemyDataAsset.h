// GothicEnemyDataAsset.h
// Data-driven enemy classification: Tier × Shape × Role.
// One asset per enemy KIND (DA_Enemy_Thrall, DA_Enemy_Retained, …), assigned on
// the enemy Blueprint's EnemyData field. On spawn the pawn configures itself from
// it — base attributes from the numbers here, Behaviour Tree from the Role map —
// so a new enemy variant is a new data asset, not a new C++ branch.
//
// Scope: Tier drives HP/defense; Role drives BT assignment (pass 1). The flinch
// thresholds are evaluated at the damage choke point (pass 2), and the per-Shape
// incoming-damage multipliers likewise (pass 3). All of the latter default to
// no-ops (thresholds 0, multipliers 1.0), so an asset authored for pass 1 keeps
// its exact behaviour until someone tunes the new fields.

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
 * Material/body class. The per-shape incoming-damage modifiers (e.g. Plated
 * shrugging off body shots, Shrouded resisting until revealed) live in
 * ShapeBodyDamageMultiplier / ShapeVitalDamageMultiplier below and are read at the
 * damage choke point. Defaults are 1.0 — the mechanism is live, the tuning is not.
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
    // Flinch thresholds — evaluated at the damage choke point (pass 2). A hit
    // whose final APPLIED damage meets the threshold trips the reaction; 0 (the
    // default) never trips. Absolute applied-damage values, not a 0..1 fraction.
    // -------------------------------------------------------------------------

    /** Threshold for a body-shot flinch. 0 = never. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Flinch",
        meta = (ClampMin = "0.0"))
    float BodyFlinchThreshold = 0.f;

    /** Threshold for a vital-shot flinch. Only meaningful where AllowsVitalFlinch()
     *  is true (i.e. non-Boss tiers). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Flinch",
        meta = (ClampMin = "0.0"))
    float VitalFlinchThreshold = 0.f;

    // -------------------------------------------------------------------------
    // Shape damage modifiers — incoming-damage multipliers by hit kind.
    //
    // The Shape field above is the enemy's damage IDENTITY (Plated shrugs body
    // shots, Shrouded resists until revealed); these two multipliers are how that
    // identity reads at the damage choke point. Kept as fields on the asset, next
    // to the flinch thresholds and the base attributes, because that is where this
    // project already carries per-kind tuning — no new config asset, editor-tunable
    // on the DA_Enemy_* assets that already exist.
    //
    // BOTH DEFAULT 1.0 this pass: mechanism only, zero balance change on merge.
    // Real values come from in-editor measurement, never from reasoning here.
    // -------------------------------------------------------------------------

    /** Multiplier on the final (post-mitigation) BODY-hit damage this enemy takes. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Shape",
        meta = (ClampMin = "0.0"))
    float ShapeBodyDamageMultiplier = 1.f;

    /** Multiplier on the final (post-mitigation) VITAL-hit damage this enemy takes. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy|Shape",
        meta = (ClampMin = "0.0"))
    float ShapeVitalDamageMultiplier = 1.f;

    /** The shape multiplier for a hit of the given kind. 1.0 by default. */
    float GetShapeDamageMultiplier(bool bVital) const
    { return bVital ? ShapeVitalDamageMultiplier : ShapeBodyDamageMultiplier; }

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
