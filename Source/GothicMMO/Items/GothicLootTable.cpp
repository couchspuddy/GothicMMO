// GothicLootTable.cpp

#include "Items/GothicLootTable.h"
#include "Items/GothicItemDefinition.h"

bool UGothicLootTable::RollDrop(FGothicItemInstance& OutInstance) const
{
	// Check drop chance first
	if (FMath::FRand() > DropChance)
	{
		return false;
	}

	if (Entries.Num() == 0)
	{
		return false;
	}

	// Calculate total weight
	int32 TotalWeight = 0;
	for (const FGothicLootTableEntry& Entry : Entries)
	{
		if (Entry.ItemDefinition)
		{
			TotalWeight += Entry.Weight;
		}
	}

	if (TotalWeight <= 0)
	{
		return false;
	}

	// Weighted random selection
	int32 Roll = FMath::RandRange(0, TotalWeight - 1);
	for (const FGothicLootTableEntry& Entry : Entries)
	{
		if (!Entry.ItemDefinition)
		{
			continue;
		}

		Roll -= Entry.Weight;
		if (Roll < 0)
		{
			OutInstance = Entry.ItemDefinition->RollInstance();
			UE_LOG(LogTemp, Log, TEXT("LootTable: Dropped %s (Rarity %d)"),
				*Entry.ItemDefinition->ItemID.ToString(),
				(int32)Entry.ItemDefinition->Rarity);
			return true;
		}
	}

	return false;
}