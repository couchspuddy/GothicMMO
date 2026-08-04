// GothicWeaponData.cpp
//
// The header is almost entirely data and inline accessors; this file exists for
// the few slot helpers that need to know about gameplay tags, which the header
// deliberately does not pull in.

#include "Weapons/GothicWeaponData.h"
#include "AbilitySystem/GothicGameplayTags.h"

namespace
{
    // PILOT EFFECT — Deep Reserves, "+50% max reserve ammo"
    // (WEAPON_PERK_TABLES.md, Fine-Tune bucket). Hardcoded here rather than read
    // from the catalog because the catalog asset does not exist yet; part 2
    // moves this to FGothicWeaponPerkEntry::Magnitude along with the other 16.
    constexpr float DeepReservesScale = 1.5f;
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
