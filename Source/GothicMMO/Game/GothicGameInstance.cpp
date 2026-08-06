// GothicGameInstance.cpp

#include "Game/GothicGameInstance.h"

#include "Game/GothicPlayerState.h"
#include "Character/GothicPlayerCharacter.h"
#include "Items/GothicInventoryComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"

void UGothicGameInstance::CaptureTravelSnapshot(APlayerState* PlayerState)
{
    AGothicPlayerState* PS = Cast<AGothicPlayerState>(PlayerState);
    if (!PS)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicGameInstance: CaptureTravelSnapshot with no GothicPlayerState — nothing captured"));
        return;
    }

    UGothicInventoryComponent* Inventory = PS->GetInventory();
    if (!Inventory)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicGameInstance: CaptureTravelSnapshot — PlayerState has no inventory, nothing captured"));
        return;
    }

    // Overwrite wholesale rather than merge. Two captures without an intervening
    // restore means two travels in a row, and the later one is the truth.
    TravelSnapshot.Reset();
    TravelSnapshot.Items         = Inventory->GetAllItems();
    TravelSnapshot.EquippedItems = Inventory->GetEquippedSlots();
    TravelSnapshot.Silver        = Inventory->Silver;

    if (const UGothicAttributeSet* Attributes = PS->GetGothicAttributeSet())
    {
        TravelSnapshot.Selah = Attributes->GetSelah();
    }

    // Ammo comes off the PAWN — it is the only place WeaponSlots exist. A
    // PlayerState without one (mid-travel, spectating) simply banks no ammo,
    // and the restore then leaves the fresh slots as InitFromData made them,
    // which is the old behaviour rather than a broken one.
    if (const AGothicPlayerCharacter* Player = Cast<AGothicPlayerCharacter>(PS->GetPawn()))
    {
        TravelSnapshot.Ammo = Player->CaptureAmmoSnapshot();
    }

    TravelSnapshot.bHasSnapshot = true;

    UE_LOG(LogTemp, Log, TEXT("GothicGameInstance: travel snapshot captured — %d carried, %d equipped, %.0f Selah, %d Silver"),
        TravelSnapshot.Items.Num(), TravelSnapshot.EquippedItems.Num(),
        TravelSnapshot.Selah, TravelSnapshot.Silver);
}

bool UGothicGameInstance::RestoreTravelSnapshot(APlayerState* PlayerState)
{
    if (!TravelSnapshot.bHasSnapshot)
    {
        return false;
    }

    AGothicPlayerState* PS = Cast<AGothicPlayerState>(PlayerState);
    if (!PS)
    {
        return false;
    }

    UGothicInventoryComponent* Inventory = PS->GetInventory();
    if (!Inventory || !Inventory->HasInventoryAuthority())
    {
        return false;
    }

    // The component does the work: it owns the containers, the one-shot
    // starting-kit guard, and the real EquipItem path that rebuilds
    // GE_EquipmentStats, gear score and the weapon slots. Nothing derived is
    // written by hand here.
    const bool bRestored = Inventory->RestoreFromTravelSnapshot(
        TravelSnapshot.Items, TravelSnapshot.EquippedItems);

    if (!bRestored)
    {
        return false;
    }

    Inventory->Silver = TravelSnapshot.Silver;

    if (UGothicAttributeSet* Attributes = PS->GetGothicAttributeSet())
    {
        // Set the attribute rather than applying an effect: this is a restore of
        // a value the player already owned, not a new grant, and GE_InitStats has
        // already run by the time the restore seam is reached.
        Attributes->SetSelah(TravelSnapshot.Selah);
    }

    // Park the ammo on the PlayerState instead of writing slots here. This
    // function is called from InitGASFromPlayerState BEFORE the weapon-slot sync
    // that refills every magazine, so anything written now would be erased a few
    // lines later. The PlayerState bank is consumed AFTER that sync, by the same
    // restore the death path uses — one seam, both journeys.
    if (TravelSnapshot.Ammo.Num() > 0)
    {
        PS->CacheAmmo(TravelSnapshot.Ammo);
    }

    UE_LOG(LogTemp, Log, TEXT("GothicGameInstance: travel snapshot restored — %d carried, %d equipped, %.0f Selah, %d Silver"),
        TravelSnapshot.Items.Num(), TravelSnapshot.EquippedItems.Num(),
        TravelSnapshot.Selah, TravelSnapshot.Silver);

    // One-shot. A respawn later in this level must not re-run the restore and
    // duplicate the whole inventory.
    TravelSnapshot.Reset();

    return true;
}
