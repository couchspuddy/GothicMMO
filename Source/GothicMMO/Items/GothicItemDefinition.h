// GothicItemDefinition.h
// Data asset defining an item type's base properties.
// Each distinct item in the game has one of these.
// Instances (FGothicItemInstance) are rolled copies with specific stats.
//
// Create in editor: Right-click → Miscellaneous → Data Asset → GothicItemDefinition
// Examples: DA_Item_IronSidearm, DA_Item_EmberCoat, DA_Item_PriorFlameFragment

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Items/GothicItemTypes.h"
#include "GothicItemDefinition.generated.h"

class UGothicWeaponData;

UCLASS(BlueprintType)
class GOTHICMMO_API UGothicItemDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // ── Identity ─────────────────────────────────────────────────────────

    /** Internal unique name. Used for lookups and save data. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Identity")
    FName ItemID;

    /** Display name shown in UI. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Identity")
    FText DisplayName;

    /** Lore description. For Pure items, this is the Accursed's story fragment. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Identity", meta = (MultiLine = true))
    FText Description;

    /** Icon for inventory/HUD display. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Identity")
    TObjectPtr<UTexture2D> Icon;

    // ── Classification ───────────────────────────────────────────────────

    /** Which equipment slot this item occupies. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Classification")
    EGothicEquipSlot EquipSlot = EGothicEquipSlot::Sidearm;

    /** Rarity tier — determines dismantle currency and Strain behavior. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Classification")
    EGothicItemRarity Rarity = EGothicItemRarity::Salvage;

    /**
     * Gear tier (1-5). Determines base Gear Power (baked, not rollable).
     * Higher tiers only drop from higher-tier enemies.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Classification", meta = (ClampMin = 1, ClampMax = 5))
    int32 GearTier = 1;

    // ── Stats ────────────────────────────────────────────────────────────

    /** Which primary stat this item rolls. Class-biased by design. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Stats")
    EGothicPrimaryStat PrimaryStatType = EGothicPrimaryStat::Resolve;

    /** Roll range for the primary stat. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Stats")
    FVector2D PrimaryStatRange = FVector2D(5.f, 15.f);

    /** How many secondary stats roll on this item. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Stats", meta = (ClampMin = 0, ClampMax = 4))
    int32 SecondaryStatSlots = 2;

    /** Pool of possible secondaries with their roll ranges. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Stats")
    TArray<FGothicSecondaryStatRange> SecondaryStatPool;

    // ── Resonance ────────────────────────────────────────────────────────

    /** Base Resonance Strain cost when equipped. Only relevant for Resonant/Pure. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Resonance")
    float BaseStrainCost = 0.f;

    /** Star ceiling range — rolled at drop. Higher difficulty = higher ceiling. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Resonance", meta = (ClampMin = 1, ClampMax = 5))
    int32 MinStarCeiling = 1;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Resonance", meta = (ClampMin = 1, ClampMax = 5))
    int32 MaxStarCeiling = 3;

    // ── Weapon Data ──────────────────────────────────────────────────────

    /**
     * If this item is a weapon, reference the weapon data asset that defines
     * its fire behavior (damage, range, magazine, crosshair).
     * Null for non-weapon items.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Weapon")
    TObjectPtr<UGothicWeaponData> WeaponData;

    // ── Economy ──────────────────────────────────────────────────────────

    /** Silver gained from dismantling (mundane rarity). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Economy")
    int32 DismantleSilver = 10;

    /** Selah returned from dismantling (Resonant/Pure rarity). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Economy")
    float DismantleSelah = 0.f;

    // ── Helpers ──────────────────────────────────────────────────────────

    /** True if this is a mundane item (Salvage/Kept/Remembered). */
    bool IsMundane() const
    {
        return Rarity == EGothicItemRarity::Salvage
            || Rarity == EGothicItemRarity::Kept
            || Rarity == EGothicItemRarity::Remembered;
    }

    /** True if this item carries Resonance Strain. */
    bool HasStrain() const
    {
        return Rarity == EGothicItemRarity::Resonant
            || Rarity == EGothicItemRarity::Pure;
    }

    /** True if this is a weapon (has weapon data assigned). */
    bool IsWeapon() const { return WeaponData != nullptr; }

    /** Gear Power for this tier. Baked formula — not rollable. */
    int32 GetGearPower() const { return GearTier * 100; }

    /**
     * This piece's contribution to Gear Score (ITEMIZATION_AND_LOOT.md, "Gear
     * Score — Definition"). Its tier, except that Salvage counts 0: Salvage has
     * no upgrade path and sits outside the tier ladder entirely, so the GearTier
     * field's default of 1 does not mean a Salvage piece is a Tier-1 piece. This
     * is what keeps a fresh character — whose whole starting kit is Salvage — at
     * Gear Score 0, and therefore on today's exact damage numbers.
     */
    int32 GetGearScoreTier() const
    {
        return Rarity == EGothicItemRarity::Salvage ? 0 : GearTier;
    }

    /**
     * Roll a fresh item instance from this definition.
     * Star ceiling rolled within [MinStarCeiling, MaxStarCeiling].
     * Primary and secondary stats rolled from configured ranges.
     */
    FGothicItemInstance RollInstance() const;
};