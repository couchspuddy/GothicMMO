// GothicInventoryComponent.cpp

#include "Items/GothicInventoryComponent.h"
#include "Items/GothicItemDefinition.h"
#include "Weapons/GothicWeaponData.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"

UGothicInventoryComponent::UGothicInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

// ═══════════════════════════════════════════════════════════════════════════
// Inventory
// ═══════════════════════════════════════════════════════════════════════════

bool UGothicInventoryComponent::AddItem(const FGothicItemInstance& Item)
{
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

const FGothicItemInstance* UGothicInventoryComponent::FindItem(const FGuid& InstanceID) const
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

FGothicItemInstance* UGothicInventoryComponent::FindItemMutable(const FGuid& InstanceID)
{
    for (FGothicItemInstance& Item : Items)
    {
        if (Item.InstanceID == InstanceID)
        {
            return &Item;
        }
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Equipment
// ═══════════════════════════════════════════════════════════════════════════

bool UGothicInventoryComponent::EquipItem(const FGuid& InstanceID)
{
    const FGothicItemInstance* Item = FindItem(InstanceID);
    if (!Item || !Item->Definition)
    {
        UE_LOG(LogTemp, Warning, TEXT("Inventory: EquipItem — item not found"));
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

    // Unequip current item in that slot first
    if (EquippedItems.Contains(Slot))
    {
        UnequipSlot(Slot);
    }

    // Move from inventory to equipped
    FGothicItemInstance ItemCopy = *Item;
    RemoveItem(InstanceID);
    EquippedItems.Add(Slot, ItemCopy);
    ApplyEquipmentStats(Slot, ItemCopy);

    RecalculateStrain();
    OnItemEquipped.Broadcast(Slot, ItemCopy);


    return true;
}

bool UGothicInventoryComponent::UnequipSlot(EGothicEquipSlot Slot)
{
    if (!EquippedItems.Contains(Slot))
    {
        return false;
    }

    FGothicItemInstance Unequipped = EquippedItems[Slot];
    RemoveEquipmentStats(Slot);
    EquippedItems.Remove(Slot);

    // Return to inventory
    Items.Add(Unequipped);
    RecalculateStrain();


    return true;
}

const FGothicItemInstance* UGothicInventoryComponent::GetEquippedItem(EGothicEquipSlot Slot) const
{
    return EquippedItems.Find(Slot);
}

bool UGothicInventoryComponent::IsSlotEquipped(EGothicEquipSlot Slot) const
{
    return EquippedItems.Contains(Slot);
}

// ═══════════════════════════════════════════════════════════════════════════
// Resonance Strain
// ═══════════════════════════════════════════════════════════════════════════

float UGothicInventoryComponent::GetCurrentStrain() const
{
    float Total = 0.f;
    for (const auto& Pair : EquippedItems)
    {
        Total += Pair.Value.StrainCost;
    }
    return Total;
}

bool UGothicInventoryComponent::WouldExceedStrain(const FGothicItemInstance& Item) const
{
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
    const FGothicItemInstance* Item = FindItem(InstanceID);
    if (!Item || !Item->Definition)
    {
        UE_LOG(LogTemp, Warning, TEXT("Inventory: DismantleItem — item not found"));
        return false;
    }

    const UGothicItemDefinition* Def = Item->Definition;

    if (Def->IsMundane())
    {
        // Mundane → Silver
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
    FGothicItemInstance* Item = FindItemMutable(InstanceID);
    if (!Item || !Item->Definition)
    {
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


    return true;
}

bool UGothicInventoryComponent::ImbueStar(const FGuid& InstanceID, float SelahCost)
{
    FGothicItemInstance* Item = FindItemMutable(InstanceID);
    if (!Item || !Item->Definition)
    {
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


    return true;
}

void UGothicInventoryComponent::GrantStartingItems()
{
    // Server-authoritative and one-shot. The PlayerState (and this component)
    // survives death, so without the guard every respawn would re-grant a full
    // kit on top of whatever the player has since equipped.
    if (bStartingItemsGranted)
    {
        return;
    }
    if (GetOwner() && !GetOwner()->HasAuthority())
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
        // drop, then equip it straight into its slot.
        FGothicItemInstance Instance = Def->RollInstance();
        if (AddItem(Instance))
        {
            EquipItem(Instance.InstanceID);
        }
    }
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
    // Keep this list in sync with GE_EquipmentStats' Modifiers array.
    TMap<FName, float> TagTotals;
    for (const FName& StatTag : {
            FName("Data.Stat.MaxHealth"),
            FName("Data.Stat.MovementSpeed"),
            FName("Data.Stat.EvasionChance"),
            FName("Data.Stat.AbilityHaste"),
            FName("Data.Stat.VitalPointRadius"),
            FName("Data.Stat.SteadfastRate"),
            FName("Data.Stat.HealingReceived"),
            FName("Data.Stat.ReloadSpeed") })
    {
        TagTotals.Add(StatTag, 0.f);
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

    FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    ActiveStatEffects.Add(Slot, Handle);
}

int32 UGothicInventoryComponent::GetAggregateGearPower() const
{
    int32 Total = 0;
    int32 Count = 0;
    for (const TPair<EGothicEquipSlot, FGothicItemInstance>& Pair : EquippedItems)
    {
        if (Pair.Value.IsValid())
        {
            Total += Pair.Value.GearPower;
            ++Count;
        }
    }
    return Count > 0 ? Total / Count : 0;
}

float UGothicInventoryComponent::GetArchetypeDamageBonus(EGothicSecondaryStat DamageStat) const
{
    float Sum = 0.f;
    for (const TPair<EGothicEquipSlot, FGothicItemInstance>& Pair : EquippedItems)
    {
        if (!Pair.Value.IsValid())
        {
            continue;
        }
        for (const FGothicStatRoll& Roll : Pair.Value.SecondaryStats)
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