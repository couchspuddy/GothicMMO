// GothicItemTypes.h
// Core types for the item and inventory system.
// Enums, stat definitions, and the item instance struct.
// These are referenced by every other item-system file.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "GothicItemTypes.generated.h"

// ═══════════════════════════════════════════════════════════════════════════
// Enums
// ═══════════════════════════════════════════════════════════════════════════

/** Rarity ladder — locked in ECONOMY_SYSTEM.md. */
UENUM(BlueprintType)
enum class EGothicItemRarity : uint8
{
    Salvage      UMETA(DisplayName = "Salvage"),       // Mundane, lowest
    Kept         UMETA(DisplayName = "Kept"),           // Mundane
    Remembered   UMETA(DisplayName = "Remembered"),     // Mundane, highest non-Selah
    Resonant     UMETA(DisplayName = "Resonant"),       // Selah-bound
    Pure         UMETA(DisplayName = "Pure"),            // Pilgrimage only
};

/** Equipment slot types. Explicit values prevent Blueprint data corruption when entries are added/reordered. */
UENUM(BlueprintType)
enum class EGothicEquipSlot : uint8
{
    // Weapon slots (indices 0-2 map to WeaponSlots array on the player character)
    Sidearm         = 0   UMETA(DisplayName = "Sidearm"),
    Piece           = 1   UMETA(DisplayName = "Piece"),
    Rig             = 2   UMETA(DisplayName = "Rig"),

    // Armor slots (ten, locked per design doc)
    Head            = 10  UMETA(DisplayName = "Head"),
    Neck            = 11  UMETA(DisplayName = "Neck"),
    Chest           = 12  UMETA(DisplayName = "Chest"),
    Back            = 13  UMETA(DisplayName = "Back"),
    LeftArm         = 14  UMETA(DisplayName = "Left Arm"),
    RightArm        = 15  UMETA(DisplayName = "Right Arm"),
    Wrist           = 16  UMETA(DisplayName = "Wrist"),
    LeftLeg         = 17  UMETA(DisplayName = "Left Leg"),
    RightLeg        = 18  UMETA(DisplayName = "Right Leg"),
    Feet            = 19  UMETA(DisplayName = "Feet"),
};

/** Primary stats — creed-mapped, from PROGRESSION_STATS_AND_BALANCE.md. */
UENUM(BlueprintType)
enum class EGothicPrimaryStat : uint8
{
    Resolve     UMETA(DisplayName = "Resolve"),      // Endure — mitigation, health
    Clarity     UMETA(DisplayName = "Clarity"),       // Remember — crit, cooldowns
    Conviction  UMETA(DisplayName = "Conviction"),    // Repay — Steadfast, resource gen
};

/**
 * Secondary stats rolled on gear.
 *
 * Units vary by entry — these are NOT uniformly "flat values", as this comment
 * used to say. The eleven Damage_* lines are PERCENTAGES (armor rolls them at
 * 4-15%), and so are EvasionChance (rolled 2-5%, compared against a 0-100 roll
 * in UGothicAttributeSet) and AbilityHaste (divided by 100 in UGA_Fire).
 * Check the consuming code before doing arithmetic with a roll.
 *
 * HealingReceived and ReloadSpeed are rollable but deliberately inert — they are
 * deferred until self-healing and reload animations exist. Not bugs; leave them.
 */
UENUM(BlueprintType)
enum class EGothicSecondaryStat : uint8
{
    // Per-archetype weapon damage. These are the ONLY damage stats armor rolls —
    // there is deliberately no universal "flat damage" line (raw power lives in
    // Gear Power, not in a rollable stat). A damage line only does anything while
    // its exact archetype is the equipped weapon; GA_Fire reads the active
    // weapon's archetype and applies only the matching one. Order mirrors
    // EGothicWeaponArchetype so the archetype→stat mapping stays legible.
    Damage_Revolver         UMETA(DisplayName = "Revolver Damage"),
    Damage_RepeatingPistol  UMETA(DisplayName = "Repeating Pistol Damage"),
    Damage_Derringer        UMETA(DisplayName = "Derringer Damage"),
    Damage_LeverAction      UMETA(DisplayName = "Lever-Action Damage"),
    Damage_BoltAction       UMETA(DisplayName = "Bolt-Action Damage"),
    Damage_SawedOff         UMETA(DisplayName = "Sawed-Off Damage"),
    Damage_Carbine          UMETA(DisplayName = "Carbine Damage"),
    Damage_GatlingRig       UMETA(DisplayName = "Gatling Rig Damage"),
    Damage_BombThrower      UMETA(DisplayName = "Bomb Thrower Damage"),
    Damage_Breacher         UMETA(DisplayName = "Breacher Damage"),
    Damage_HeavyMelee       UMETA(DisplayName = "Heavy Melee Damage"),

    MovementSpeed       UMETA(DisplayName = "Movement Speed"),
    EvasionChance       UMETA(DisplayName = "Evasion Chance"),
    HealingReceived     UMETA(DisplayName = "Healing Received"),
    AbilityHaste        UMETA(DisplayName = "Ability Haste"),
    VitalPointRadius    UMETA(DisplayName = "Vital Point Radius"),
    SteadfastRate       UMETA(DisplayName = "Steadfast Rate"),
    ReloadSpeed         UMETA(DisplayName = "Reload Speed"),
};

// ═══════════════════════════════════════════════════════════════════════════
// Stat Roll — one rolled secondary stat on an item
// ═══════════════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FGothicStatRoll
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    EGothicSecondaryStat StatType = EGothicSecondaryStat::MovementSpeed;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    float Value = 0.f;
};

/** Defines possible secondary stat ranges for loot rolling. */
USTRUCT(BlueprintType)
struct FGothicSecondaryStatRange
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
    EGothicSecondaryStat StatType = EGothicSecondaryStat::MovementSpeed;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
    float MinValue = 0.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
    float MaxValue = 10.f;
};

// ═══════════════════════════════════════════════════════════════════════════
// Item Instance — a specific copy of an item with rolled stats
// ═══════════════════════════════════════════════════════════════════════════

class UGothicItemDefinition;

USTRUCT(BlueprintType)
struct FGothicItemInstance
{
    GENERATED_BODY()

    /** Unique ID for this specific copy. */
    UPROPERTY(BlueprintReadOnly, Category = "Item")
    FGuid InstanceID;

    /** The item definition this instance is based on. */
    UPROPERTY(BlueprintReadOnly, Category = "Item")
    TObjectPtr<UGothicItemDefinition> Definition;

    /** Star ceiling — set at drop, determines max imbue level. */
    UPROPERTY(BlueprintReadOnly, Category = "Item")
    int32 StarCeiling = 1;

    /** Current stars — raised by Selah imbuing at the Binder. */
    UPROPERTY(BlueprintReadOnly, Category = "Item")
    int32 CurrentStars = 0;

    /** Rolled primary stat value. */
    UPROPERTY(BlueprintReadOnly, Category = "Item")
    float PrimaryStatValue = 0.f;

    /** Rolled secondary stats. */
    UPROPERTY(BlueprintReadOnly, Category = "Item")
    TArray<FGothicStatRoll> SecondaryStats;

    /** Current Resonance Strain cost. Rises over content cycles for Resonant/Pure. */
    UPROPERTY(BlueprintReadOnly, Category = "Item")
    float StrainCost = 0.f;

    /** True once any Selah has been imbued — locks trading. */
    UPROPERTY(BlueprintReadOnly, Category = "Item")
    bool bImbued = false;

    /** Gear Power — baked into tier, not rollable. */
    UPROPERTY(BlueprintReadOnly, Category = "Item")
    int32 GearPower = 0;

    bool IsValid() const { return Definition != nullptr && InstanceID.IsValid(); }
};