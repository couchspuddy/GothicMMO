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
	 * How many enemies this one point produces. Encounter length is roughly
	 * (total wave HP / player DPS), so this is the tuning dial for duration —
	 * raise the count, not the placement. One enemy per point meant a 45-Thrall
	 * plaza needed 45 hand-placed actors, and every retune moved them.
	 *
	 * Counts above 1 need a ScatterRadius, or they stack on the same spot and
	 * the collision handler shoves them apart in whatever direction it likes.
	 */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Spawner", meta = (ClampMin = "1"))
	int32 SpawnCount = 1;

	/**
	 * Radius (cm) of the disc around this point that SpawnCount enemies are
	 * scattered across. Each offset is projected onto the navmesh before use, so
	 * a radius that overhangs a ledge or a wall pulls back to walkable ground
	 * rather than dropping a Thrall inside geometry. 0 = spawn exactly on the point.
	 */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Spawner", meta = (ClampMin = "0.0"))
	float ScatterRadius = 0.f;

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