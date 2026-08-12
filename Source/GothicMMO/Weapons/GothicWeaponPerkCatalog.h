// GothicWeaponPerkCatalog.h
// The rolled-weapon-perk catalog: which perks exist, which bucket each sits in,
// and what capability a weapon must have to be allowed to roll it.
//
// Spec: docs/WEAPON_PERK_TABLES.md. One DataAsset (DA_WeaponPerkCatalog) holds
// every entry; each UGothicWeaponData points at it. Adding a perk is a data edit
// plus one native tag — the roll logic never enumerates perks by name.
//
// Create in editor: Right-click -> Miscellaneous -> Data Asset -> GothicWeaponPerkCatalog
//
// The three exclusion layers live in three different places on purpose:
//   1. Engine-level capability  — the bRequires* flags here, checked against
//                                 UGothicWeaponData's bCanScoreVitalHits/bUsesAmmo.
//                                 New weapons inherit the right exclusions free.
//   2. Numeric threshold        — MinMagazineCapacity here (Marksman's Due needs > 3).
//   3. Per-weapon curation      — UGothicWeaponData::ExcludedPerks, hand-authored
//                                 per archetype from the doc's curation table.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GothicWeaponPerkCatalog.generated.h"

class UGothicWeaponData;

/**
 * The three roll buckets. A Resonant or Pure weapon rolls exactly one perk from
 * each — never two from the same bucket, which is what stops a weapon landing
 * two inert Fine-Tune numbers or both halves of an opposite pair.
 */
UENUM(BlueprintType)
enum class EGothicPerkBucket : uint8
{
    FineTune UMETA(DisplayName = "Fine-Tune"),
    VerbA    UMETA(DisplayName = "Verb A - In-Fight Behaviour"),
    VerbB    UMETA(DisplayName = "Verb B - Economy & Utility"),
};

/**
 * One perk in the catalog.
 *
 * Magnitude is authored but read by nothing yet — the two pilot effects in this
 * PR use their doc-quoted constants directly so the seam can be proven without
 * the asset existing. Part 2 moves every effect onto this field.
 */
USTRUCT(BlueprintType)
struct FGothicWeaponPerkEntry
{
    GENERATED_BODY()

    /** The Perk.Weapon.<Bucket>.<Name> tag written onto the item instance. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perk")
    FGameplayTag PerkTag;

    /** Which bucket this perk rolls from. Must agree with the tag's parent. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perk")
    EGothicPerkBucket Bucket = EGothicPerkBucket::FineTune;

    /** Name shown on the item tooltip. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perk")
    FText DisplayName;

    /** Effect text shown on the item tooltip. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perk", meta = (MultiLine = true))
    FText Description;

    /**
     * The perk's numeric coefficient, in whatever unit its effect reads.
     * Deliberately untyped and unused in part 1 — WEAPON_PERK_TABLES.md defers
     * every coefficient pending playtesting, so authoring these is part of the
     * part-2 effects pass, not this one.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perk")
    float Magnitude = 0.f;

    // ── Exclusion layer 1: engine-level capability ───────────────────────────

    /**
     * Excluded on weapons that cannot register a vital hit at all
     * (UGothicWeaponData::bCanScoreVitalHits false — currently only the Bomb
     * Thrower / Censer Launcher: "an explosion does not find a vital point").
     * Set on Steady Read, Moving Target, Marksman's Due.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perk|Exclusions")
    bool bRequiresVitalHits = false;

    /**
     * Excluded on weapons with no ammo or Steadfast interaction
     * (UGothicWeaponData::bUsesAmmo false — currently only Heavy Melee).
     * Set on Deep Reserves, Extended Magazine, Well-Tended, Charitable Toll,
     * Frugal Hand, Overcharge — and on Marksman's Due, which the doc flags as
     * non-functional there for a different reason (no magazine to refund into).
     * Spent Well is deliberately NOT set: the Steadfast meter is global.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perk|Exclusions")
    bool bRequiresAmmo = false;

    // ── Exclusion layer 2: numeric threshold ─────────────────────────────────

    /**
     * Minimum MagazineCapacity the weapon must have. 0 disables the check.
     * Marksman's Due authors 4 here — the doc's "excluded on MagazineCapacity
     * <= 3", drawn to keep Bolt-Action (5) and Revolver (6) eligible.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perk|Exclusions", meta = (ClampMin = 0))
    int32 MinMagazineCapacity = 0;
};

UCLASS(BlueprintType)
class GOTHICMMO_API UGothicWeaponPerkCatalog : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** Every perk that exists. 18 entries as of WEAPON_PERK_TABLES.md. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perks")
    TArray<FGothicWeaponPerkEntry> Perks;

    /**
     * The perks in Bucket that WeaponData is allowed to roll, with all three
     * exclusion layers applied. Returns tags rather than entries because that is
     * what the roll writes and what the instance stores.
     *
     * A null WeaponData yields an empty pool rather than the whole bucket — a
     * weaponless definition must never roll weapon perks.
     */
    UFUNCTION(BlueprintCallable, Category = "Perks")
    TArray<FGameplayTag> GetEligiblePerks(EGothicPerkBucket Bucket, const UGothicWeaponData* WeaponData) const;

    /**
     * The catalog entry for a rolled perk tag, or null if this catalog holds no
     * such perk. This is the one lookup the inventory UI resolves a rolled
     * Perk.Weapon.<Bucket>.<Name> tag through to reach its display name and
     * description. Not a UFUNCTION — it returns a pointer into Perks, which the
     * UI copies out of immediately; an unknown tag returns null and the caller
     * falls back to the tag's leaf name rather than dropping the perk.
     */
    const FGothicWeaponPerkEntry* FindPerkEntry(const FGameplayTag& PerkTag) const;
};
