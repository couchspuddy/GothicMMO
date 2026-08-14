// GothicInventoryComponent.h
// Manages the player's item inventory, equipped gear, and Resonance Strain.
// Lives on PlayerState to survive respawns (same pattern as ASC/AttributeSet).
//
// Server-authoritative and replicated. Every mutation runs on the server only;
// clients observe the result through replication and never mutate locally.
//
// ── The shape of it ──────────────────────────────────────────────────────────
//
// Items, EquippedItems and Silver are Replicated, COND_OwnerOnly — a player's
// inventory is nobody else's business, and PlayerState replicates to every
// connection by default, so without the condition each client would receive
// every other player's full gear list. The PlayerState's Owner is its
// PlayerController (APlayerController::InitPlayerState), which is what makes
// both COND_OwnerOnly and the Server RPCs below route correctly from a
// component that does not live on the pawn.
//
// EquippedItems is a TArray<FGothicEquippedSlot>, not the TMap it used to be:
// UE cannot replicate a TMap. GetEquippedItem/IsSlotEquipped hide the change,
// so callers are unaffected.
//
// ── Who may call what ────────────────────────────────────────────────────────
//
// AddItem and RemoveItem are authority-only and refuse (with a log) on a
// client; nothing legitimately drives them from a client — pickups, loot and
// the starting kit are all server-side.
//
// EquipItem, UnequipSlot, DismantleItem, RerollSecondaries and ImbueStar ARE
// legitimately client-initiated: they are driven by the player from
// GothicInventoryWidget and Sean the Binder's UI. Each is a router — on
// authority it performs the change, on a client it forwards to its Server RPC
// and the change comes back as replicated state. Call sites do not change, but
// see the return-value note on each: on a client the bool means "request sent",
// not "request succeeded", because the answer is not known locally yet.
//
// ── UI refresh, and why it does not double-fire ──────────────────────────────
//
// The four delegates (OnItemAdded/OnItemRemoved/OnItemEquipped/OnStrainChanged)
// are broadcast directly by the mutation functions, which now only ever execute
// on the authority. Clients get the same broadcasts from OnRep_Items and
// OnRep_EquippedItems, which diff the newly-arrived arrays against a local
// shadow copy and emit exactly the events the server emitted.
//
// These two paths cannot both fire for the same change: UE never calls an OnRep
// on the authority, and a listen-server host IS the authority for every
// PlayerState in its world — including remote players'. So the host only ever
// takes the direct-broadcast path and a client only ever takes the OnRep path.
// The OnReps additionally early-out on ROLE_Authority as a belt-and-braces
// guard against a future caller invoking them by hand.

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

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** True when this component may mutate state. Ownerless (CDO/tests) counts as authority. */
    bool HasInventoryAuthority() const;

    // ── Inventory ────────────────────────────────────────────────────────

    /**
     * Add a rolled item to the inventory. Returns false if inventory is full.
     * Authority-only — refuses and logs on a client. No client path exists
     * because nothing client-side legitimately grants items.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool AddItem(const FGothicItemInstance& Item);

    /**
     * Grant a fresh item rolled from a definition — the production vendor seam
     * (Hearth weapon vendor). Client-callable: on authority it grants directly,
     * on a client it forwards to ServerGrantItem, exactly like EquipItem →
     * ServerEquipItem. The roll MUST happen server-side because
     * UGothicItemDefinition::RollInstance is plain C++, not replicated — a client
     * roll would never reach the authoritative inventory.
     *
     * Unlike AddItem, this IS a legitimate client entry point: it is what the
     * vendor UI's "buy" button calls. FREE for now — no cost is deducted; the
     * currency check/deduction slots in server-side (see
     * ServerGrantItem_Implementation, next to the Silver read).
     *
     * bAutoEquip: equip the rolled instance into its slot and, for weapons, swap
     * it into the active hand — mirrors the dev bench's GrantBenchItem.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    void RequestGrantItem(UGothicItemDefinition* Definition, bool bAutoEquip = true);

    /** Remove an item by its instance ID. Authority-only. Returns true if found and removed. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool RemoveItem(const FGuid& InstanceID);

    /**
     * Find an item by its instance ID, searching the inventory AND equipped gear.
     *
     * Equipped gear used to be invisible here, which is why the Binder could
     * never re-roll, imbue or dismantle the armor a player was actually wearing.
     * If you specifically need "in the backpack, not on the body" — EquipItem
     * does — use FindInventoryItem.
     */
    const FGothicItemInstance* FindItem(const FGuid& InstanceID) const;

    /** Find an item in the unequipped inventory only. Returns null if not found or if equipped. */
    const FGothicItemInstance* FindInventoryItem(const FGuid& InstanceID) const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    int32 GetItemCount() const { return Items.Num(); }

    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    const TArray<FGothicItemInstance>& GetAllItems() const { return Items; }

    // ── Equipment ────────────────────────────────────────────────────────

    /**
     * Equip an item from inventory to its slot.
     * Unequips any item currently in that slot (returns it to inventory).
     * Checks Resonance Strain budget before equipping Resonant/Pure items.
     *
     * Client-callable: on a client this forwards to ServerEquipItem and returns
     * true meaning "request sent" — the real outcome arrives via replication and
     * shows up as OnItemEquipped. Only the authority's return value is a verdict.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool EquipItem(const FGuid& InstanceID);

    /** Unequip an item from a slot, returning it to inventory. Client-callable; see EquipItem on the return value. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool UnequipSlot(EGothicEquipSlot Slot);

    /** Get the item currently equipped in a slot. Returns null if empty. */
    const FGothicItemInstance* GetEquippedItem(EGothicEquipSlot Slot) const;

    /** True if a slot has an item equipped. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    bool IsSlotEquipped(EGothicEquipSlot Slot) const;

    /**
     * Every occupied slot, for callers that need the whole equipment set rather
     * than one slot — the travel snapshot is the one today. Equipped items are
     * NOT in Items, so this and GetAllItems together are the complete holdings.
     */
    const TArray<FGothicEquippedSlot>& GetEquippedSlots() const { return EquippedItems; }

    /**
     * Average Gear Power across equipped (non-empty) slots — the overall power
     * level, and the intended hook for gating which activities the player can
     * enter. 0 with nothing equipped. No longer feeds damage: as of 2026-08-04
     * weapon damage reads the weapon's own tier and armor tier reads Gear Score,
     * because an average let ten armor slots vote on one weapon and diluted every
     * upgrade to a tenth of its face value.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    int32 GetAggregateGearPower() const;

    /**
     * Gear Score — the SUM of gear tier across the ten equipped ARMOR slots,
     * 0 to 50 (ITEMIZATION_AND_LOOT.md). Weapons are excluded and Salvage counts
     * 0. Attack Power and Defense are derived from it linearly, so one tier point
     * on one piece is a visible, guaranteed +1 rather than a tenth of one.
     *
     * Sum, not average, deliberately — see the doc; the average is exactly the
     * trap this replaced.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    int32 GetGearScore() const;

    /**
     * Attack Power and Defense granted per point of Gear Score, on top of the
     * base 15/8 from GE_InitStats_Player — so AP 15→65 and Def 8→33 across the
     * 0–50 range. Defense grows at half the rate so damage taken shrinks slower
     * than damage dealt grows; ITEMIZATION_AND_LOOT.md flags that rate as an
     * open redline knob, and this is the line to move when it is redlined.
     */
    static constexpr float AttackPowerPerGearTier = 1.0f;
    static constexpr float DefensePerGearTier     = 0.5f;

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

    /**
     * Rolls, adds, and equips each StartingItemDef once, on authority.
     *
     * bCanonicalRolls is the measurement-bench path (L_DEV_FeelBox): each piece
     * is rolled through UGothicItemDefinition::RollInstance(true), which freezes
     * every stat to its range midpoint so the kit — and therefore the archetype
     * damage scalar GA_Fire reads — is byte-identical every session and every
     * spawn. Default false leaves every shipping map on the existing rolled path,
     * untouched. The caller (the pawn) decides, from its DevBenchMaps gate.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    void GrantStartingItems(bool bCanonicalRolls = false);

    /**
     * Refill this (freshly-created) inventory from a UGothicGameInstance travel
     * snapshot. Authority-only; returns false if it refuses.
     *
     * Restores through the real paths — AddItem for every carried and equipped
     * instance, then EquipItem_Authority for the ones that were equipped — so
     * GE_EquipmentStats, gear score, Attack Power/Defense, the character's weapon
     * slots and the HUD all rebuild exactly as they do when a player equips by
     * hand. Nothing derived is written directly.
     *
     * It also raises bStartingItemsGranted, which is what suppresses the starting
     * kit: a travelling player already owns their gear, and re-granting it would
     * both duplicate the kit and overwrite what they were wearing. That means the
     * restore must run BEFORE GrantStartingItems at the init seam.
     */
    bool RestoreFromTravelSnapshot(
        const TArray<FGothicItemInstance>& InItems,
        const TArray<FGothicEquippedSlot>& InEquipped);

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

    /**
     * True if equipping this item would exceed the Strain cap.
     *
     * UNREACHABLE TODAY: this can never return true with current content. The
     * highest StrainCost authored anywhere is the Pure-tier Ashen Crown at 40,
     * and the most strain-bearing items that can be worn at once total 50
     * across all slots, against a ResonanceCap of 100. The gate is real code on
     * a budget nothing can currently spend. Raising strain costs (or lowering
     * the cap) is a deliberate balance decision and explicitly deferred — do not
     * "fix" this by changing the cap.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    bool WouldExceedStrain(const FGothicItemInstance& Item) const;

    // ── Economy ──────────────────────────────────────────────────────────

    /**
     * Dismantle an item — returns Silver (mundane) or Selah (Resonant/Pure).
     * Works on equipped gear as well as inventory; equipped items are unequipped
     * first so their stat effect is torn down. Client-callable.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool DismantleItem(const FGuid& InstanceID);

    /**
     * Current Silver balance. Mundane currency, account-wide.
     *
     * STUB — accumulates and is never spent. Dismantling mundane gear pays into
     * it and nothing anywhere reads it: there is no Silver sink (no vendor, no
     * repair, no crafting cost). It replicates so the number a client sees is
     * the server's, but until a sink exists it is a scoreboard, not an economy.
     * Building that sink is deferred, not forgotten.
     */
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Gothic|Economy")
    int32 Silver = 0;

    // ── Sean the Binder ──────────────────────────────────────────────────

    /**
     * Re-roll an item's secondary stats at Sean.
     * Costs Selah (from the player's Selah attribute).
     * Random, never directed — Selah buys attempts, not outcomes.
     * Works on equipped gear as well as inventory. Client-callable.
     * Returns true if the re-roll succeeded (on the authority).
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool RerollSecondaries(const FGuid& InstanceID, float SelahCost);

    /**
     * Imbue stars into an item at Sean.
     * Costs Selah. Cannot exceed the item's StarCeiling.
     * Locks the item (bImbued = true, prevents trading).
     * Works on equipped gear as well as inventory. Client-callable.
     *
     * STUB — CurrentStars is recorded and spent Selah is really deducted, but
     * stars have no mechanical effect: nothing reads CurrentStars when computing
     * stats, damage or Gear Power. Imbuing today buys a number on a tooltip and
     * a trade lock. Wiring stars to a stat multiplier is deferred by design.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    bool ImbueStar(const FGuid& InstanceID, float SelahCost);

    // ── Server RPCs ──────────────────────────────────────────────────────
    //
    // The client-initiated mutations. Each is Reliable + WithValidation and
    // simply re-enters the matching public function on the authority, so the
    // rules (strain budget, Selah cost, inventory cap, slot agreement) are
    // enforced in exactly one place and a client cannot bypass them by calling
    // the RPC directly.

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerGrantItem(UGothicItemDefinition* Definition, bool bAutoEquip);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerEquipItem(const FGuid& InstanceID);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerUnequipSlot(EGothicEquipSlot Slot);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerDismantleItem(const FGuid& InstanceID);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRerollSecondaries(const FGuid& InstanceID, float SelahCost);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerImbueStar(const FGuid& InstanceID, float SelahCost);

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
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Items, Category = "Gothic|Inventory")
    TArray<FGothicItemInstance> Items;

    /**
     * Currently equipped items, one entry per occupied slot.
     *
     * A TArray and not a TMap because UE cannot replicate a TMap. Order is not
     * meaningful; look up by Slot (GetEquippedItem / FindEquippedSlot).
     */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EquippedItems, Category = "Gothic|Inventory")
    TArray<FGothicEquippedSlot> EquippedItems;

    /** Client-side: rebuild the UI events the server broadcast directly. Never runs on the authority. */
    UFUNCTION()
    void OnRep_Items(const TArray<FGothicItemInstance>& PreviousItems);

    UFUNCTION()
    void OnRep_EquippedItems(const TArray<FGothicEquippedSlot>& PreviousEquipped);

    /** Guards GrantStartingItems so the kit is granted once, not every respawn. */
    bool bStartingItemsGranted = false;

    /** The GameplayEffect used to apply equipment stat bonuses. Assign GE_EquipmentStats in BP. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Inventory")
    TSubclassOf<UGameplayEffect> EquipmentStatsEffect;

private:
    /**
     * Find a mutable item by ID, searching the inventory AND equipped gear.
     *
     * DANGER: the returned pointer points into Items or EquippedItems and is
     * invalidated by ANY mutation of either array — including calls that mutate
     * indirectly, like RemoveItem, EquipItem and UnequipSlot. A use-after-free
     * from exactly this pattern was fixed in EquipItem; copy the value out
     * before you touch a container, and never hold this across a call.
     */
    FGothicItemInstance* FindItemMutable(const FGuid& InstanceID);

    /** The slot an equipped item occupies, or none if the ID is not equipped. */
    TOptional<EGothicEquipSlot> FindEquippedSlotForItem(const FGuid& InstanceID) const;

    /** Index into EquippedItems for a slot, or INDEX_NONE. */
    int32 IndexOfEquippedSlot(EGothicEquipSlot Slot) const;

    // ── Authority implementations ────────────────────────────────────────
    // The public functions are routers (authority → these; client → Server RPC).
    // These assume authority and are where the actual rules live.

    void GrantItem_Authority(UGothicItemDefinition* Definition, bool bAutoEquip);
    bool EquipItem_Authority(const FGuid& InstanceID);
    bool UnequipSlot_Authority(EGothicEquipSlot Slot);
    bool DismantleItem_Authority(const FGuid& InstanceID);
    bool RerollSecondaries_Authority(const FGuid& InstanceID, float SelahCost);
    bool ImbueStar_Authority(const FGuid& InstanceID, float SelahCost);

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