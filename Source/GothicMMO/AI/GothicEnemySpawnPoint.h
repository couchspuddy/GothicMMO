// New file: GothicEnemySpawnPoint.h
// A placeable marker for where a dynamically-spawned enemy should appear and
// what class it should be. General-purpose — not specific to any encounter.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GothicEnemySpawnPoint.generated.h"

class AGothicEnemyBase;

UCLASS()
class GOTHICMMO_API AGothicEnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AGothicEnemySpawnPoint();

	UPROPERTY(EditInstanceOnly, Category = "Gothic|Spawner")
	TSubclassOf<AGothicEnemyBase> EnemyClass;

	/**
	 * Optional pack stamp for the spawned enemy. NAME_None = packless.
	 * This is how wave enemies join packs — they can't be hand-edited in
	 * the level, so the spawn point carries the grouping. Give every point
	 * in one wave the same PackID and the wave regroups as one pack,
	 * independent of other waves through the same encounter volume.
	 */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Spawner")
	FName PackID = NAME_None;
};