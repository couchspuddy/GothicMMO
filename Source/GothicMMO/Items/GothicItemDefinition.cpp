// GothicItemDefinition.cpp

#include "Items/GothicItemDefinition.h"
#include "Game/GothicDeterminism.h"
#include "Weapons/GothicWeaponData.h"
#include "Weapons/GothicWeaponPerkCatalog.h"

// Every roll below goes through FGothicDeterminism rather than FMath directly.
// Off (the default) it IS FMath. On, the whole starting kit — 10 pieces, 2
// secondaries each out of a pool where 11 entries are Damage_* percentages —
// rolls the same way every session, which is the only way a damage measurement
// taken on Tuesday means anything on Wednesday.

FGothicItemInstance UGothicItemDefinition::RollInstance(bool bCanonical) const
{
	FGothicItemInstance Instance;
	Instance.InstanceID = FGuid::NewGuid();
	Instance.Definition = const_cast<UGothicItemDefinition*>(this);
	Instance.GearPower = GetGearPower();
	Instance.StrainCost = BaseStrainCost;
	Instance.bImbued = false;
	Instance.CurrentStars = 0;

	// The canonical bench path takes each range's midpoint instead of a die roll
	// and never touches FGothicDeterminism — see the header. The midpoint (not
	// the floor) is chosen so the pinned loadout sits at a representative centre
	// of every range rather than a degenerate minimum. InstanceID stays a fresh
	// GUID: it is a per-instance handle, not a stat, and nothing measurement-facing
	// reads it.
	auto Pick = [bCanonical](float Min, float Max) -> float
	{
		return bCanonical ? (Min + Max) * 0.5f : FGothicDeterminism::FRandRange(Min, Max);
	};

	// Roll star ceiling within range (canonical: the floor — stars are inert, so
	// the choice is cosmetic; the floor keeps it stable and legible).
	Instance.StarCeiling = bCanonical
		? MinStarCeiling
		: FGothicDeterminism::RandRange(MinStarCeiling, MaxStarCeiling);

	// Roll primary stat
	Instance.PrimaryStatValue = Pick(PrimaryStatRange.X, PrimaryStatRange.Y);

	// Roll secondaries — random subset without repeats, OR (canonical) the first
	// SecondaryStatSlots entries of the pool in author order, at their midpoints.
	if (SecondaryStatSlots > 0 && SecondaryStatPool.Num() > 0)
	{
		const int32 NumToRoll = FMath::Min(SecondaryStatSlots, SecondaryStatPool.Num());

		if (bCanonical)
		{
			for (int32 i = 0; i < NumToRoll; ++i)
			{
				const FGothicSecondaryStatRange& Range = SecondaryStatPool[i];

				FGothicStatRoll Roll;
				Roll.StatType = Range.StatType;
				Roll.Value = Pick(Range.MinValue, Range.MaxValue);
				Instance.SecondaryStats.Add(Roll);
			}
		}
		else
		{
			TArray<int32> AvailableIndices;
			for (int32 i = 0; i < SecondaryStatPool.Num(); ++i)
			{
				AvailableIndices.Add(i);
			}

			for (int32 i = 0; i < NumToRoll; ++i)
			{
				const int32 PickIdx = FGothicDeterminism::RandRange(0, AvailableIndices.Num() - 1);
				const int32 PoolIdx = AvailableIndices[PickIdx];
				AvailableIndices.RemoveAtSwap(PickIdx);

				const FGothicSecondaryStatRange& Range = SecondaryStatPool[PoolIdx];

				FGothicStatRoll Roll;
				Roll.StatType = Range.StatType;
				Roll.Value = FGothicDeterminism::FRandRange(Range.MinValue, Range.MaxValue);
				Instance.SecondaryStats.Add(Roll);
			}
		}
	}

	// Perks roll AT DROP, alongside the secondaries and through the same seeded
	// stream — a measurement run that pins the seed gets the same three perks on
	// the same weapon every session, exactly as it gets the same stat rolls. The
	// canonical path picks the first eligible perk in each bucket instead.
	RollWeaponPerks(Instance, bCanonical);

	return Instance;
}

void UGothicItemDefinition::RollWeaponPerks(FGothicItemInstance& Instance, bool bCanonical) const
{
	// Weapons only, Resonant/Pure only. Salvage/Kept/Remembered roll zero perks
	// per the rarity table — unchanged from ITEMIZATION_AND_LOOT.md.
	if (!RollsWeaponPerks())
	{
		return;
	}

	const UGothicWeaponPerkCatalog* Catalog = WeaponData->PerkCatalog;
	if (!Catalog)
	{
		// No catalog authored yet. Silent no-op rather than a warning: until
		// DA_WeaponPerkCatalog exists this is the state of every weapon in the
		// project, and a per-drop log line would drown the loot channel.
		return;
	}

	// Exactly one per bucket, never two from the same one. Rolling bucket by
	// bucket is what enforces that structurally — there is no pass that could
	// pick two Fine-Tunes, or land Frugal Hand and Overcharge on one gun.
	static const EGothicPerkBucket Buckets[] = {
		EGothicPerkBucket::FineTune,
		EGothicPerkBucket::VerbA,
		EGothicPerkBucket::VerbB,
	};

	for (const EGothicPerkBucket Bucket : Buckets)
	{
		// GetEligiblePerks applies all three exclusion layers: the capability
		// flags, the magazine threshold, and the weapon's curated list.
		const TArray<FGameplayTag> Pool = Catalog->GetEligiblePerks(Bucket, WeaponData);
		if (Pool.Num() == 0)
		{
			continue;
		}

		const int32 PickIdx = bCanonical ? 0 : FGothicDeterminism::RandRange(0, Pool.Num() - 1);
		Instance.WeaponPerks.Add(Pool[PickIdx]);
	}

	// Pure additionally carries a fixed class-conditional trait on top of these
	// three. That trait is BENCHED as its own design space (WEAPON_PERK_TABLES.md,
	// "Pure weapon trait design is explicitly benched") — deliberately not
	// implemented here, and not a gap to fill in by extrapolation.
}