// GothicGameInstance.h
// The one object that outlives a level transition — and therefore the only
// place a player's inventory can be parked while the world is torn down.
//
// ── The bug this exists for ──────────────────────────────────────────────────
//
// Inventory lives on UGothicInventoryComponent, which lives on
// AGothicPlayerState, which lives in the world. UGameplayStatics::OpenLevel
// destroys the world — every actor in it, PlayerStates included — and builds a
// fresh one. So every travel (Palewood → Hearth on encounter completion, the
// contract board, the return trigger) handed the player a brand-new empty
// inventory and re-granted the starting kit on top. Everything they had found
// was gone, silently, with no error anywhere.
//
// The GameInstance is the carrier because it is the only UObject in the chain
// that survives: it is owned by the engine, not the world.
//
// ── Shape of the fix ─────────────────────────────────────────────────────────
//
// CaptureTravelSnapshot is called immediately before OpenLevel; RestoreInto is
// called from the new world's inventory-init seam
// (AGothicPlayerCharacter::InitGASFromPlayerState). The snapshot is ONE-SHOT —
// consumed and cleared on restore — so a later respawn inside the new level
// re-grants nothing and restores nothing.
//
// SINGLE-PLAYER / HOST ONLY, by design for now. There is exactly one snapshot,
// not one per player, and OpenLevel is a local travel that a listen server's
// clients do not follow at all. Proper multiplayer travel is ServerTravel or
// seamless travel; the per-player extension here would be a TMap keyed by a
// stable player identity. Deliberately not built yet — see the PR.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Items/GothicItemTypes.h"
#include "Weapons/GothicWeaponData.h"
#include "GothicGameInstance.generated.h"

class APlayerState;

/**
 * Everything a player carries across a level transition.
 *
 * FGothicItemInstance is plain UPROPERTY data — GUID, a definition pointer (to
 * an asset, which is not world state and stays loaded), rolled primary and
 * secondary values, stars, strain and the rolled WeaponPerks tags — so it
 * copies by value with no fixups needed. That is what makes the snapshot a
 * straight array copy rather than a serialisation format.
 *
 * Backpack and equipped items are stored SEPARATELY because the inventory
 * component stores them separately: an equipped item is removed from Items and
 * held in EquippedItems. Flattening the two here would either lose which slot
 * an item was in or duplicate it.
 */
USTRUCT(BlueprintType)
struct FGothicTravelSnapshot
{
    GENERATED_BODY()

    /** Unequipped items — the backpack. */
    UPROPERTY()
    TArray<FGothicItemInstance> Items;

    /** Equipped items with the slot each occupied, re-equipped through EquipItem on restore. */
    UPROPERTY()
    TArray<FGothicEquippedSlot> EquippedItems;

    /**
     * Per-slot ammo, banked for the same reason the items are: WeaponSlots live
     * on the pawn, and the pawn is destroyed with the world. Without this a
     * travel refilled every magazine — the known gap the travel PR accepted.
     *
     * Restored by parking it on the new world's PlayerState rather than writing
     * slots directly: the restore seam runs BEFORE the slot sync that refills
     * them, so the only correct place to apply it is the after-sync restore the
     * death path already owns. See RestoreTravelSnapshot.
     */
    UPROPERTY()
    TArray<FGothicAmmoSlotSnapshot> Ammo;

    /** Selah balance, read off and written back through the attribute. */
    UPROPERTY()
    float Selah = 0.f;

    /** Silver balance. */
    UPROPERTY()
    int32 Silver = 0;

    /** False until something is captured; cleared again the moment it is consumed. */
    UPROPERTY()
    bool bHasSnapshot = false;

    void Reset()
    {
        Items.Reset();
        EquippedItems.Reset();
        Ammo.Reset();
        Selah = 0.f;
        Silver = 0;
        bHasSnapshot = false;
    }
};

UCLASS()
class GOTHICMMO_API UGothicGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    /**
     * Park the given player's inventory, equipment, Selah and Silver for the
     * level about to be loaded. Call this IMMEDIATELY before OpenLevel — the
     * PlayerState is destroyed with the world, so there is no later chance.
     *
     * BlueprintCallable because three of the four travel sites are Blueprint
     * (WBP_ContractBoardPanel, BP_ReturnTrigger, WBP_CharacterSelect); only
     * AGothicEncounterVolume::ReturnToHub travels from C++.
     *
     * Passing null, or a PlayerState with no inventory, is a no-op that logs —
     * it deliberately does NOT clear an existing snapshot, so a mis-wired
     * Blueprint call cannot destroy a good one.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Travel")
    void CaptureTravelSnapshot(APlayerState* PlayerState);

    /**
     * Restore a pending snapshot into a freshly-initialized player, and consume
     * it. Returns true if a snapshot was applied — the caller uses that to skip
     * the starting-kit grant, though the component's own one-shot guard is set
     * by the restore anyway.
     *
     * Authority-only in effect: it drives AddItem and EquipItem, which refuse on
     * a client.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Travel")
    bool RestoreTravelSnapshot(APlayerState* PlayerState);

    /** True while a captured snapshot is waiting to be consumed. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Travel")
    bool HasTravelSnapshot() const { return TravelSnapshot.bHasSnapshot; }

    /** Drop any pending snapshot — for a deliberate fresh start (new character). */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Travel")
    void ClearTravelSnapshot() { TravelSnapshot.Reset(); }

private:
    /** The single parked snapshot. See the class comment on why there is only one. */
    UPROPERTY()
    FGothicTravelSnapshot TravelSnapshot;
};
