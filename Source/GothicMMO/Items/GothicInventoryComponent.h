// GothicInventoryComponent.h
// Manages the player's item inventory, equipped gear, and Resonance Strain.
// Lives on PlayerState to survive respawns (same pattern as ASC/AttributeSet).
//
// The inventory is server-authoritative — all mutations require authority.
// Clients read the state via replication.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/GothicItemTypes.h"
#include "ActiveGameplayEffectHandle.h"
#include "GothicInventoryComponent.generated.h"

class UGothicItemDefinition;
class UGameplayEffect;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryChanged, const FGothicItemInstance&, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnItemEquipped, EGothicEquipSlot, Slot, const FGothicItemInstance&, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStrainChanged);

UCLASS(ClassGroup = (Gothic), meta = (BlueprintSpawnableComponent))
class GOTHICMMO_API UGothicInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGothicInventoryComponent();

    // ── Inventory ────────────────────────────────────────────────────────

    /** Add a rolled item to the inventory. Returns false if inventory is full. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool AddItem(const FGothicItemInstance& Item);

    /** Remove an item by its instance ID. Returns true if found and removed. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool RemoveItem(const FGuid& InstanceID);

    /** Find an item by its instance ID. Returns null if not found. */
    const FGothicItemInstance* FindItem(const FGuid& InstanceID) const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    int32 GetItemCount() const { return Items.Num(); }

    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    const TArray<FGothicItemInstance>& GetAllItems() const { return Items; }

    // ── Equipment ────────────────────────────────────────────────────────

    /**
     * Equip an item from inventory to its slot.
     * Unequips any item currently in that slot (returns it to inventory).
     * Checks Resonance Strain budget before equipping Resonant/Pure items.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool EquipItem(const FGuid& InstanceID);

    /** Unequip an item from a slot, returning it to inventory. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool UnequipSlot(EGothicEquipSlot Slot);

    /** Get the item currently equipped in a slot. Returns null if empty. */
    const FGothicItemInstance* GetEquippedItem(EGothicEquipSlot Slot) const;

    /** True if a slot has an item equipped. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    bool IsSlotEquipped(EGothicEquipSlot Slot) const;

    /**
     * Average Gear Power across equipped (non-empty) slots — the overall power
     * level. Raises the damage floor in GA_Fire and is the intended hook for
     * gating which activities the player can enter. 0 with nothing equipped.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    int32 GetAggregateGearPower() const;

    /**
     * Total of one archetype-damage secondary line summed across every equipped
     * item, as a percent. GA_Fire applies (1 + this/100) but only for the
     * archetype currently equipped, so a Revolver line does nothing while a
     * Rifle is out.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    float GetArchetypeDamageBonus(EGothicSecondaryStat DamageStat) const;

    /**
     * Item definitions the player begins with, rolled and auto-equipped into
     * their slots the first time the inventory initializes. This is the starting
     * kit — a basic piece in every armor slot so a tester walks in geared, then
     * swaps in drops to feel the difference. Server-authoritative and one-shot
     * (survives respawn without re-granting). Configure on the PlayerState BP.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Inventory")
    TArray<TObjectPtr<UGothicItemDefinition>> StartingItemDefs;

    /** Rolls, adds, and equips each StartingItemDef once, on authority. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    void GrantStartingItems();

    // ── Resonance Strain ─────────────────────────────────────────────────

    /** Fixed Resonance cap. Never rises. All pressure on the item side. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Resonance")
    float ResonanceCap = 100.f;

    /** Current total Strain from all equipped items. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    float GetCurrentStrain() const;

    /** How much Strain budget remains. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    float GetRemainingStrain() const { return ResonanceCap - GetCurrentStrain(); }

    /** True if equipping this item would exceed the Strain cap. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    bool WouldExceedStrain(const FGothicItemInstance& Item) const;

    // ── Economy ──────────────────────────────────────────────────────────

    /** Dismantle an item — returns Silver (mundane) or Selah (Resonant/Pure). */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool DismantleItem(const FGuid& InstanceID);

    /** Current Silver balance. Mundane currency, account-wide. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Economy")
    int32 Silver = 0;

    // ── Sean the Binder ──────────────────────────────────────────────────

    /**
     * Re-roll an item's secondary stats at Sean.
     * Costs Selah (from the player's Selah attribute).
     * Random, never directed — Selah buys attempts, not outcomes.
     * Returns true if the re-roll succeeded.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool RerollSecondaries(const FGuid& InstanceID, float SelahCost);

    /**
     * Imbue stars into an item at Sean.
     * Costs Selah. Cannot exceed the item's StarCeiling.
     * Locks the item (bImbued = true, prevents trading).
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool ImbueStar(const FGuid& InstanceID, float SelahCost);

    // ── Delegates ────────────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category = "Gothic|Inventory")
    FOnInventoryChanged OnItemAdded;

    UPROPERTY(BlueprintAssignable, Category = "Gothic|Inventory")
    FOnInventoryChanged OnItemRemoved;

    UPROPERTY(BlueprintAssignable, Category = "Gothic|Inventory")
    FOnItemEquipped OnItemEquipped;

    UPROPERTY(BlueprintAssignable, Category = "Gothic|Inventory")
    FOnStrainChanged OnStrainChanged;

    // ── Debug ────────────────────────────────────────────────────────────

    /** Spawns a batch of test items covering every rarity and slot. For testing only. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Debug")
    void DebugSpawnTestItems();

protected:
    /** Maximum inventory slots. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Inventory")
    int32 MaxInventorySize = 50;

    /** All items in the player's inventory (unequipped). */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Inventory")
    TArray<FGothicItemInstance> Items;

    /** Currently equipped items, keyed by slot. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Inventory")
    TMap<EGothicEquipSlot, FGothicItemInstance> EquippedItems;

    /** Guards GrantStartingItems so the kit is granted once, not every respawn. */
    bool bStartingItemsGranted = false;

    /** The GameplayEffect used to apply equipment stat bonuses. Assign GE_EquipmentStats in BP. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Inventory")
    TSubclassOf<UGameplayEffect> EquipmentStatsEffect;

private:
    /** Find a mutable item in the inventory by ID. */
    FGothicItemInstance* FindItemMutable(const FGuid& InstanceID);

    /** Recalculate and broadcast strain. */
    void RecalculateStrain();

    /** Active GE handles per equipped slot — removed on unequip. */
    TMap<EGothicEquipSlot, FActiveGameplayEffectHandle> ActiveStatEffects;

    /** Apply the item's stats as a GameplayEffect on the owning ASC. */
    void ApplyEquipmentStats(EGothicEquipSlot Slot, const FGothicItemInstance& Item);

    /**
     * SetByCaller tag a secondary stat writes through on GE_EquipmentStats.
     * Returns NAME_None for anything unmapped. Keep in step with the modifiers
     * authored on the effect — GAS ignores a SetByCaller with no matching
     * modifier silently, so a mismatch presents as "the stat rolled zero".
     */
    static FName SecondaryStatToSetByCallerTag(EGothicSecondaryStat StatType);

    /** Remove the stat effect for a slot. */
    void RemoveEquipmentStats(EGothicEquipSlot Slot);
};