// GothicWeaponPerkCatalog.cpp

#include "Weapons/GothicWeaponPerkCatalog.h"
#include "Weapons/GothicWeaponData.h"

TArray<FGameplayTag> UGothicWeaponPerkCatalog::GetEligiblePerks(
    EGothicPerkBucket Bucket, const UGothicWeaponData* WeaponData) const
{
    TArray<FGameplayTag> Eligible;

    if (!WeaponData)
    {
        return Eligible;
    }

    for (const FGothicWeaponPerkEntry& Entry : Perks)
    {
        if (Entry.Bucket != Bucket || !Entry.PerkTag.IsValid())
        {
            continue;
        }

        // Layer 1 — engine-level capability. Both flags are properties of what
        // the weapon can physically do, so new weapons inherit these exclusions
        // without anyone touching a list.
        if (Entry.bRequiresVitalHits && !WeaponData->bCanScoreVitalHits)
        {
            continue;
        }

        if (Entry.bRequiresAmmo && !WeaponData->bUsesAmmo)
        {
            continue;
        }

        // Layer 2 — numeric threshold. Only meaningful on a weapon that has a
        // magazine at all; the bRequiresAmmo check above has already dropped the
        // ammo-less case for every perk that cares.
        if (Entry.MinMagazineCapacity > 0 && WeaponData->MagazineCapacity < Entry.MinMagazineCapacity)
        {
            continue;
        }

        // Layer 3 — per-weapon curation, hand-authored on the weapon asset.
        if (WeaponData->ExcludedPerks.Contains(Entry.PerkTag))
        {
            continue;
        }

        Eligible.Add(Entry.PerkTag);
    }

    return Eligible;
}

const FGothicWeaponPerkEntry* UGothicWeaponPerkCatalog::FindPerkEntry(const FGameplayTag& PerkTag) const
{
    if (!PerkTag.IsValid())
    {
        return nullptr;
    }

    // Linear scan — the catalog is one small hand-authored asset (18 entries as
    // of WEAPON_PERK_TABLES.md) and the UI resolves at most three tags per item,
    // so a map keyed by tag would cost more to build than it saves.
    for (const FGothicWeaponPerkEntry& Entry : Perks)
    {
        if (Entry.PerkTag == PerkTag)
        {
            return &Entry;
        }
    }

    return nullptr;
}
