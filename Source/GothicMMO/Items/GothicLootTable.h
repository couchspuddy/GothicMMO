// GothicLootTable.h
// Data asset defining a loot pool for an enemy type.
// Each enemy Blueprint references one of these.
// On death, RollDrop() picks a weighted random item and rolls its stats.
//
// Enemy tier → eligible rarities (from ECONOMY_SYSTEM.md):
//   Thrall    → Salvage, Kept
//   Retained  → Kept, Remembered
//   Lucid     → Remembered, Resonant (never Pure — Pure comes from Pilgrimage only)

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Items/GothicItemTypes.h"
#include "GothicLootTable.generated.h"

class UGothicItemDefinition;

/** One weighted entry in a loot table. */
USTRUCT(BlueprintType)
struct FGothicLootTableEntry
{
	GENERATED_BODY()

	/** The item that can drop. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UGothicItemDefinition> ItemDefinition;

	/** Relative weight. Higher = more likely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = 1))
	int32 Weight = 100;
};

UCLASS(BlueprintType)
class GOTHICMMO_API UGothicLootTable : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** All possible drops from this table. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	TArray<FGothicLootTableEntry> Entries;

	/** Chance (0-1) that anything drops at all. 1.0 = always drops. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float DropChance = 0.5f;

	/**
	 * Roll a drop from this table.
	 * Returns true if an item dropped; OutInstance is the rolled item.
	 * Returns false if nothing dropped (failed DropChance roll).
	 */
	bool RollDrop(FGothicItemInstance& OutInstance) const;
};