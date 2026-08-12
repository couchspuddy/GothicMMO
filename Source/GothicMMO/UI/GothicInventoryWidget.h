// GothicInventoryWidget.h
// Base class for the inventory screen widget.
// Blueprint child (WBP_Inventory) handles the visual layout.
// This class handles data flow: reading inventory, triggering equip/dismantle.
//
// Toggle with a key press (I or Tab). While open, shows cursor and pauses input.
//
// Blueprint child: WBP_Inventory
//   - Build the visual grid, equipment slots, detail panel, strain bar
//   - Call C++ functions for equip/unequip/dismantle
//   - Implement BIEs for refresh callbacks

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Items/GothicItemTypes.h"
#include "GothicInventoryWidget.generated.h"

class UGothicInventoryComponent;

/**
 * One rolled weapon perk, resolved to display-ready text for the inspect screen.
 * Built in MakeUIData from FGothicItemInstance::WeaponPerks by looking each tag
 * up in the weapon's own catalog. DisplayName falls back to the tag's leaf name
 * and Description is left empty when the perk cannot be resolved (unknown tag,
 * or no catalog authored yet) — the entry is never dropped, so a perk the copy
 * actually rolled always shows the player something.
 */
USTRUCT(BlueprintType)
struct FGothicPerkUIData
{
    GENERATED_BODY()

    /** The Perk.Weapon.<Bucket>.<Name> tag written onto the item instance. */
    UPROPERTY(BlueprintReadOnly, Category = "UI")
    FGameplayTag Tag;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    FText DisplayName;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    FText Description;
};

/** Simplified item data for UI display — avoids exposing raw pointers to Blueprint. */
USTRUCT(BlueprintType)
struct FGothicItemUIData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    FGuid InstanceID;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    FText DisplayName;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    FText Description;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    EGothicItemRarity Rarity = EGothicItemRarity::Salvage;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    EGothicEquipSlot EquipSlot = EGothicEquipSlot::Sidearm;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    int32 GearPower = 0;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    int32 StarCeiling = 1;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    int32 CurrentStars = 0;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    float PrimaryStatValue = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    EGothicPrimaryStat PrimaryStatType = EGothicPrimaryStat::Resolve;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    TArray<FGothicStatRoll> SecondaryStats;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    float StrainCost = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    bool bImbued = false;

    UPROPERTY(BlueprintReadOnly, Category = "UI")
    TObjectPtr<UTexture2D> Icon;

    /**
     * Rolled weapon perks, resolved to display text for the inspect screen.
     * Empty for non-weapons, mundane drops, and every weapon until
     * DA_WeaponPerkCatalog is authored (the roll writes no tags then). Populated
     * in MakeUIData — see FGothicPerkUIData for the fallback rules.
     */
    UPROPERTY(BlueprintReadOnly, Category = "UI")
    TArray<FGothicPerkUIData> Perks;
};

/**
 * The character's live totals after equipment, for the equip screen's stat
 * panel. Read from the ASC, so it already includes everything gear applied
 * through GE_EquipmentStats — the widget never re-adds item values itself.
 */
USTRUCT(BlueprintType)
struct FGothicCharacterStatSummary
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "UI") float Health = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float MaxHealth = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float AttackPower = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float Defense = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float MaxEther = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float MovementSpeed = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float EvasionChance = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float AbilityHaste = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float VitalPointRadius = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float SteadfastRate = 0.f;

    /** INERT — no consumer yet. Shown so gear reads honestly, not as a promise. */
    UPROPERTY(BlueprintReadOnly, Category = "UI") float HealingReceived = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float ReloadSpeed = 0.f;

    // -------------------------------------------------------------------------
    // Display strings — bind the stat panel's text blocks to THESE, not to the
    // floats above.
    //
    // The floats are the honest values and stay as-is for comparison maths, but
    // they are not fit to show: the panel was rendering "Health 244.055908 /
    // 244.055908" and "Ability Haste 57.081573" because a raw float straight
    // into a Text conversion carries every decimal it has. Formatting here
    // rather than in Blueprint keeps every stat's precision and unit in one
    // place, so a percentage cannot end up looking like a flat number on one
    // screen and a percentage on another.
    // -------------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, Category = "UI|Display") FText HealthText;
    UPROPERTY(BlueprintReadOnly, Category = "UI|Display") FText AttackPowerText;
    UPROPERTY(BlueprintReadOnly, Category = "UI|Display") FText DefenseText;
    UPROPERTY(BlueprintReadOnly, Category = "UI|Display") FText EtherText;
    UPROPERTY(BlueprintReadOnly, Category = "UI|Display") FText MovementSpeedText;
    UPROPERTY(BlueprintReadOnly, Category = "UI|Display") FText EvasionText;
    UPROPERTY(BlueprintReadOnly, Category = "UI|Display") FText AbilityHasteText;
    UPROPERTY(BlueprintReadOnly, Category = "UI|Display") FText SteadfastRateText;
};

/**
 * Difference a candidate item would make against whatever occupies its slot.
 * Every field is candidate-minus-equipped, so positive is an upgrade and the
 * widget can colour on sign without knowing anything about the stats.
 */
USTRUCT(BlueprintType)
struct FGothicEquipComparison
{
    GENERATED_BODY()

    /** False when the candidate ID resolves to nothing — treat the rest as meaningless. */
    UPROPERTY(BlueprintReadOnly, Category = "UI") bool bValid = false;

    /** True when the slot is currently empty, so every delta is a pure gain. */
    UPROPERTY(BlueprintReadOnly, Category = "UI") bool bSlotEmpty = true;

    UPROPERTY(BlueprintReadOnly, Category = "UI") EGothicEquipSlot Slot = EGothicEquipSlot::Sidearm;
    UPROPERTY(BlueprintReadOnly, Category = "UI") int32 GearPowerDelta = 0;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float PrimaryStatDelta = 0.f;
    UPROPERTY(BlueprintReadOnly, Category = "UI") float StrainDelta = 0.f;

    /**
     * Per-secondary delta. A stat on only one of the two items still appears,
     * with the missing side counted as zero — otherwise losing a stat entirely
     * would silently vanish from the comparison instead of reading as a loss.
     */
    UPROPERTY(BlueprintReadOnly, Category = "UI") TArray<FGothicStatRoll> SecondaryDeltas;
};

UCLASS(Abstract, Blueprintable)
class GOTHICMMO_API UGothicInventoryWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Call this after creating the widget to bind it to the inventory. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    void InitializeFromInventory(UGothicInventoryComponent* InInventory);

    // ── Data Queries — Blueprint reads these ─────────────────────────────

    /** Get all items currently in inventory (unequipped). */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    TArray<FGothicItemUIData> GetInventoryItems() const;

    /** Get the item equipped in a specific slot. Returns false if empty. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    bool GetEquippedItemInEquipSlot(EGothicEquipSlot EquipSlot, FGothicItemUIData& OutItem) const;

    /** Current Resonance Strain / Cap. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    float GetCurrentStrain() const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    float GetResonanceCap() const;

    /** Current Silver balance. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    int32 GetSilver() const;

    /** Live character totals after gear, for the stat panel. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    FGothicCharacterStatSummary GetCharacterStatSummary() const;

    /**
     * What equipping this item would change, against whatever is in its slot.
     * Drives the upgrade/downgrade arrows on the item entry.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    FGothicEquipComparison CompareToEquipped(const FGuid& InstanceID) const;

    /** Every slot in EGothicEquipSlot, in declaration order — lets the paper
     *  doll build itself rather than hardcoding thirteen entries in UMG. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    static TArray<EGothicEquipSlot> GetAllEquipSlots();

    /** Display label for a slot ("Left Arm"), so UMG needn't switch on the enum. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    static FText GetEquipSlotDisplayName(EGothicEquipSlot EquipSlot);

    // ── Actions — Blueprint calls these from button clicks ───────────────

    /** Equip an item by its instance ID. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool RequestEquip(const FGuid& InstanceID);

    /** Unequip a slot. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool RequestUnequip(EGothicEquipSlot EquipSlot);

    /** Dismantle an item for Silver/Selah. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool RequestDismantle(const FGuid& InstanceID);

    // ── Refresh Callbacks — Blueprint implements these ───────────────────

    /** Called when inventory contents change. Rebuild the item grid. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Inventory")
    void OnInventoryRefreshed();

    /** Called when equipment changes. Update the equipment slot display. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Inventory")
    void OnEquipmentRefreshed();

    /** Called when Resonance Strain changes. Update the strain bar. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Inventory")
    void OnStrainRefreshed();

protected:
    virtual void NativeDestruct() override;

    UPROPERTY()
    TObjectPtr<UGothicInventoryComponent> CachedInventory;

private:
    /** Converts a raw item instance to Blueprint-friendly UI data. */
    FGothicItemUIData MakeUIData(const FGothicItemInstance& Item) const;

    UFUNCTION()
    void HandleItemAdded(const FGothicItemInstance& Item);

    UFUNCTION()
    void HandleItemRemoved(const FGothicItemInstance& Item);

    UFUNCTION()
    void HandleEquipChanged(EGothicEquipSlot EquipSlot, const FGothicItemInstance& Item);

    UFUNCTION()
    void HandleStrainChanged();
};