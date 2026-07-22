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

/** Equipment slot types. */
UENUM(BlueprintType)
enum class EGothicEquipSlot : uint8
{
    // Weapon slots (indices 0-2 map to WeaponSlots array)
    Sidearm         UMETA(DisplayName = "Sidearm"),
    Piece           UMETA(DisplayName = "Piece"),
    Rig             UMETA(DisplayName = "Rig"),

    // Armor slots (ten, locked per design doc)
    Head            UMETA(DisplayName = "Head"),
    Neck            UMETA(DisplayName = "Neck"),
    Chest           UMETA(DisplayName = "Chest"),
    Back            UMETA(DisplayName = "Back"),
    LeftArm         UMETA(DisplayName = "Left Arm"),
    RightArm        UMETA(DisplayName = "Right Arm"),
    Wrist           UMETA(DisplayName = "Wrist"),
    LeftLeg         UMETA(DisplayName = "Left Leg"),
    RightLeg        UMETA(DisplayName = "Right Leg"),
    Feet            UMETA(DisplayName = "Feet"),
};

/** Primary stats — creed-mapped, from PROGRESSION_STATS_AND_BALANCE.md. */
UENUM(BlueprintType)
enum class EGothicPrimaryStat : uint8
{
    Resolve     UMETA(DisplayName = "Resolve"),      // Endure — mitigation, health
    Clarity     UMETA(DisplayName = "Clarity"),       // Remember — crit, cooldowns
    Conviction  UMETA(DisplayName = "Conviction"),    // Repay — Steadfast, resource gen
};

/** Secondary stats — flat values rolled on gear. */
UENUM(BlueprintType)
enum class EGothicSecondaryStat : uint8
{
    FlatDamage          UMETA(DisplayName = "Flat Damage"),
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
    EGothicSecondaryStat StatType = EGothicSecondaryStat::FlatDamage;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    float Value = 0.f;
};

/** Defines possible secondary stat ranges for loot rolling. */
USTRUCT(BlueprintType)
struct FGothicSecondaryStatRange
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
    EGothicSecondaryStat StatType = EGothicSecondaryStat::FlatDamage;

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