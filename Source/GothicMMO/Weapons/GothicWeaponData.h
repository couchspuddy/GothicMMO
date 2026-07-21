// GothicWeaponData.h
// Data asset defining a weapon type's properties.
// Create one per weapon: DA_Weapon_Pistol, DA_Weapon_SpecialRifle, etc.
// The player character holds an array of these; GA_Fire reads from the active one.
// Adding a new weapon requires no recompile — just a new data asset.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UI/GothicHUDTypes.h"
#include "GothicWeaponData.generated.h"

class UGameplayEffect;
class UNiagaraSystem;

UCLASS(BlueprintType)
class GOTHICMMO_API UGothicWeaponData : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** Display name shown in HUD/UI. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
    FText WeaponName;

    // ── Damage ───────────────────────────────────────────────────────────

    /** Base damage per shot before vital multiplier. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Damage")
    float Damage = 15.f;

    /** Multiplier applied on a confirmed vital hit. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Damage")
    float VitalDamageMultiplier = 2.f;

    /** The GameplayEffect that deals the damage. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Damage")
    TSubclassOf<UGameplayEffect> DamageEffect;

    // ── Fire Rate & Range ────────────────────────────────────────────────

    /** Cooldown GE applied per shot. Controls fire rate. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
    TSubclassOf<UGameplayEffect> CooldownEffect;

    /** Hitscan range in cm. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
    float TraceRange = 5000.f;

    // ── Ammo ─────────────────────────────────────────────────────────────

    /** Rounds per magazine. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
    int32 MagazineCapacity = 6;

    /** Maximum reserve ammo. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
    int32 MaxReserveAmmo = 18;

    /** Reserve ammo the weapon starts with. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
    int32 StartingReserveAmmo = 18;

    // ── Feedback ─────────────────────────────────────────────────────────

    /** Crosshair type shown when this weapon is active. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback")
    EGothicCrosshairType CrosshairType = EGothicCrosshairType::Pistol;

    /** Super meter gained per hit with this weapon. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback")
    TSubclassOf<UGameplayEffect> SuperGainOnHitEffect;
};

/**
 * Runtime state for one equipped weapon.
 * The data asset defines the weapon; this struct tracks ammo at runtime.
 */
USTRUCT(BlueprintType)
struct FGothicWeaponSlot
{
    GENERATED_BODY()

    /** The weapon type in this slot. Assign in BP_GothicPlayerCharacter. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
    TObjectPtr<UGothicWeaponData> WeaponData;

    /** Current rounds in the magazine. */
    UPROPERTY(BlueprintReadOnly, Category = "Weapon")
    int32 CurrentMagazine = 0;

    /** Current reserve ammo. */
    UPROPERTY(BlueprintReadOnly, Category = "Weapon")
    int32 CurrentReserve = 0;

    /** Initialize ammo from the weapon data's defaults. */
    void InitFromData()
    {
        if (WeaponData)
        {
            CurrentMagazine = WeaponData->MagazineCapacity;
            CurrentReserve = WeaponData->StartingReserveAmmo;
        }
    }
};