// GothicInventoryComponent.cpp

#include "Items/GothicInventoryComponent.h"
#include "Items/GothicItemDefinition.h"
#include "Character/GothicPlayerCharacter.h" // vendor grant reaches the pawn's weapon swap
#include "GameFramework/PlayerState.h"
#include "GothicMMO.h"                        // LogVigilCombat
#include "Weapons/GothicWeaponData.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

UGothicInventoryComponent::UGothicInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UGothicInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // COND_OwnerOnly throughout. This component lives on the PlayerState, which
    // replicates to EVERY connection — without the condition, each client would
    // receive every other player's complete inventory, which is both a waste of
    // bandwidth and free intel for a cheat client. The PlayerState's Owner is
    // its PlayerController, so "owner only" resolves to the right connection.
    DOREPLIFETIME_CONDITION(UGothicInventoryComponent, Items, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UGothicInventoryComponent, EquippedItems, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UGothicInventoryComponent, Silver, COND_OwnerOnly);
}

bool UGothicInventoryComponent::HasInventoryAuthority() const
{
    // No owner means no net context at all — the CDO, or a component built in a
    // test harness. Treat that as authority so those paths keep working rather
    // than silently refusing every mutation.
    const AActor* Owner = GetOwner();
    return Owner == nullptr || Owner->HasAuthority();
}

// ═══════════════════════════════════════════════════════════════════════════
// Inventory
// ═══════════════════════════════════════════════════════════════════════════

bool UGothicInventoryComponent::AddItem(const FGothicItemInstance& Item)
{
    if (!HasInventoryAuthority())
    {
        // Deliberately no Server RPC: nothing on a client legitimately grants
        // items. Pickups, loot rolls and the starting kit all originate on the
        // server, so a client reaching here is a bug in the caller.
        UE_LOG(LogTemp, Warning,
            TEXT("Inventory: AddItem called on a client — ignored. Item grants are server-side."));
        return false;
    }

    if (!Item.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("Inventory: Tried to add invalid item"));
        return false;
    }

    if (Items.Num() >= MaxInventorySize)
    {
        UE_LOG(LogTemp, Warning, TEXT("Inventory: Full (%d/%d) — cannot add %s"),
            Items.Num(), MaxInventorySize,
            Item.Definition ? *Item.Definition->ItemID.ToString() : TEXT("Unknown"));
        return false;
    }

    Items.Add(Item);
    OnItemAdded.Broadcast(Item);

    // Picked-up items accumulate in the inventory and are equipped deliberately
    // from the equip screen. The previous auto-equip-into-empty-slot behavior
    // immediately moved each new-slot item out of Items (EquipItem removes it),
    // so the inventory never appeared to fill — and it could swap the active
    // weapon mid-fight. Equipping is now always an explicit player choice.

    return true;
}

bool UGothicInventoryComponent::RemoveItem(const FGuid& InstanceID)
{
    if (!HasInventoryAuthority())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Inventory: RemoveItem called on a client — ignored. Item removal is server-side."));
        return false;
    }

    for (int32 i = 0; i < Items.Num(); ++i)
    {
        if (Items[i].InstanceID == InstanceID)
        {
            FGothicItemInstance Removed = Items[i];
            Items.RemoveAt(i);
            OnItemRemoved.Broadcast(Removed);
            return true;
        }
    }
    return false;
}

const FGothicItemInstance* UGothicInventoryComponent::FindInventoryItem(const FGuid& InstanceID) const
{
    for (const FGothicItemInstance& Item : Items)
    {
        if (Item.InstanceID == InstanceID)
        {
            return &Item;
        }
    }
    return nullptr;
}

const FGothicItemInstance* UGothicInventoryComponent::FindItem(const FGuid& InstanceID) const
{
    if (const FGothicItemInstance* Found = FindInventoryItem(InstanceID))
    {
        return Found;
    }

    // Equipped gear is findable too. It was not, which is the whole reason the
    // Binder silently refused to work on anything the player was wearing.
    for (const FGothicEquippedSlot& Entry : EquippedItems)
    {
        if (Entry.Item.InstanceID == InstanceID)
        {
            return &Entry.Item;
        }
    }
    return nullptr;
}

FGothicItemInstance* UGothicInventoryComponent::FindItemMutable(const FGuid& InstanceID)
{
    for (FGothicItemInstance& Item : Items)
    {
        if (Item.InstanceID == InstanceID)
        {
            return &Item;
        }
    }

    for (FGothicEquippedSlot& Entry : EquippedItems)
    {
        if (Entry.Item.InstanceID == InstanceID)
        {
            return &Entry.Item;
        }
    }
    return nullptr;
}

TOptional<EGothicEquipSlot> UGothicInventoryComponent::FindEquippedSlotForItem(const FGuid& InstanceID) const
{
    for (const FGothicEquippedSlot& Entry : EquippedItems)
    {
        if (Entry.Item.InstanceID == InstanceID)
        {
            return Entry.Slot;
        }
    }
    return TOptional<EGothicEquipSlot>();
}

int32 UGothicInventoryComponent::IndexOfEquippedSlot(EGothicEquipSlot Slot) const
{
    for (int32 i = 0; i < EquippedItems.Num(); ++i)
    {
        if (EquippedItems[i].Slot == Slot)
        {
            return i;
        }
    }
    return INDEX_NONE;
}

// ═══════════════════════════════════════════════════════════════════════════
// Equipment
// ═══════════════════════════════════════════════════════════════════════════

bool UGothicInventoryComponent::EquipItem(const FGuid& InstanceID)
{
    if (!HasInventoryAuthority())
    {
        ServerEquipItem(InstanceID);
        return true; // "request sent" — see the header note on return values.
    }
    return EquipItem_Authority(InstanceID);
}

// ═══════════════════════════════════════════════════════════════════════════
// Vendor grant seam (Hearth weapon vendor)
//
// Same router → Server RPC → *_Authority shape as EquipItem: the client entry
// point forwards to the server, and the roll + add + equip happen only on the
// authority. The roll cannot be client-side — RollInstance is plain, unreplicated
// C++, so a client roll would produce an instance the server never sees.
// ═══════════════════════════════════════════════════════════════════════════

void UGothicInventoryComponent::RequestGrantItem(UGothicItemDefinition* Definition, bool bAutoEquip)
{
    if (!HasInventoryAuthority())
    {
        ServerGrantItem(Definition, bAutoEquip);
        return;
    }
    GrantItem_Authority(Definition, bAutoEquip);
}

bool UGothicInventoryComponent::ServerGrantItem_Validate(UGothicItemDefinition* Definition, bool bAutoEquip)
{
    // Reject a null definition outright — the one thing a malicious/buggy client
    // could send that the authority path can't roll. Everything else (inventory
    // full, misauthored slot) is handled and logged downstream by AddItem/EquipItem.
    return Definition != nullptr;
}

void UGothicInventoryComponent::ServerGrantItem_Implementation(UGothicItemDefinition* Definition, bool bAutoEquip)
{
    GrantItem_Authority(Definition, bAutoEquip);
}

void UGothicInventoryComponent::GrantItem_Authority(UGothicItemDefinition* Definition, bool bAutoEquip)
{
    if (!Definition)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Vendor|Grant|NULL-DEF — RequestGrantItem called with no definition; ignored."));
        return;
    }

    // ── Cost hook ────────────────────────────────────────────────────────
    // FREE for now: no currency is deducted. When Silver becomes spendable this
    // is the single point where the vendor price check + deduction slots in —
    // read the price off the definition (or a vendor table), compare against
    // Silver, bail with a log if short, and deduct after a successful AddItem.
    // Left explicitly FREE per the current plan (free until currency exists).
    // e.g.:  if (Silver < Price) { log "insufficient"; return; }  ... Silver -= Price;

    // Canonical roll: free vendor stock must be deterministic. A non-canonical
    // roll would let a player re-buy the same item until the secondaries came up
    // favourably (reroll-farming); RollInstance(true) freezes every stat to its
    // range midpoint so every copy of a given vendor line is identical.
    const FGothicItemInstance Instance = Definition->RollInstance(/*bCanonical=*/true);
    if (!AddItem(Instance))
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Vendor|Grant|ADD-FAILED|def=%s — inventory full."),
            *Definition->ItemID.ToString());
        return;
    }

    bool bEquipped = false;
    bool bSwappedToHand = false;
    if (bAutoEquip)
    {
        bEquipped = EquipItem(Instance.InstanceID);

        // For a weapon, force it into the active hand — mirrors GrantBenchItem.
        // The equip above already armed the weapon slot via OnItemEquipped →
        // OnEquipmentChanged (on authority and, through OnRep_EquippedItems, on
        // the owning client); this only makes it the ACTIVE weapon. Reaching the
        // pawn from a component that lives on the PlayerState: Owner is the
        // PlayerState, whose Pawn is the character.
        if (bEquipped && Definition->IsWeapon())
        {
            APlayerState* OwningPS = Cast<APlayerState>(GetOwner());
            AGothicPlayerCharacter* PlayerChar =
                OwningPS ? Cast<AGothicPlayerCharacter>(OwningPS->GetPawn()) : nullptr;
            if (PlayerChar)
            {
                bSwappedToHand = PlayerChar->SwapToWeaponForEquipSlot(Definition->EquipSlot);
            }
        }
    }

    UE_LOG(LogVigilCombat, Log,
        TEXT("Vendor|Grant|OK|owner=%s|def=%s|instance=%s|slot=%d|weapon=%d|equipped=%d|inHand=%d"),
        *GetNameSafe(GetOwner()),
        *Definition->ItemID.ToString(),
        *Instance.InstanceID.ToString(),
        static_cast<int32>(Definition->EquipSlot),
        Definition->IsWeapon() ? 1 : 0,
        bEquipped ? 1 : 0,
        bSwappedToHand ? 1 : 0);
}

bool UGothicInventoryComponent::ServerEquipItem_Validate(const FGuid& InstanceID)
{
    return true;
}

void UGothicInventoryComponent::ServerEquipItem_Implementation(const FGuid& InstanceID)
{
    EquipItem_Authority(InstanceID);
}

bool UGothicInventoryComponent::EquipItem_Authority(const FGuid& InstanceID)
{
    // Deliberately FindInventoryItem, not FindItem. FindItem now sees equipped
    // gear as well, and equipping something already equipped would take the
    // swap path against itself — RemoveItem finds nothing, UnequipSlot pushes
    // the item back into Items, and the slot is then overwritten with a stale
    // copy, duplicating the item. Refuse it outright instead.
    const FGothicItemInstance* Item = FindInventoryItem(InstanceID);
    if (!Item || !Item->Definition)
    {
        if (FindEquippedSlotForItem(InstanceID).IsSet())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Inventory: EquipItem — %s is already equipped."), *InstanceID.ToString());
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Inventory: EquipItem — item %s not found in inventory."), *InstanceID.ToString());
        }
        return false;
    }

    const EGothicEquipSlot Slot = Item->Definition->EquipSlot;

    // A weapon's archetype declares the slot it belongs in. If the item definition
    // disagrees, the asset is misauthored — refuse rather than silently loading a
    // Rig into the Sidearm slot, where it would inherit the wrong Steadfast tier.
    if (Item->Definition->IsWeapon()
        && Item->Definition->WeaponData->IntendedSlot != Slot)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Inventory: Cannot equip %s — weapon '%s' declares slot %d but the item definition says slot %d. Fix IntendedSlot on the weapon data or EquipSlot on the item definition."),
            *Item->Definition->ItemID.ToString(),
            *Item->Definition->WeaponData->WeaponName.ToString(),
            (int32)Item->Definition->WeaponData->IntendedSlot,
            (int32)Slot);
        return false;
    }

    // Check Resonance Strain budget for Resonant/Pure items
    if (Item->Definition->HasStrain() && WouldExceedStrain(*Item))
    {
        UE_LOG(LogTemp, Warning, TEXT("Inventory: Cannot equip %s — would exceed Resonance cap (%.1f + %.1f > %.1f)"),
            *Item->Definition->ItemID.ToString(),
            GetCurrentStrain(), Item->StrainCost, ResonanceCap);
        return false;
    }

    // Copy BEFORE touching Items. `Item` points INTO the Items array, and the
    // unequip below does Items.Add() — a reallocation there left the old code
    // reading freed memory on the very next line. Copy first, then never
    // dereference the pointer again.
    const FGothicItemInstance ItemCopy = *Item;
    Item = nullptr;

    // Remove before unequipping, not after. UnequipSlot now refuses at
    // MaxInventorySize, and a swap at a full inventory is net-zero — taking this
    // item out first guarantees the outgoing one has somewhere to land.
    RemoveItem(InstanceID);

    // Unequip whatever is in that slot; it returns to inventory.
    if (IndexOfEquippedSlot(Slot) != INDEX_NONE)
    {
        UnequipSlot_Authority(Slot);
    }

    FGothicEquippedSlot NewEntry;
    NewEntry.Slot = Slot;
    NewEntry.Item = ItemCopy;
    EquippedItems.Add(NewEntry);

    ApplyEquipmentStats(Slot, ItemCopy);

    RecalculateStrain();
    OnItemEquipped.Broadcast(Slot, ItemCopy);


    return true;
}

bool UGothicInventoryComponent::UnequipSlot(EGothicEquipSlot Slot)
{
    if (!HasInventoryAuthority())
    {
        ServerUnequipSlot(Slot);
        return true; // "request sent" — see the header note on return values.
    }
    return UnequipSlot_Authority(Slot);
}

bool UGothicInventoryComponent::ServerUnequipSlot_Validate(EGothicEquipSlot Slot)
{
    return true;
}

void UGothicInventoryComponent::ServerUnequipSlot_Implementation(EGothicEquipSlot Slot)
{
    UnequipSlot_Authority(Slot);
}

bool UGothicInventoryComponent::UnequipSlot_Authority(EGothicEquipSlot Slot)
{
    const int32 EquippedIdx = IndexOfEquippedSlot(Slot);
    if (EquippedIdx == INDEX_NONE)
    {
        return false;
    }

    // Copy by value — the array is mutated below and any reference into it dies.
    FGothicItemInstance Unequipped = EquippedItems[EquippedIdx].Item;

    // The cap is checked BEFORE anything is torn down. AddItem refuses at
    // MaxInventorySize; this path used to Items.Add() unconditionally, so
    // unequipping into a full inventory silently pushed it over the cap. Failing
    // here leaves the item equipped, which is the recoverable state — dropping it
    // on the floor would be a design decision, not a repair.
    if (Items.Num() >= MaxInventorySize)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Inventory: Cannot unequip slot %d — inventory full (%d/%d). Item stays equipped."),
            (int32)Slot, Items.Num(), MaxInventorySize);
        return false;
    }

    RemoveEquipmentStats(Slot);
    EquippedItems.RemoveAt(EquippedIdx);

    // Return to inventory
    Items.Add(Unequipped);
    RecalculateStrain();

    // Mirror what the equip path broadcasts. Only OnStrainChanged (from
    // RecalculateStrain) used to fire, so GothicInventoryWidget refreshed its
    // strain bar while the item list and equipment panel went stale — and
    // AGothicPlayerCharacter::OnEquipmentChanged, which is bound to
    // OnItemEquipped, never ran at all: the weapon slot, mesh, crosshair, ammo
    // and RefreshMovementSpeed() all kept the unequipped weapon's state.
    OnItemAdded.Broadcast(Unequipped);

    // An empty instance is the "slot is now bare" signal — OnEquipmentChanged
    // reads Item.Definition and clears the weapon slot when it is null.
    OnItemEquipped.Broadcast(Slot, FGothicItemInstance());

    return true;
}

const FGothicItemInstance* UGothicInventoryComponent::GetEquippedItem(EGothicEquipSlot Slot) const
{
    const int32 Idx = IndexOfEquippedSlot(Slot);
    return Idx == INDEX_NONE ? nullptr : &EquippedItems[Idx].Item;
}

bool UGothicInventoryComponent::IsSlotEquipped(EGothicEquipSlot Slot) const
{
    return IndexOfEquippedSlot(Slot) != INDEX_NONE;
}

// ═══════════════════════════════════════════════════════════════════════════
// Replication callbacks
//
// These reconstruct, on a client, the delegate broadcasts the server made
// directly from its mutation functions. They diff the arriving array against
// the pre-replication value UE hands us, so a client emits the same events in
// the same shapes — including UnequipSlot's empty-instance "slot is bare"
// signal, which AGothicPlayerCharacter::OnEquipmentChanged depends on.
//
// They cannot double-fire alongside the direct broadcasts: UE never calls an
// OnRep on the authority, and a listen-server host is the authority for every
// PlayerState it owns — including remote players'. The ROLE_Authority early-out
// is a guard against hand-invocation, not a case that occurs in normal flow.
// ═══════════════════════════════════════════════════════════════════════════

void UGothicInventoryComponent::OnRep_Items(const TArray<FGothicItemInstance>& PreviousItems)
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        return;
    }

    auto ContainsID = [](const TArray<FGothicItemInstance>& Array, const FGuid& ID)
    {
        return Array.ContainsByPredicate(
            [&ID](const FGothicItemInstance& Candidate) { return Candidate.InstanceID == ID; });
    };

    // Gone since the last update → OnItemRemoved, matching RemoveItem.
    for (const FGothicItemInstance& Old : PreviousItems)
    {
        if (!ContainsID(Items, Old.InstanceID))
        {
            OnItemRemoved.Broadcast(Old);
        }
    }

    // New since the last update → OnItemAdded, matching AddItem and the
    // return-to-inventory half of UnequipSlot.
    for (const FGothicItemInstance& New : Items)
    {
        if (!ContainsID(PreviousItems, New.InstanceID))
        {
            OnItemAdded.Broadcast(New);
        }
    }
}

void UGothicInventoryComponent::OnRep_EquippedItems(const TArray<FGothicEquippedSlot>& PreviousEquipped)
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        return;
    }

    // Slots that emptied. The server signals this by broadcasting OnItemEquipped
    // with a default-constructed instance (null Definition); reproduce that
    // exactly — OnEquipmentChanged reads Item.Definition and clears the weapon
    // slot, mesh, crosshair and ammo when it is null.
    for (const FGothicEquippedSlot& Old : PreviousEquipped)
    {
        const bool bStillOccupied = EquippedItems.ContainsByPredicate(
            [&Old](const FGothicEquippedSlot& Entry)
            {
                return Entry.Slot == Old.Slot && Entry.Item.InstanceID == Old.Item.InstanceID;
            });

        if (!bStillOccupied)
        {
            const bool bSlotRefilled = EquippedItems.ContainsByPredicate(
                [&Old](const FGothicEquippedSlot& Entry) { return Entry.Slot == Old.Slot; });

            // Only announce "bare" for a slot that is genuinely empty now. A
            // straight swap replaces the occupant in one replication step, and
            // firing empty-then-filled there would make the character briefly
            // tear down a weapon it is about to rebuild.
            if (!bSlotRefilled)
            {
                OnItemEquipped.Broadcast(Old.Slot, FGothicItemInstance());
            }
        }
    }

    // Slots that gained (or changed) an occupant → OnItemEquipped with the item.
    for (const FGothicEquippedSlot& New : EquippedItems)
    {
        const bool bUnchanged = PreviousEquipped.ContainsByPredicate(
            [&New](const FGothicEquippedSlot& Entry)
            {
                return Entry.Slot == New.Slot && Entry.Item.InstanceID == New.Item.InstanceID;
            });

        if (!bUnchanged)
        {
            OnItemEquipped.Broadcast(New.Slot, New.Item);
        }
    }

    // Strain is derived entirely from EquippedItems, so any change to the set
    // changes it. The server's equivalent call is RecalculateStrain().
    OnStrainChanged.Broadcast();
}

// ═══════════════════════════════════════════════════════════════════════════
// Resonance Strain
// ═══════════════════════════════════════════════════════════════════════════

float UGothicInventoryComponent::GetCurrentStrain() const
{
    float Total = 0.f;
    for (const FGothicEquippedSlot& Entry : EquippedItems)
    {
        Total += Entry.Item.StrainCost;
    }
    return Total;
}

bool UGothicInventoryComponent::WouldExceedStrain(const FGothicItemInstance& Item) const
{
    // Cannot return true with today's content — see the header. Max achievable
    // strain across every wearable slot is 50 against a ResonanceCap of 100.
    return (GetCurrentStrain() + Item.StrainCost) > ResonanceCap;
}

void UGothicInventoryComponent::RecalculateStrain()
{
    OnStrainChanged.Broadcast();
}

// ═══════════════════════════════════════════════════════════════════════════
// Economy — Dismantling
// ═══════════════════════════════════════════════════════════════════════════

bool UGothicInventoryComponent::DismantleItem(const FGuid& InstanceID)
{
    if (!HasInventoryAuthority())
    {
        ServerDismantleItem(InstanceID);
        return true; // "request sent" — see the header note on return values.
    }
    return DismantleItem_Authority(InstanceID);
}

bool UGothicInventoryComponent::ServerDismantleItem_Validate(const FGuid& InstanceID)
{
    return true;
}

void UGothicInventoryComponent::ServerDismantleItem_Implementation(const FGuid& InstanceID)
{
    DismantleItem_Authority(InstanceID);
}

bool UGothicInventoryComponent::DismantleItem_Authority(const FGuid& InstanceID)
{
    const FGothicItemInstance* Item = FindItem(InstanceID);
    if (!Item || !Item->Definition)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Inventory: DismantleItem — item %s not found in inventory or equipped gear."),
            *InstanceID.ToString());
        return false;
    }

    // Def is a UObject pointer to the shared asset, so it survives the container
    // mutations below. `Item` itself does not — do not touch it after this point.
    const UGothicItemDefinition* Def = Item->Definition;
    Item = nullptr;

    // If it is being worn, take it off first: that tears down the item's
    // GameplayEffect and returns it to Items, which is where the removal below
    // expects to find it. Skipping this would leave a permanent stat effect
    // applied by an item that no longer exists.
    if (const TOptional<EGothicEquipSlot> EquippedSlot = FindEquippedSlotForItem(InstanceID))
    {
        if (!UnequipSlot_Authority(*EquippedSlot))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Inventory: DismantleItem — could not unequip %s (inventory full); item not dismantled."),
                *InstanceID.ToString());
            return false;
        }
    }

    if (Def->IsMundane())
    {
        // Mundane → Silver. Note this currency has no sink yet — see the Silver
        // declaration in the header.
        Silver += Def->DismantleSilver;
    }
    else
    {
        // Resonant/Pure → Selah returned to the player's attribute
        UAbilitySystemComponent* ASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
        if (ASC)
        {
            if (UGothicAttributeSet* Attrs = const_cast<UGothicAttributeSet*>(
                    ASC->GetSet<UGothicAttributeSet>()))
            {
                Attrs->SetSelah(Attrs->GetSelah() + Def->DismantleSelah);
            }
        }
    }

    RemoveItem(InstanceID);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Sean the Binder — Re-roll and Imbue
// ═══════════════════════════════════════════════════════════════════════════

bool UGothicInventoryComponent::RerollSecondaries(const FGuid& InstanceID, float SelahCost)
{
    if (!HasInventoryAuthority())
    {
        ServerRerollSecondaries(InstanceID, SelahCost);
        return true; // "request sent" — see the header note on return values.
    }
    return RerollSecondaries_Authority(InstanceID, SelahCost);
}

bool UGothicInventoryComponent::ServerRerollSecondaries_Validate(const FGuid& InstanceID, float SelahCost)
{
    // A negative cost would pay the player to re-roll. The affordability check
    // itself lives in the authority implementation, where the ASC is available.
    return SelahCost >= 0.f;
}

void UGothicInventoryComponent::ServerRerollSecondaries_Implementation(const FGuid& InstanceID, float SelahCost)
{
    RerollSecondaries_Authority(InstanceID, SelahCost);
}

bool UGothicInventoryComponent::RerollSecondaries_Authority(const FGuid& InstanceID, float SelahCost)
{
    // FindItemMutable reaches equipped gear as well as the backpack. Nothing
    // below mutates Items or EquippedItems, so holding this pointer is safe —
    // it edits the item in place. Do not add a container mutation here without
    // re-reading the pointer.
    FGothicItemInstance* Item = FindItemMutable(InstanceID);
    if (!Item || !Item->Definition)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Binder: RerollSecondaries — item %s not found in inventory or equipped gear."),
            *InstanceID.ToString());
        return false;
    }

    // Check Selah cost
    UAbilitySystemComponent* ASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
    if (!ASC) return false;

    const UGothicAttributeSet* Attrs = ASC->GetSet<UGothicAttributeSet>();
    if (!Attrs || Attrs->GetSelah() < SelahCost)
    {
        UE_LOG(LogTemp, Warning, TEXT("Binder: Insufficient Selah for re-roll (%.1f < %.1f)"),
            Attrs ? Attrs->GetSelah() : 0.f, SelahCost);
        return false;
    }

    // Pay the cost
    UGothicAttributeSet* MutableAttrs = const_cast<UGothicAttributeSet*>(Attrs);
    MutableAttrs->SetSelah(Attrs->GetSelah() - SelahCost);

    // Re-roll — random, never directed. Selah buys attempts, not outcomes.
    Item->SecondaryStats.Empty();
    const UGothicItemDefinition* Def = Item->Definition;

    if (Def->SecondaryStatSlots > 0 && Def->SecondaryStatPool.Num() > 0)
    {
        TArray<int32> AvailableIndices;
        for (int32 i = 0; i < Def->SecondaryStatPool.Num(); ++i)
        {
            AvailableIndices.Add(i);
        }

        const int32 NumToRoll = FMath::Min(Def->SecondaryStatSlots, Def->SecondaryStatPool.Num());
        for (int32 i = 0; i < NumToRoll; ++i)
        {
            const int32 PickIdx = FMath::RandRange(0, AvailableIndices.Num() - 1);
            const int32 PoolIdx = AvailableIndices[PickIdx];
            AvailableIndices.RemoveAtSwap(PickIdx);

            const FGothicSecondaryStatRange& Range = Def->SecondaryStatPool[PoolIdx];

            FGothicStatRoll Roll;
            Roll.StatType = Range.StatType;
            Roll.Value = FMath::FRandRange(Range.MinValue, Range.MaxValue);
            Item->SecondaryStats.Add(Roll);
        }
    }

    Item->bImbued = true; // Any Selah interaction locks the item

    // If the re-rolled item is currently worn, its live stat effect is now built
    // from stats that no longer exist. Tear it down and rebuild from the new
    // roll — this could not happen before, because equipped gear was unreachable
    // from here. Remove-then-apply, not apply alone: ApplyEquipmentStats
    // overwrites the ActiveStatEffects entry, which would strand the old GE on
    // the ASC forever and stack the new one on top of it.
    if (const TOptional<EGothicEquipSlot> EquippedSlot = FindEquippedSlotForItem(InstanceID))
    {
        const FGothicItemInstance ItemCopy = *Item;
        Item = nullptr;

        RemoveEquipmentStats(*EquippedSlot);
        ApplyEquipmentStats(*EquippedSlot, ItemCopy);

        RecalculateStrain();
        OnItemEquipped.Broadcast(*EquippedSlot, ItemCopy);
    }

    return true;
}

bool UGothicInventoryComponent::ImbueStar(const FGuid& InstanceID, float SelahCost)
{
    if (!HasInventoryAuthority())
    {
        ServerImbueStar(InstanceID, SelahCost);
        return true; // "request sent" — see the header note on return values.
    }
    return ImbueStar_Authority(InstanceID, SelahCost);
}

bool UGothicInventoryComponent::ServerImbueStar_Validate(const FGuid& InstanceID, float SelahCost)
{
    return SelahCost >= 0.f;
}

void UGothicInventoryComponent::ServerImbueStar_Implementation(const FGuid& InstanceID, float SelahCost)
{
    ImbueStar_Authority(InstanceID, SelahCost);
}

bool UGothicInventoryComponent::ImbueStar_Authority(const FGuid& InstanceID, float SelahCost)
{
    // Reaches equipped gear as well as the backpack. No container mutation
    // below, so the pointer stays valid throughout.
    FGothicItemInstance* Item = FindItemMutable(InstanceID);
    if (!Item || !Item->Definition)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Binder: ImbueStar — item %s not found in inventory or equipped gear."),
            *InstanceID.ToString());
        return false;
    }

    if (Item->CurrentStars >= Item->StarCeiling)
    {
        UE_LOG(LogTemp, Warning, TEXT("Binder: %s already at max stars (%d/%d)"),
            *Item->Definition->ItemID.ToString(), Item->CurrentStars, Item->StarCeiling);
        return false;
    }

    // Check Selah cost
    UAbilitySystemComponent* ASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
    if (!ASC) return false;

    const UGothicAttributeSet* Attrs = ASC->GetSet<UGothicAttributeSet>();
    if (!Attrs || Attrs->GetSelah() < SelahCost)
    {
        UE_LOG(LogTemp, Warning, TEXT("Binder: Insufficient Selah for imbue (%.1f < %.1f)"),
            Attrs ? Attrs->GetSelah() : 0.f, SelahCost);
        return false;
    }

    // Pay and imbue
    UGothicAttributeSet* MutableAttrs = const_cast<UGothicAttributeSet*>(Attrs);
    MutableAttrs->SetSelah(Attrs->GetSelah() - SelahCost);

    Item->CurrentStars++;
    Item->bImbued = true;

    // No stat effect to rebuild: stars are inert (see the header). If they ever
    // gain a mechanical effect, this needs the same remove-and-reapply the
    // re-roll path does when the item is equipped.
    if (const TOptional<EGothicEquipSlot> EquippedSlot = FindEquippedSlotForItem(InstanceID))
    {
        OnItemEquipped.Broadcast(*EquippedSlot, *Item);
    }

    return true;
}

void UGothicInventoryComponent::GrantStartingItems(bool bCanonicalRolls)
{
    // Server-authoritative and one-shot. The PlayerState (and this component)
    // survives death, so without the guard every respawn would re-grant a full
    // kit on top of whatever the player has since equipped. The one-shot guard is
    // also what makes a bench respawn identical: a respawned bench pawn does not
    // re-grant, so it keeps the canonical kit granted on its first spawn.
    if (bStartingItemsGranted)
    {
        return;
    }
    if (!HasInventoryAuthority())
    {
        return;
    }

    bStartingItemsGranted = true;

    for (const TObjectPtr<UGothicItemDefinition>& Def : StartingItemDefs)
    {
        if (!Def)
        {
            continue;
        }

        // Roll a real instance so the starting kit has rolled stats like any
        // drop, then equip it straight into its slot. On the bench (bCanonicalRolls)
        // the roll is frozen to midpoints so the kit is identical every session.
        FGothicItemInstance Instance = Def->RollInstance(bCanonicalRolls);
        if (AddItem(Instance))
        {
            // Already on the authority — go straight to the implementation
            // rather than back through the client-routing wrapper.
            EquipItem_Authority(Instance.InstanceID);
        }
    }
}

bool UGothicInventoryComponent::RestoreFromTravelSnapshot(
    const TArray<FGothicItemInstance>& InItems,
    const TArray<FGothicEquippedSlot>& InEquipped)
{
    if (!HasInventoryAuthority())
    {
        return false;
    }

    // Suppress the starting kit before anything else. If a later step fails, the
    // player is better off with a partial restore than with a full kit granted
    // on top of it.
    bStartingItemsGranted = true;

    // Everything goes into the backpack first, equipped items included: EquipItem
    // takes an item OUT of Items, so an item has to be there to be equipped.
    for (const FGothicItemInstance& Item : InItems)
    {
        if (Item.IsValid())
        {
            AddItem(Item);
        }
    }
    for (const FGothicEquippedSlot& Equipped : InEquipped)
    {
        if (Equipped.Item.IsValid())
        {
            AddItem(Equipped.Item);
        }
    }

    // Now re-equip, by instance ID and through the authority implementation —
    // the slot comes from the item definition, exactly as it did the first time,
    // so the pre-travel slot is reproduced without trusting a stored slot value.
    // This is also what rebuilds every derived stat.
    for (const FGothicEquippedSlot& Equipped : InEquipped)
    {
        if (Equipped.Item.IsValid())
        {
            EquipItem_Authority(Equipped.Item.InstanceID);
        }
    }

    RecalculateStrain();

    return true;
}

// =============================================================================
// Debug
// =============================================================================

void UGothicInventoryComponent::DebugSpawnTestItems()
{
    // Helper lambda to create a transient item definition and roll an instance
    auto MakeItem = [this](
        FName ID, FText Name, EGothicItemRarity Rarity, EGothicEquipSlot Slot,
        int32 Tier, EGothicPrimaryStat PrimStat, FVector2D PrimRange,
        float Strain, int32 DismSilver, float DismSelah,
        TArray<FGothicSecondaryStatRange> SecPool, int32 SecSlots)
    {
        UGothicItemDefinition* Def = NewObject<UGothicItemDefinition>(this);
        Def->ItemID = ID;
        Def->DisplayName = Name;
        Def->Description = FText::FromString(FString::Printf(TEXT("A %s item for testing."), *Name.ToString()));
        Def->Rarity = Rarity;
        Def->EquipSlot = Slot;
        Def->GearTier = Tier;
        Def->PrimaryStatType = PrimStat;
        Def->PrimaryStatRange = PrimRange;
        Def->BaseStrainCost = Strain;
        Def->DismantleSilver = DismSilver;
        Def->DismantleSelah = DismSelah;
        Def->SecondaryStatPool = SecPool;
        Def->SecondaryStatSlots = SecSlots;
        Def->MinStarCeiling = FMath::Max(1, Tier);
        Def->MaxStarCeiling = FMath::Min(5, Tier + 2);

        FGothicItemInstance Instance = Def->RollInstance();
        AddItem(Instance);
    };

    // Common secondary stat pool
    TArray<FGothicSecondaryStatRange> ArmorSecondaries;
    ArmorSecondaries.Add({ EGothicSecondaryStat::Damage_Revolver, 4.f, 15.f });
    ArmorSecondaries.Add({ EGothicSecondaryStat::Damage_LeverAction, 4.f, 15.f });
    ArmorSecondaries.Add({ EGothicSecondaryStat::MovementSpeed, 1.f, 5.f });
    ArmorSecondaries.Add({ EGothicSecondaryStat::EvasionChance, 1.f, 4.f });
    ArmorSecondaries.Add({ EGothicSecondaryStat::HealingReceived, 2.f, 6.f });
    ArmorSecondaries.Add({ EGothicSecondaryStat::AbilityHaste, 3.f, 10.f });
    ArmorSecondaries.Add({ EGothicSecondaryStat::SteadfastRate, 1.f, 5.f });

    // ── Salvage tier ─────────────────────────────────────────────────────
    MakeItem(FName("RustedBracer"), FText::FromString("Rusted Bracer"),
        EGothicItemRarity::Salvage, EGothicEquipSlot::LeftArm,
        1, EGothicPrimaryStat::Resolve, FVector2D(3.f, 8.f),
        0.f, 5, 0.f, ArmorSecondaries, 1);

    MakeItem(FName("TornGreaves"), FText::FromString("Torn Greaves"),
        EGothicItemRarity::Salvage, EGothicEquipSlot::LeftLeg,
        1, EGothicPrimaryStat::Conviction, FVector2D(3.f, 8.f),
        0.f, 5, 0.f, ArmorSecondaries, 1);

    // ── Kept tier ────────────────────────────────────────────────────────
    MakeItem(FName("PatrolCoat"), FText::FromString("Patrol Coat"),
        EGothicItemRarity::Kept, EGothicEquipSlot::Chest,
        2, EGothicPrimaryStat::Resolve, FVector2D(6.f, 14.f),
        0.f, 15, 0.f, ArmorSecondaries, 2);

    MakeItem(FName("WornHood"), FText::FromString("Worn Hood"),
        EGothicItemRarity::Kept, EGothicEquipSlot::Head,
        2, EGothicPrimaryStat::Clarity, FVector2D(5.f, 12.f),
        0.f, 12, 0.f, ArmorSecondaries, 1);

    // ── Remembered tier ──────────────────────────────────────────────────
    MakeItem(FName("EmberThreadVest"), FText::FromString("Ember-Thread Vest"),
        EGothicItemRarity::Remembered, EGothicEquipSlot::Chest,
        3, EGothicPrimaryStat::Conviction, FVector2D(10.f, 20.f),
        0.f, 30, 0.f, ArmorSecondaries, 2);

    MakeItem(FName("VigilantMask"), FText::FromString("Vigilant Mask"),
        EGothicItemRarity::Remembered, EGothicEquipSlot::Head,
        3, EGothicPrimaryStat::Clarity, FVector2D(8.f, 18.f),
        0.f, 25, 0.f, ArmorSecondaries, 2);

    MakeItem(FName("HuntersEmblem"), FText::FromString("Hunter's Emblem"),
        EGothicItemRarity::Remembered, EGothicEquipSlot::Wrist,
        3, EGothicPrimaryStat::Clarity, FVector2D(10.f, 22.f),
        0.f, 35, 0.f, ArmorSecondaries, 2);

    // ── Resonant tier (has Strain) ───────────────────────────────────────
    MakeItem(FName("SelahWovenMantle"), FText::FromString("Selah-Woven Mantle"),
        EGothicItemRarity::Resonant, EGothicEquipSlot::Chest,
        4, EGothicPrimaryStat::Resolve, FVector2D(15.f, 28.f),
        25.f, 0, 5.f, ArmorSecondaries, 3);

    MakeItem(FName("PriorFlameGauntlets"), FText::FromString("Prior Flame Gauntlets"),
        EGothicItemRarity::Resonant, EGothicEquipSlot::LeftArm,
        4, EGothicPrimaryStat::Conviction, FVector2D(12.f, 24.f),
        20.f, 0, 4.f, ArmorSecondaries, 3);

    // ── Pure tier (highest Strain, Pilgrimage-only in real gameplay) ─────
    MakeItem(FName("AshenCrown"), FText::FromString("Ashen Crown"),
        EGothicItemRarity::Pure, EGothicEquipSlot::Head,
        5, EGothicPrimaryStat::Clarity, FVector2D(20.f, 35.f),
        40.f, 0, 10.f, ArmorSecondaries, 4);

}

// =============================================================================
// Equipment Stats — Apply/Remove GE per equipped slot
// =============================================================================

void UGothicInventoryComponent::ApplyEquipmentStats(EGothicEquipSlot Slot, const FGothicItemInstance& Item)
{
    if (!EquipmentStatsEffect || !Item.Definition)
    {
        return;
    }

    UAbilitySystemComponent* ASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());

    if (!ASC)
    {
        UE_LOG(LogTemp, Warning, TEXT("EquipStats: No ASC on owner"));
        return;
    }

    FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
    Context.AddSourceObject(this);

    FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
        EquipmentStatsEffect, 1.f, Context);

    if (!Spec.IsValid())
    {
        return;
    }

    // Accumulate every contribution by tag first, then push one SetByCaller per
    // tag. A primary and a secondary can legitimately target the same attribute
    // (Clarity and a rolled Ability Haste line both feed Data.Stat.AbilityHaste),
    // and SetSetByCallerMagnitude overwrites — so setting them one at a time
    // would silently drop whichever came first. Summing first fixes that.
    //
    // Seeded with every tag GE_EquipmentStats declares a modifier for. A modifier
    // whose SetByCaller was never set resolves to 0 and logs
    // "GetMagnitude called ... when magnitude had not yet been set by caller" — one
    // error per unrolled stat per equip, ~129 per playthrough, which buried the real
    // errors in the log. Every op on that effect is AddBase, so an explicit 0 is the
    // same no-op the engine already fell back to, minus the noise.
    //
    // Keep this list in sync with GE_EquipmentStats' Modifiers array. A tag set
    // here with no matching modifier on the asset is a harmless no-op — which is
    // the state Data.Stat.AttackPower and Data.Stat.Defense are in until the two
    // modifier entries are added in the editor; until then Gear Score computes
    // correctly and moves nothing.
    TMap<FName, float> TagTotals;
    for (const FName& StatTag : {
            FName("Data.Stat.MaxHealth"),
            FName("Data.Stat.MovementSpeed"),
            FName("Data.Stat.EvasionChance"),
            FName("Data.Stat.AbilityHaste"),
            FName("Data.Stat.VitalPointRadius"),
            FName("Data.Stat.SteadfastRate"),
            FName("Data.Stat.HealingReceived"),
            FName("Data.Stat.ReloadSpeed"),
            FName("Data.Stat.AttackPower"),
            FName("Data.Stat.Defense") })
    {
        TagTotals.Add(StatTag, 0.f);
    }

    // Gear Score → Attack Power and Defense (ITEMIZATION_AND_LOOT.md, "Gear
    // Score → Attack Power & Defense"). The doc states the stats as totals —
    // AP = 15 + 1.0 x GearScore, Def = 8 + 0.5 x GearScore — and this effect is
    // per-slot, so each armor piece contributes its own tier's share. The ten
    // slots sum to exactly the doc's line, and the base 15/8 stays where it
    // already lives, on GE_InitStats_Player.
    //
    // Per-slot rather than recomputed globally on every equip because the
    // add/remove lifecycle already exists and is already correct: unequipping a
    // piece removes its handle, and its contribution leaves with it. A global
    // recompute would need a second effect and a second thing to forget.
    //
    // Weapons contribute nothing: their tier is already paid out through the
    // weapon's own damage multiplier, and Salvage contributes nothing because it
    // sits outside the tier ladder (GetGearScoreTier).
    if (IsGothicArmorSlot(Slot))
    {
        const float TierPoints = static_cast<float>(Item.Definition->GetGearScoreTier());
        TagTotals.FindOrAdd(FName("Data.Stat.AttackPower")) += TierPoints * AttackPowerPerGearTier;
        TagTotals.FindOrAdd(FName("Data.Stat.Defense"))     += TierPoints * DefensePerGearTier;
    }

    // Primary stat → its creed-mapped attribute (PROGRESSION_STATS_AND_BALANCE.md):
    //   Resolve    → MaxHealth      (Endure — health pool scaling)
    //   Clarity    → AbilityHaste   (Remember — ability cooldown rate)
    //   Conviction → SteadfastRate  (Repay — Steadfast generation rate)
    // First-pass single-effect mappings. Each stat governs more in the design
    // (Resolve's mitigation curve, Clarity's crit/vital, Conviction's Selah
    // yield, plus threshold breakpoints); those remain design-only for now.
    FName PrimaryTag = NAME_None;
    switch (Item.Definition->PrimaryStatType)
    {
        case EGothicPrimaryStat::Resolve:    PrimaryTag = FName("Data.Stat.MaxHealth");     break;
        case EGothicPrimaryStat::Clarity:    PrimaryTag = FName("Data.Stat.AbilityHaste");  break;
        case EGothicPrimaryStat::Conviction: PrimaryTag = FName("Data.Stat.SteadfastRate"); break;
    }
    if (!PrimaryTag.IsNone())
    {
        TagTotals.FindOrAdd(PrimaryTag) += Item.PrimaryStatValue;
    }

    // Secondary stats. Each rolled stat maps to its SetByCaller tag; archetype
    // damage lines return NAME_None and are skipped (they're read at fire time,
    // not baked into an attribute). GE_EquipmentStats must declare a modifier
    // for every tag used here.
    for (const FGothicStatRoll& Roll : Item.SecondaryStats)
    {
        const FName TagName = SecondaryStatToSetByCallerTag(Roll.StatType);
        if (!TagName.IsNone())
        {
            TagTotals.FindOrAdd(TagName) += Roll.Value;
        }
    }

    for (const TPair<FName, float>& Pair : TagTotals)
    {
        Spec.Data->SetSetByCallerMagnitude(
            FGameplayTag::RequestGameplayTag(Pair.Key), Pair.Value);
    }

    // A failed application returns an invalid handle. Storing it anyway left a
    // dead entry in ActiveStatEffects that RemoveEquipmentStats then skips, so
    // the slot looked stat-bearing forever while contributing nothing — and the
    // failure itself was silent.
    const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    if (!Handle.WasSuccessfullyApplied())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Inventory: Equipment stat effect failed to apply for slot %d (%s) — ")
            TEXT("the item is equipped but grants no stats."),
            (int32)Slot,
            Item.Definition ? *Item.Definition->ItemID.ToString() : TEXT("Unknown"));
        ActiveStatEffects.Remove(Slot);
        return;
    }

    ActiveStatEffects.Add(Slot, Handle);
}

int32 UGothicInventoryComponent::GetGearScore() const
{
    int32 Score = 0;
    for (const FGothicEquippedSlot& Entry : EquippedItems)
    {
        if (Entry.Item.IsValid() && Entry.Item.Definition && IsGothicArmorSlot(Entry.Slot))
        {
            Score += Entry.Item.Definition->GetGearScoreTier();
        }
    }
    return Score;
}

int32 UGothicInventoryComponent::GetAggregateGearPower() const
{
    int32 Total = 0;
    int32 Count = 0;
    for (const FGothicEquippedSlot& Entry : EquippedItems)
    {
        if (Entry.Item.IsValid())
        {
            Total += Entry.Item.GearPower;
            ++Count;
        }
    }
    return Count > 0 ? Total / Count : 0;
}

float UGothicInventoryComponent::GetArchetypeDamageBonus(EGothicSecondaryStat DamageStat) const
{
    float Sum = 0.f;
    for (const FGothicEquippedSlot& Entry : EquippedItems)
    {
        if (!Entry.Item.IsValid())
        {
            continue;
        }
        for (const FGothicStatRoll& Roll : Entry.Item.SecondaryStats)
        {
            if (Roll.StatType == DamageStat)
            {
                Sum += Roll.Value;
            }
        }
    }
    return Sum;
}

FName UGothicInventoryComponent::SecondaryStatToSetByCallerTag(EGothicSecondaryStat StatType)
{
    switch (StatType)
    {
        // The eleven Damage_* archetype lines intentionally return NAME_None:
        // they are NOT routed through GE_EquipmentStats into an attribute. They
        // apply conditionally at fire time (only for the equipped archetype), so
        // GA_Fire reads them straight off the equipped items via
        // GetArchetypeDamageBonus rather than baking them into a character stat.
        case EGothicSecondaryStat::MovementSpeed:    return FName("Data.Stat.MovementSpeed");
        case EGothicSecondaryStat::EvasionChance:    return FName("Data.Stat.EvasionChance");
        case EGothicSecondaryStat::AbilityHaste:     return FName("Data.Stat.AbilityHaste");
        case EGothicSecondaryStat::VitalPointRadius: return FName("Data.Stat.VitalPointRadius");
        case EGothicSecondaryStat::SteadfastRate:    return FName("Data.Stat.SteadfastRate");

        // Both of these apply cleanly to their attribute, but the attribute has
        // no consumer yet: nothing in the project heals, and reload is instant.
        // They are mapped anyway so the value is real the moment one exists.
        case EGothicSecondaryStat::HealingReceived:  return FName("Data.Stat.HealingReceived");
        case EGothicSecondaryStat::ReloadSpeed:      return FName("Data.Stat.ReloadSpeed");
    }
    return NAME_None;
}

void UGothicInventoryComponent::RemoveEquipmentStats(EGothicEquipSlot Slot)
{
    if (FActiveGameplayEffectHandle* Handle = ActiveStatEffects.Find(Slot))
    {
        UAbilitySystemComponent* ASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());

        if (ASC && Handle->IsValid())
        {
            ASC->RemoveActiveGameplayEffect(*Handle);
        }

        ActiveStatEffects.Remove(Slot);
    }
}