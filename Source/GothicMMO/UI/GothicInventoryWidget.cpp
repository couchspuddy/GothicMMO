// GothicInventoryWidget.cpp

#include "UI/GothicInventoryWidget.h"
#include "Items/GothicInventoryComponent.h"
#include "Items/GothicItemDefinition.h"

void UGothicInventoryWidget::InitializeFromInventory(UGothicInventoryComponent* InInventory)
{
    if (!InInventory)
    {
        UE_LOG(LogTemp, Warning, TEXT("InventoryWidget: Null inventory component"));
        return;
    }

    CachedInventory = InInventory;

    // Subscribe to inventory events
    CachedInventory->OnItemAdded.AddDynamic(this, &UGothicInventoryWidget::HandleItemAdded);
    CachedInventory->OnItemRemoved.AddDynamic(this, &UGothicInventoryWidget::HandleItemRemoved);
    CachedInventory->OnItemEquipped.AddDynamic(this, &UGothicInventoryWidget::HandleEquipChanged);
    CachedInventory->OnStrainChanged.AddDynamic(this, &UGothicInventoryWidget::HandleStrainChanged);

    // Initial refresh
    OnInventoryRefreshed();
    OnEquipmentRefreshed();
    OnStrainRefreshed();

    UE_LOG(LogTemp, Log, TEXT("InventoryWidget: Initialized with %d items"), CachedInventory->GetItemCount());
}

void UGothicInventoryWidget::NativeDestruct()
{
    if (CachedInventory)
    {
        CachedInventory->OnItemAdded.RemoveDynamic(this, &UGothicInventoryWidget::HandleItemAdded);
        CachedInventory->OnItemRemoved.RemoveDynamic(this, &UGothicInventoryWidget::HandleItemRemoved);
        CachedInventory->OnItemEquipped.RemoveDynamic(this, &UGothicInventoryWidget::HandleEquipChanged);
        CachedInventory->OnStrainChanged.RemoveDynamic(this, &UGothicInventoryWidget::HandleStrainChanged);
    }
    Super::NativeDestruct();
}

// ═══════════════════════════════════════════════════════════════════════════
// Data Queries
// ═══════════════════════════════════════════════════════════════════════════

TArray<FGothicItemUIData> UGothicInventoryWidget::GetInventoryItems() const
{
    TArray<FGothicItemUIData> Result;
    if (!CachedInventory) return Result;

    for (const FGothicItemInstance& Item : CachedInventory->GetAllItems())
    {
        Result.Add(MakeUIData(Item));
    }
    return Result;
}

bool UGothicInventoryWidget::GetEquippedItemInEquipSlot(EGothicEquipSlot EquipSlot, FGothicItemUIData& OutItem) const
{
    if (!CachedInventory) return false;

    const FGothicItemInstance* Equipped = CachedInventory->GetEquippedItem(EquipSlot);
    if (!Equipped) return false;

    OutItem = MakeUIData(*Equipped);
    return true;
}

float UGothicInventoryWidget::GetCurrentStrain() const
{
    return CachedInventory ? CachedInventory->GetCurrentStrain() : 0.f;
}

float UGothicInventoryWidget::GetResonanceCap() const
{
    return CachedInventory ? CachedInventory->ResonanceCap : 100.f;
}

int32 UGothicInventoryWidget::GetSilver() const
{
    return CachedInventory ? CachedInventory->Silver : 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Actions
// ═══════════════════════════════════════════════════════════════════════════

bool UGothicInventoryWidget::RequestEquip(const FGuid& InstanceID)
{
    if (!CachedInventory) return false;
    return CachedInventory->EquipItem(InstanceID);
}

bool UGothicInventoryWidget::RequestUnequip(EGothicEquipSlot EquipSlot)
{
    if (!CachedInventory) return false;
    return CachedInventory->UnequipSlot(EquipSlot);
}

bool UGothicInventoryWidget::RequestDismantle(const FGuid& InstanceID)
{
    if (!CachedInventory) return false;
    return CachedInventory->DismantleItem(InstanceID);
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal
// ═══════════════════════════════════════════════════════════════════════════

FGothicItemUIData UGothicInventoryWidget::MakeUIData(const FGothicItemInstance& Item) const
{
    FGothicItemUIData Data;
    Data.InstanceID = Item.InstanceID;
    Data.GearPower = Item.GearPower;
    Data.StarCeiling = Item.StarCeiling;
    Data.CurrentStars = Item.CurrentStars;
    Data.PrimaryStatValue = Item.PrimaryStatValue;
    Data.SecondaryStats = Item.SecondaryStats;
    Data.StrainCost = Item.StrainCost;
    Data.bImbued = Item.bImbued;

    if (Item.Definition)
    {
        Data.DisplayName = Item.Definition->DisplayName;
        Data.Description = Item.Definition->Description;
        Data.Rarity = Item.Definition->Rarity;
        Data.EquipSlot = Item.Definition->EquipSlot;
        Data.PrimaryStatType = Item.Definition->PrimaryStatType;
        Data.Icon = Item.Definition->Icon;
    }

    return Data;
}

void UGothicInventoryWidget::HandleItemAdded(const FGothicItemInstance& Item)
{
    OnInventoryRefreshed();
}

void UGothicInventoryWidget::HandleItemRemoved(const FGothicItemInstance& Item)
{
    OnInventoryRefreshed();
}

void UGothicInventoryWidget::HandleEquipChanged(EGothicEquipSlot EquipSlot, const FGothicItemInstance& Item)
{
    OnEquipmentRefreshed();
    OnInventoryRefreshed(); // Item moved between inventory and equipped
}

void UGothicInventoryWidget::HandleStrainChanged()
{
    OnStrainRefreshed();
}