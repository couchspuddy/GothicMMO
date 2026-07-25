// GothicWeaponData.h
// Data asset defining a weapon type's properties.
// Create one per weapon: DA_Weapon_Pistol, DA_Weapon_SpecialRifle, etc.
// The player character holds an array of these; GA_Fire reads from the active one.
// Adding a new weapon requires no recompile — just a new data asset.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Items/GothicItemTypes.h"
#include "UI/GothicHUDTypes.h"
#include "GothicWeaponData.generated.h"

class UGameplayEffect;
class UNiagaraSystem;
class UStaticMesh;

/**
 * The eleven weapon archetypes. Each weapon is exactly one, and armor's
 * per-archetype damage lines (EGothicSecondaryStat::Damage_*) only apply while a
 * weapon of the matching archetype is equipped. Order mirrors the Damage_*
 * block in EGothicSecondaryStat so GetArchetypeDamageStat can map by ordinal.
 */
UENUM(BlueprintType)
enum class EGothicWeaponArchetype : uint8
{
    Revolver         UMETA(DisplayName = "Revolver"),
    RepeatingPistol  UMETA(DisplayName = "Repeating Pistol"),
    Derringer        UMETA(DisplayName = "Derringer"),
    LeverAction      UMETA(DisplayName = "Lever-Action Repeater"),
    BoltAction       UMETA(DisplayName = "Bolt-Action Rifle"),
    SawedOff         UMETA(DisplayName = "Sawed-Off"),
    Carbine          UMETA(DisplayName = "Carbine"),
    GatlingRig       UMETA(DisplayName = "Gatling Rig"),
    BombThrower      UMETA(DisplayName = "Bomb Thrower"),
    Breacher         UMETA(DisplayName = "Breacher"),
    HeavyMelee       UMETA(DisplayName = "Heavy Melee"),
};

/**
 * Maps a weapon archetype to the armor damage line that sharpens it. The two
 * enums declare their eleven entries in the same order, so this is a straight
 * ordinal cast — kept as a named function so the coupling is a single, findable
 * point rather than an inline cast scattered across the damage math.
 */
FORCEINLINE EGothicSecondaryStat GetArchetypeDamageStat(EGothicWeaponArchetype Archetype)
{
    return static_cast<EGothicSecondaryStat>(static_cast<uint8>(Archetype));
}

UCLASS(BlueprintType)
class GOTHICMMO_API UGothicWeaponData : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** Display name shown in HUD/UI. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
    FText WeaponName;

    /**
     * Which weapon slot this archetype belongs in — Sidearm, Piece, or Rig.
     * The item definition carries its own EquipSlot; the inventory refuses the
     * equip when the two disagree, so a Rig cannot be authored into a Sidearm slot.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
    EGothicEquipSlot IntendedSlot = EGothicEquipSlot::Sidearm;

    // ── Visual ──────────────────────────────────────────────────────────

    /** First-person weapon mesh. Assign a static mesh for now; swap to skeletal when animated weapons arrive. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
    TObjectPtr<UStaticMesh> WeaponMesh;

    /**
     * Offset from the HandGrip_R socket. Tune per weapon in the data asset.
     *
     * Defaults to zero because the socket is already positioned in the hand —
     * a weapon needs an offset only if its pivot is off. The old default of
     * (30, 15, -15) dated from when the mesh was attached to the CAMERA; once
     * it moved to the hand socket that became a stale nudge that floated the
     * gun forward and to the right. Six weapons were corrected by hand and
     * five were left carrying it, which is why some sat wrong and some did not.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
    FVector MeshOffset = FVector::ZeroVector;

    /** Rotation offset — lets you orient each weapon model without editing the mesh. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
    FRotator MeshRotation = FRotator::ZeroRotator;

    /** Scale override. Most placeholder meshes will need this. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
    FVector MeshScale = FVector(1.f);

    // ── Damage ───────────────────────────────────────────────────────────

    /** Which of the eleven archetypes this weapon is. Selects which armor
     *  Damage_* line sharpens it. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Damage")
    EGothicWeaponArchetype Archetype = EGothicWeaponArchetype::Revolver;

    /** Base damage per shot before vital multiplier. This is the weapon's own
     *  attack power — armor never scales it universally, only the matching
     *  archetype line and the aggregate Gear Power floor touch it. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Damage")
    float Damage = 15.f;

    /** Multiplier applied on a confirmed vital hit. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Damage")
    float VitalDamageMultiplier = 2.f;

    /** The GameplayEffect that deals the damage. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Damage")
    TSubclassOf<UGameplayEffect> DamageEffect;

    // ── Fire Rate & Range ────────────────────────────────────────────────

    /**
     * Fire rate in rounds per minute. This is the authoring value — GA_Fire converts
     * it to seconds and feeds the cooldown GE's duration through the Data.Cooldown
     * SetByCaller, so every weapon shares one cooldown asset.
     * 60 = one shot a second, 600 = ten.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = 1, UIMin = 30, UIMax = 900))
    float RoundsPerMinute = 171.f;

    /**
     * Optional per-weapon cooldown GE. Leave null and the weapon uses GA_Fire's own
     * cooldown effect with the duration driven by RoundsPerMinute — which is what
     * every ordinary weapon wants. Set it only for a weapon whose cooldown needs
     * different tags or stacking (charge-up, burst), and note that whatever you
     * assign must still take its duration from the Data.Cooldown SetByCaller.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
    TSubclassOf<UGameplayEffect> CooldownEffect;

    /** Hitscan range in cm. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
    float TraceRange = 5000.f;

    /** Seconds between shots. Zero or negative RPM yields no cooldown rather than a divide by zero. */
    UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
    float GetFireInterval() const { return RoundsPerMinute > 0.f ? 60.f / RoundsPerMinute : 0.f; }

    /** Body-shot DPS, ignoring reloads and magazine limits. Balance reference, not a runtime value. */
    UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
    float GetSustainedDPS() const { return Damage * RoundsPerMinute / 60.f; }

    /** DPS assuming every shot lands on a vital point. The ceiling half of the balance range. */
    UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
    float GetVitalDPS() const { return GetSustainedDPS() * VitalDamageMultiplier; }

    // ── Ammo ─────────────────────────────────────────────────────────────

    /**
     * False for weapons that never consume ammo — the Heavy Melee Rig.
     * When false the magazine and reserve fields are ignored entirely: the weapon
     * always has a round chambered, firing consumes nothing, and it never reloads.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
    bool bUsesAmmo = true;

    /** Rounds per magazine. Ignored when bUsesAmmo is false. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (EditCondition = "bUsesAmmo"))
    int32 MagazineCapacity = 6;

    /** Maximum reserve ammo. Ignored when bUsesAmmo is false. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (EditCondition = "bUsesAmmo"))
    int32 MaxReserveAmmo = 18;

    /** Reserve ammo the weapon starts with. Ignored when bUsesAmmo is false. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (EditCondition = "bUsesAmmo"))
    int32 StartingReserveAmmo = 18;

    /**
     * Steadfast charges spent converting a charge into reserve ammo.
     * Tiered by slot per design: Sidearm = 1, Piece = 2, Rig = 3 — a Rig refill
     * leaves zero defensive charges, which is the tension the tiering exists for.
     * Read GetSteadfastRefillCost() rather than this field; it returns 0 for
     * weapons that carry no ammo.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (EditCondition = "bUsesAmmo", ClampMin = 0))
    int32 SteadfastRefillCost = 1;

    /** Reserve rounds granted per Steadfast refill. Ignored when bUsesAmmo is false. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (EditCondition = "bUsesAmmo", ClampMin = 0))
    int32 SteadfastRefillAmount = 12;

    /** Steadfast cost to refill this weapon. Zero for weapons that carry no ammo. */
    UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
    int32 GetSteadfastRefillCost() const { return bUsesAmmo ? SteadfastRefillCost : 0; }

    // ── Feedback ─────────────────────────────────────────────────────────

    /** Crosshair type shown when this weapon is active. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback")
    EGothicCrosshairType CrosshairType = EGothicCrosshairType::Pistol;

    /**
     * Camera pitch kick on fire (degrees, negative = upward).
     * Revolver: -0.8, Repeating Pistol: -0.3, Bolt-Action: -1.5, etc.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback")
    float RecoilPitch = -0.5f;

    /**
     * Random horizontal spread added to recoil (degrees).
     * 0 = perfectly vertical kick. Higher = more unpredictable.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback")
    float RecoilYawSpread = 0.f;

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

    /**
     * Gear Power of the equipped copy, carried over from FGothicItemInstance
     * when the slot is filled from the inventory. Zero means "no rolled copy
     * behind this slot" — a weapon assigned directly on the Blueprint default
     * loadout — and is treated as the baseline, i.e. no scaling.
     *
     * This exists because the slot used to keep only the UGothicWeaponData
     * asset, which is shared by every copy of an archetype. A Salvage Revolver
     * and a Pure Revolver therefore hit identically: the instance was dropped
     * at the equip boundary and could never reach the fire trace.
     *
     * NOTE: the instance's rolled SecondaryStats are still not carried here,
     * and are applied nowhere in the project — see ApplyEquipmentStats, which
     * maps PrimaryStatValue only. Connecting those is separate work affecting
     * all ten armour slots, not just weapons.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Weapon")
    int32 GearPower = 0;

    /** Initialize ammo from the weapon data's defaults. Ammo-less weapons stay at zero. */
    void InitFromData()
    {
        if (WeaponData && WeaponData->bUsesAmmo)
        {
            CurrentMagazine = WeaponData->MagazineCapacity;
            CurrentReserve = WeaponData->StartingReserveAmmo;
        }
        else
        {
            CurrentMagazine = 0;
            CurrentReserve = 0;
        }
    }

    /** True if this slot can fire — ammo-less weapons are always ready. */
    bool HasAmmo() const
    {
        if (!WeaponData)
        {
            return false;
        }
        return !WeaponData->bUsesAmmo || CurrentMagazine > 0;
    }
};