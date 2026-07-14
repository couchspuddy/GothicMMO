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
};