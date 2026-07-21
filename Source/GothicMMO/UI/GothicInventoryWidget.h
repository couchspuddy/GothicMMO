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
#include "Items/GothicItemTypes.h"
#include "GothicInventoryWidget.generated.h"

class UGothicInventoryComponent;

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
    EGothicEquipSlot EquipSlot = EGothicEquipSlot::PrimaryWeapon;

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