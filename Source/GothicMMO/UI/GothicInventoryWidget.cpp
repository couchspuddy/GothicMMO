// GothicInventoryWidget.cpp

#include "UI/GothicInventoryWidget.h"
#include "Items/GothicInventoryComponent.h"
#include "Items/GothicItemDefinition.h"
#include "Weapons/GothicWeaponData.h"
#include "Weapons/GothicWeaponPerkCatalog.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"

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

FGothicCharacterStatSummary UGothicInventoryWidget::GetCharacterStatSummary() const
{
    FGothicCharacterStatSummary Out;

    if (!CachedInventory)
    {
        return Out;
    }

    // The inventory lives on the PlayerState, and so does the ASC — ask the
    // PlayerState directly rather than routing through the pawn, which does not
    // survive death (standing gotcha #4).
    const UAbilitySystemComponent* ASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(CachedInventory->GetOwner());

    if (!ASC)
    {
        return Out;
    }

    Out.Health           = ASC->GetNumericAttribute(UGothicAttributeSet::GetHealthAttribute());
    Out.MaxHealth        = ASC->GetNumericAttribute(UGothicAttributeSet::GetMaxHealthAttribute());
    Out.AttackPower      = ASC->GetNumericAttribute(UGothicAttributeSet::GetAttackPowerAttribute());
    Out.Defense          = ASC->GetNumericAttribute(UGothicAttributeSet::GetDefenseAttribute());
    Out.MaxEther         = ASC->GetNumericAttribute(UGothicAttributeSet::GetMaxEtherAttribute());
    Out.MovementSpeed    = ASC->GetNumericAttribute(UGothicAttributeSet::GetMovementSpeedAttribute());
    Out.EvasionChance    = ASC->GetNumericAttribute(UGothicAttributeSet::GetEvasionChanceAttribute());
    Out.AbilityHaste     = ASC->GetNumericAttribute(UGothicAttributeSet::GetAbilityHasteAttribute());
    Out.VitalPointRadius = ASC->GetNumericAttribute(UGothicAttributeSet::GetVitalPointRadiusAttribute());
    Out.SteadfastRate    = ASC->GetNumericAttribute(UGothicAttributeSet::GetSteadfastRateAttribute());
    Out.HealingReceived  = ASC->GetNumericAttribute(UGothicAttributeSet::GetHealingReceivedAttribute());
    Out.ReloadSpeed      = ASC->GetNumericAttribute(UGothicAttributeSet::GetReloadSpeedAttribute());

    // Presentation strings. See the header for why these exist rather than
    // letting Blueprint convert the floats directly.
    Out.HealthText = FText::FromString(FString::Printf(
        TEXT("%.0f / %.0f"), Out.Health, Out.MaxHealth));
    Out.AttackPowerText    = FText::AsNumber(FMath::RoundToInt(Out.AttackPower));
    Out.DefenseText        = FText::AsNumber(FMath::RoundToInt(Out.Defense));
    Out.EtherText          = FText::AsNumber(FMath::RoundToInt(Out.MaxEther));

    // Signed: these read as bonuses on top of a base, so a bare "0" is
    // ambiguous about whether gear contributed nothing or the stat is broken.
    Out.MovementSpeedText  = FText::FromString(FString::Printf(TEXT("%+.0f"), Out.MovementSpeed));
    Out.SteadfastRateText  = FText::FromString(FString::Printf(TEXT("%+.1f/s"), Out.SteadfastRate));

    // Percentages, and labelled as such -- EvasionChance is rolled against 100
    // in PostGameplayEffectExecute and AbilityHaste is a percent cooldown
    // reduction, so showing them unmarked invited reading 57 as flat points.
    Out.EvasionText        = FText::FromString(FString::Printf(TEXT("%.1f%%"), Out.EvasionChance));
    Out.AbilityHasteText   = FText::FromString(FString::Printf(TEXT("%.0f%%"), Out.AbilityHaste));

    return Out;
}

FGothicEquipComparison UGothicInventoryWidget::CompareToEquipped(const FGuid& InstanceID) const
{
    FGothicEquipComparison Out;

    if (!CachedInventory)
    {
        return Out;
    }

    const FGothicItemInstance* Candidate = CachedInventory->FindItem(InstanceID);
    if (!Candidate || !Candidate->Definition)
    {
        return Out;
    }

    Out.bValid = true;
    Out.Slot   = Candidate->Definition->EquipSlot;

    const FGothicItemInstance* Current = CachedInventory->GetEquippedItem(Out.Slot);
    Out.bSlotEmpty = (Current == nullptr);

    const int32 CurrentGearPower  = Current ? Current->GearPower       : 0;
    const float CurrentPrimary    = Current ? Current->PrimaryStatValue : 0.f;
    const float CurrentStrain     = Current ? Current->StrainCost       : 0.f;

    Out.GearPowerDelta   = Candidate->GearPower       - CurrentGearPower;
    Out.PrimaryStatDelta = Candidate->PrimaryStatValue - CurrentPrimary;
    Out.StrainDelta      = Candidate->StrainCost       - CurrentStrain;

    // Union of both stat sets, not just the candidate's. A stat the equipped
    // item has and the candidate lacks must surface as a loss — otherwise
    // dropping a stat entirely would simply disappear from the comparison.
    TMap<EGothicSecondaryStat, float> Deltas;

    for (const FGothicStatRoll& Roll : Candidate->SecondaryStats)
    {
        Deltas.FindOrAdd(Roll.StatType) += Roll.Value;
    }

    if (Current)
    {
        for (const FGothicStatRoll& Roll : Current->SecondaryStats)
        {
            Deltas.FindOrAdd(Roll.StatType) -= Roll.Value;
        }
    }

    for (const TPair<EGothicSecondaryStat, float>& Pair : Deltas)
    {
        FGothicStatRoll Delta;
        Delta.StatType = Pair.Key;
        Delta.Value    = Pair.Value;
        Out.SecondaryDeltas.Add(Delta);
    }

    return Out;
}

TArray<EGothicEquipSlot> UGothicInventoryWidget::GetAllEquipSlots()
{
    return {
        EGothicEquipSlot::Sidearm,  EGothicEquipSlot::Piece,    EGothicEquipSlot::Rig,
        EGothicEquipSlot::Head,     EGothicEquipSlot::Neck,     EGothicEquipSlot::Chest,
        EGothicEquipSlot::Back,     EGothicEquipSlot::LeftArm,  EGothicEquipSlot::RightArm,
        EGothicEquipSlot::Wrist,    EGothicEquipSlot::LeftLeg,  EGothicEquipSlot::RightLeg,
        EGothicEquipSlot::Feet
    };
}

FText UGothicInventoryWidget::GetEquipSlotDisplayName(EGothicEquipSlot EquipSlot)
{
    switch (EquipSlot)
    {
        case EGothicEquipSlot::Sidearm:  return NSLOCTEXT("Gothic", "Slot_Sidearm",  "Sidearm");
        case EGothicEquipSlot::Piece:    return NSLOCTEXT("Gothic", "Slot_Piece",    "Piece");
        case EGothicEquipSlot::Rig:      return NSLOCTEXT("Gothic", "Slot_Rig",      "Rig");
        case EGothicEquipSlot::Head:     return NSLOCTEXT("Gothic", "Slot_Head",     "Head");
        case EGothicEquipSlot::Neck:     return NSLOCTEXT("Gothic", "Slot_Neck",     "Neck");
        case EGothicEquipSlot::Chest:    return NSLOCTEXT("Gothic", "Slot_Chest",    "Chest");
        case EGothicEquipSlot::Back:     return NSLOCTEXT("Gothic", "Slot_Back",     "Back");
        case EGothicEquipSlot::LeftArm:  return NSLOCTEXT("Gothic", "Slot_LeftArm",  "Left Arm");
        case EGothicEquipSlot::RightArm: return NSLOCTEXT("Gothic", "Slot_RightArm", "Right Arm");
        case EGothicEquipSlot::Wrist:    return NSLOCTEXT("Gothic", "Slot_Wrist",    "Wrist");
        case EGothicEquipSlot::LeftLeg:  return NSLOCTEXT("Gothic", "Slot_LeftLeg",  "Left Leg");
        case EGothicEquipSlot::RightLeg: return NSLOCTEXT("Gothic", "Slot_RightLeg", "Right Leg");
        case EGothicEquipSlot::Feet:     return NSLOCTEXT("Gothic", "Slot_Feet",     "Feet");
    }
    return NSLOCTEXT("Gothic", "Slot_Unknown", "Unknown");
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

    // Rolled weapon perks -> display text for the inspect screen. The rolled tags
    // live on the INSTANCE; their display name and description live on the shared
    // weapon asset's catalog, so resolution walks Definition -> WeaponData ->
    // PerkCatalog. Each of those may be null (non-weapon, or no catalog authored
    // yet), and a tag may not be in the catalog at all — every one of those
    // degrades to the tag's leaf name with an empty description rather than
    // dropping the perk the copy actually rolled.
    const UGothicWeaponData* WeaponData =
        Item.Definition ? Item.Definition->WeaponData.Get() : nullptr;
    const UGothicWeaponPerkCatalog* PerkCatalog =
        WeaponData ? WeaponData->PerkCatalog.Get() : nullptr;

    for (const FGameplayTag& PerkTag : Item.WeaponPerks)
    {
        if (!PerkTag.IsValid())
        {
            continue;
        }

        FGothicPerkUIData PerkData;
        PerkData.Tag = PerkTag;

        if (const FGothicWeaponPerkEntry* Entry = PerkCatalog ? PerkCatalog->FindPerkEntry(PerkTag) : nullptr)
        {
            PerkData.DisplayName = Entry->DisplayName;
            PerkData.Description = Entry->Description;
        }

        // Fallback name — an unresolved tag, or a catalog entry that has no
        // authored DisplayName, still reads as its leaf
        // ("Perk.Weapon.FineTune.DeadHand" -> "DeadHand") rather than as blank.
        if (PerkData.DisplayName.IsEmpty())
        {
            const FString TagString = PerkTag.ToString();
            FString Leaf;
            PerkData.DisplayName = TagString.Split(
                    TEXT("."), nullptr, &Leaf, ESearchCase::IgnoreCase, ESearchDir::FromEnd)
                ? FText::FromString(Leaf)
                : FText::FromString(TagString);
        }

        Data.Perks.Add(PerkData);
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