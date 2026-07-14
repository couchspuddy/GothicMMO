// GothicEnemySpawnPoint.cpp
#include "AI/GothicEnemySpawnPoint.h"

AGothicEnemySpawnPoint::AGothicEnemySpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // pure level-authoring marker — read once at spawn time
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}