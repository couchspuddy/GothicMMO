// GothicWeaponData.cpp
//
// The header is almost entirely data and inline accessors; this file exists for
// the few slot helpers that need to know about gameplay tags, which the header
// deliberately does not pull in.

#include "Weapons/GothicWeaponData.h"
#include "AbilitySystem/GothicGameplayTags.h"

namespace
{
    // WEAPON_PERK_TABLES.md, Fine-Tune bucket. Constants rather than catalog
    // reads: FGothicWeaponPerkEntry::Magnitude is 0 in the authored asset, so
    // reading it today would silently zero every effect. These are the doc's
    // numbers, and a data-driven Magnitude can supersede them once the catalog
    // carries real coefficients — the call sites do not change when it does.
    constexpr float DeepReservesScale     = 1.5f;   // "+50% max reserve ammo"
    constexpr float ExtendedMagazineScale = 1.25f;  // "+25% magazine capacity, rounded up"
}

int32 FGothicWeaponSlot::GetEffectiveMaxReserve() const
{
    if (!WeaponData || !WeaponData->bUsesAmmo)
    {
        return 0;
    }

    const int32 Base = WeaponData->MaxReserveAmmo;
    return HasPerk(GothicTags::Perk_Weapon_FineTune_DeepReserves)
        ? FMath::CeilToInt(Base * DeepReservesScale)
        : Base;
}

int32 FGothicWeaponSlot::GetEffectiveStartingReserve() const
{
    if (!WeaponData || !WeaponData->bUsesAmmo)
    {
        return 0;
    }

    const int32 Base = WeaponData->StartingReserveAmmo;
    const int32 Scaled = HasPerk(GothicTags::Perk_Weapon_FineTune_DeepReserves)
        ? FMath::CeilToInt(Base * DeepReservesScale)
        : Base;

    // Never hand out more than the ceiling — StartingReserveAmmo equals
    // MaxReserveAmmo on every authored weapon today, but nothing enforces that.
    return FMath::Min(Scaled, GetEffectiveMaxReserve());
}

int32 FGothicWeaponSlot::GetEffectiveMagazineCapacity() const
{
    if (!WeaponData || !WeaponData->bUsesAmmo)
    {
        return 0;
    }

    const int32 Base = WeaponData->MagazineCapacity;

    // Rounded UP, per the doc — on a 5-round Bolt-Action the honest 1.25 is 6.25
    // and truncating would hand back the same 6 a 5-round weapon gets from
    // rounding, making the perk read as +1 on everything small.
    return HasPerk(GothicTags::Perk_Weapon_FineTune_ExtendedMagazine)
        ? FMath::CeilToInt(Base * ExtendedMagazineScale)
        : Base;
}
