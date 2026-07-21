// GothicItemDefinition.cpp

#include "Items/GothicItemDefinition.h"

FGothicItemInstance UGothicItemDefinition::RollInstance() const
{
	FGothicItemInstance Instance;
	Instance.InstanceID = FGuid::NewGuid();
	Instance.Definition = const_cast<UGothicItemDefinition*>(this);
	Instance.GearPower = GetGearPower();
	Instance.StrainCost = BaseStrainCost;
	Instance.bImbued = false;
	Instance.CurrentStars = 0;

	// Roll star ceiling within range
	Instance.StarCeiling = FMath::RandRange(MinStarCeiling, MaxStarCeiling);

	// Roll primary stat
	Instance.PrimaryStatValue = FMath::FRandRange(PrimaryStatRange.X, PrimaryStatRange.Y);

	// Roll secondaries — pick random stats from the pool without repeats
	if (SecondaryStatSlots > 0 && SecondaryStatPool.Num() > 0)
	{
		TArray<int32> AvailableIndices;
		for (int32 i = 0; i < SecondaryStatPool.Num(); ++i)
		{
			AvailableIndices.Add(i);
		}

		const int32 NumToRoll = FMath::Min(SecondaryStatSlots, SecondaryStatPool.Num());
		for (int32 i = 0; i < NumToRoll; ++i)
		{
			const int32 PickIdx = FMath::RandRange(0, AvailableIndices.Num() - 1);
			const int32 PoolIdx = AvailableIndices[PickIdx];
			AvailableIndices.RemoveAtSwap(PickIdx);

			const FGothicSecondaryStatRange& Range = SecondaryStatPool[PoolIdx];

			FGothicStatRoll Roll;
			Roll.StatType = Range.StatType;
			Roll.Value = FMath::FRandRange(Range.MinValue, Range.MaxValue);
			Instance.SecondaryStats.Add(Roll);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RollInstance: %s | Rarity=%d | Tier=%d | Stars=%d/%d | Primary=%.1f | Secondaries=%d"),
		*ItemID.ToString(),
		(int32)Rarity,
		GearTier,
		Instance.CurrentStars,
		Instance.StarCeiling,
		Instance.PrimaryStatValue,
		Instance.SecondaryStats.Num());

	return Instance;
}