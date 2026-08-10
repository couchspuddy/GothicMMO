// GothicWeaponData.h
// Data asset defining a weapon type's properties.
// Create one per weapon: DA_Weapon_Pistol, DA_Weapon_SpecialRifle, etc.
// The player character holds an array of these; GA_Fire reads from the active one.
// Adding a new weapon requires no recompile — just a new data asset.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Items/GothicItemTypes.h"
#include "UI/GothicHUDTypes.h"
#include "GothicWeaponData.generated.h"

class UGameplayEffect;
class UGothicWeaponPerkCatalog;
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

    /**
     * False for weapons that cannot register a vital hit at all — currently only
     * the Bomb Thrower / Censer Launcher, per WEAPON_ARCHETYPES.md: "an explosion
     * does not find a vital point."
     *
     * Explicit rather than inferred from VitalDamageMultiplier == 1.0. The
     * multiplier being 1.0 says a vital hit is worth nothing extra, which is a
     * balance statement; this flag says a vital hit cannot HAPPEN, which is a
     * capability statement. They coincide on today's twelve assets and would
     * stop coinciding the first time someone tuned a weapon's vital payoff to
     * parity. The perk roll needs the capability answer, so it asks for it.
     *
     * Set false on DA_Weapon_BombThrower when authoring — the default is true so
     * every other existing asset stays correct without an edit.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Damage")
    bool bCanScoreVitalHits = true;

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

    /**
     * How loud this weapon is to the Accursed. GA_Fire reports a hearing stimulus
     * at the shooter on every shot; this is the Loudness passed to
     * UAISense_Hearing::ReportNoiseEvent.
     *
     * It is NOT a distance. Loudness MULTIPLIES each listener's own HearingRange
     * (AISense_Hearing::Update compares DistToSoundSq against
     * HearingRangeSq * Loudness^2), so the audible radius of a shot is
     * ListenerHearingRange * Loudness, per listener.
     *
     * 1.0 is therefore "audible at exactly the range the listener is configured
     * to hear" — 800 cm for everything deriving from AGothicEnemyBase. That is
     * the only value derivable from the existing configuration rather than
     * invented, so it is the default and no weapon is quieter or louder until
     * someone authors it.
     *
     * For reference when tuning: 1.875 would make a shot audible at 1500 cm,
     * matching the enemy's SightRadius; 2.5 would match the 2000 cm
     * LoseSightRadius. A Sawed-Off wanting to be heard across a street and a
     * Derringer wanting to stay quiet are exactly what this field is for.
     *
     * Zero silences the weapon entirely.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.0", UIMax = "3.0"))
    float FireNoiseLoudness = 1.f;

    // -------------------------------------------------------------------------
    // Shock — the electrical repeater's two hooks. Both default to off, so every
    // ordinary weapon is unaffected and this stays a property of the one Rig
    // that sets them rather than a rule of the whole gun sandbox.
    // -------------------------------------------------------------------------

    /** Per-hit chance (0-1) to apply ShockStunEffect to the target. 0 disables. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Shock",
              meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float StunChance = 0.f;

    /**
     * The GE granting State.Stunned. Rolled per hit against StunChance.
     *
     * Applying this to an ENEMY only did anything cosmetic until the stun was
     * made to actually halt them — see AGothicEnemyBase's stun handling. Before
     * that, State.Stunned drove an animation flag and blocked player abilities,
     * and nothing else.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Shock")
    TSubclassOf<UGameplayEffect> ShockStunEffect;

    /**
     * Consecutive hits with no miss before Oversurge can roll. 0 disables.
     *
     * The streak lives on the player, not the weapon asset, and resets on a
     * miss or a weapon swap — otherwise you could bank hits with one gun and
     * cash them with another.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Shock",
              meta = (ClampMin = "0"))
    int32 OversurgeHitsRequired = 0;

    /** Chance (0-1) that a qualifying hit oversurges. Rolled every hit at or past the streak. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Shock",
              meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OversurgeChance = 0.f;

    /** Damage multiplier applied when Oversurge fires. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Shock",
              meta = (ClampMin = "1.0"))
    float OversurgeDamageMultiplier = 6.f;

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

    /**
     * When the shot that empties the magazine lands, reload from reserve at once
     * without waiting for the reload key.
     *
     * Per-weapon rather than a character-wide policy because it is a property of
     * the action the gun models: a revolver or a repeater topping itself off is
     * invisible bookkeeping, while a bolt-action's cycle is the point of firing
     * one. Authoring it here also means a weapon that should NOT do it is a data
     * edit rather than a code branch.
     *
     * Ignored when bUsesAmmo is false — those weapons never reload at all. Does
     * nothing when the reserve is empty: the reload simply fails and the player
     * is left dry, and Steadfast is NEVER spent automatically to cover it.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (EditCondition = "bUsesAmmo"))
    bool bAutoReloadWhenEmpty = true;

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

    /**
     * GE applied to the shooter on a LANDED hit with this weapon (GE_SuperMeterGain
     * on every DA_Weapon_*). Read by GA_Fire; a miss builds nothing.
     *
     * Authored on all 12 weapon assets since they were written, but nothing read it
     * until now — shooting built no super meter at all, and melee (GA_HuntersStrike)
     * plus kills were the only sources.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback")
    TSubclassOf<UGameplayEffect> SuperGainOnHitEffect;

    /**
     * Super meter granted per landed hit, sent to SuperGainOnHitEffect as the
     * GothicTags::Data_SuperMeter SetByCaller — the same contract GA_HuntersStrike
     * uses for melee.
     *
     * The default is DERIVED, not designed: melee grants 15 per swing at roughly one
     * swing a second, and the class-default 171 RPM is 2.85 shots a second, so 5 per
     * shot puts ranged super income (~14/sec) at parity with melee. At MaxSuperMeter
     * 100 that is 20 landed shots to a full bar. Wants a real design pass, and per-
     * weapon values on the 12 DA_Weapon_* assets — a fast Gatling and a bolt-action
     * should almost certainly not both grant 5.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback", meta = (ClampMin = "0.0"))
    float SuperGainOnHit = 5.f;

    // ── Hit-stop (feel pass) ──────────────────────────────────────────────
    // A shooter-LOCAL micro-pause on a landed hit that sells impact without audio.
    // Gated on this flag rather than on weapon names, per the feel-pass brief, so a
    // designer decides which archetypes are "heavy" in data. Default off: no weapon
    // hitches until an asset opts in, which keeps every light sidearm untouched.

    /** True for weapons heavy enough to earn a hit-stop on a landed hit. Off = no hitch. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback|HitStop")
    bool bHeavyWeapon = false;

    /**
     * Real-time length of the shooter-local hit-stop, in seconds. The feel-pass brief
     * asks for 0.03–0.05s. Restored on the world clock, so the pawn's own dilation
     * never stretches it. Ignored unless bHeavyWeapon is true.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback|HitStop",
              meta = (EditCondition = "bHeavyWeapon", ClampMin = "0.0", ClampMax = "0.2"))
    float HitStopDuration = 0.04f;

    /**
     * Actor time-dilation applied to the SHOOTER'S pawn only during the hit-stop
     * window — lower is a harder freeze. This is per-actor CustomTimeDilation, never
     * global time dilation: only the shooter feels it, the server and co-op peers are
     * untouched. Ignored unless bHeavyWeapon is true.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Feedback|HitStop",
              meta = (EditCondition = "bHeavyWeapon", ClampMin = "0.01", ClampMax = "1.0"))
    float HitStopTimeDilation = 0.05f;

    // ── Perks ────────────────────────────────────────────────────────────

    /**
     * The perk catalog this weapon rolls from (DA_WeaponPerkCatalog — one asset
     * for the whole project). Null means this weapon rolls no perks at all,
     * which is the correct behaviour until the asset is authored: an unset
     * catalog degrades to today's perkless drops rather than to a crash.
     *
     * Lives here rather than on a global setting because the per-weapon
     * exclusion list below already makes every weapon asset a stop on the
     * authoring pass, so the reference costs no extra editor work.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Perks")
    TObjectPtr<UGothicWeaponPerkCatalog> PerkCatalog;

    /**
     * Exclusion layer 3 — the hand-curated per-weapon list from
     * WEAPON_PERK_TABLES.md's "Per-Weapon Curation" table. These are design
     * judgment calls that no flag can derive: True Bore is excluded on the
     * Gatling because degrading accuracy is the point of holding the trigger,
     * not because the weapon lacks a capability.
     *
     * Layers 1 and 2 (capability flags, magazine threshold) are applied
     * automatically and must NOT be duplicated here — listing Steady Read on the
     * Bomb Thrower is harmless but redundant, and hides the fact that a future
     * vital-less weapon gets that exclusion for free.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Perks",
              meta = (Categories = "Perk.Weapon"))
    TArray<FGameplayTag> ExcludedPerks;
};

/**
 * Runtime state for one equipped weapon.
 * The data asset defines the weapon; this struct tracks ammo at runtime.
 */
/**
 * One weapon slot's ammo, detached from the slot itself.
 *
 * This is what gets banked when a pawn is about to stop existing — death, and
 * (via the GameInstance) level travel — and replayed into the replacement
 * pawn's slots. It carries RAW counts, deliberately: Deep Reserves and Extended
 * Magazine change the effective ceilings, and a player who dies holding a perked
 * gun and respawns with an unperked one must land under the NEW ceiling rather
 * than keep a number the new slot cannot hold. The clamp therefore belongs to
 * the restore (AGothicPlayerCharacter::ApplyAmmoSnapshot), not to the capture.
 *
 * SlotIndex rather than a weapon pointer: the slots are positional (0 Sidearm,
 * 1 Piece, 2 Rig) and the restore re-checks the slot's WeaponData anyway, so
 * indexing is both sufficient and immune to a data asset being reassigned.
 */
USTRUCT(BlueprintType)
struct FGothicAmmoSlotSnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    int32 SlotIndex = INDEX_NONE;

    UPROPERTY()
    int32 Magazine = 0;

    UPROPERTY()
    int32 Reserve = 0;
};

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

    /**
     * Which rolled copy is in this slot. Invalid when the slot was filled from
     * the Blueprint default loadout rather than from inventory.
     *
     * This is the seam WEAPON_PERK_TABLES.md's implementation note asks for. The
     * slot used to keep the shared UGothicWeaponData asset and nothing else, so
     * two drops of the same archetype could never differ — the instance was
     * dropped at the equip boundary. The ID is kept alongside the perks so a
     * consumer can tell "this is a different copy of the same gun" apart from
     * "this is the same copy", which the perk list alone cannot answer.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Weapon")
    FGuid EquippedInstanceID;

    /**
     * Perks rolled onto the equipped copy, copied from FGothicItemInstance at
     * equip time. Copied rather than referenced because the fire path reads this
     * every shot and must not chase a pointer into inventory storage that a
     * reorder could move.
     *
     * Empty for mundane drops, ammo-less default loadouts, and every weapon
     * until DA_WeaponPerkCatalog is authored.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Weapon")
    TArray<FGameplayTag> Perks;

    /** True if the equipped copy rolled this perk. The one call every effect goes through. */
    bool HasPerk(const FGameplayTag& Perk) const { return Perks.Contains(Perk); }

    /**
     * Deep Reserves (+50% max reserve ammo).
     *
     * Every read of MaxReserveAmmo goes through here rather than the asset, so
     * the ceiling, the HUD's max, and the Steadfast refill's headroom all agree.
     */
    int32 GetEffectiveMaxReserve() const;

    /** Starting reserve with the same Deep Reserves scaling, so a fresh equip is not instantly below its own ceiling. */
    int32 GetEffectiveStartingReserve() const;

    /**
     * Extended Magazine (+25% magazine capacity, rounded up).
     *
     * Same treatment GetEffectiveMaxReserve got, and for the same reason: the
     * initial fill, the reload's headroom, and the HUD's denominator must all
     * read one number or the magazine can sit above a cap that says it cannot.
     */
    int32 GetEffectiveMagazineCapacity() const;

    /** Initialize ammo from the weapon data's defaults. Ammo-less weapons stay at zero. */
    void InitFromData()
    {
        if (WeaponData && WeaponData->bUsesAmmo)
        {
            CurrentMagazine = GetEffectiveMagazineCapacity();
            CurrentReserve = GetEffectiveStartingReserve();
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