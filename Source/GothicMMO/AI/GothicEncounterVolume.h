// GothicEncounterVolume.h
// Placed per-encounter in the level. Holds hand-picked references to that
// encounter's enemies (matching the deliberate placement described in the
// Eagle's Landing Encounters doc — not auto-discovered). Tracks deaths,
// and once all enemies are down, activates a shared Selah prompt on the
// last enemy to die for the whole party to see and trigger.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GothicEncounterVolume.generated.h"

class AGothicEnemyBase;
class UGameplayEffect;
class AGothicEnemySpawnPoint;

UCLASS()
class GOTHICMMO_API AGothicEncounterVolume : public AActor
{
	GENERATED_BODY()

public:
	AGothicEncounterVolume();

	virtual void BeginPlay() override;

	/** True once every enemy in this encounter is dead. */
	UFUNCTION(BlueprintPure, Category = "Gothic|Encounter")
	bool IsComplete() const { return RemainingEnemyCount <= 0; }
	
	UFUNCTION(BlueprintCallable, Category = "Gothic|Encounter")
	void AddWaveToEncounter(const TArray<AGothicEnemyBase*>& NewWaveEnemies);

	/**
	 * Server-only. Called via the player's ServerCollectEncounterSelah RPC once
	 * a player triggers the shared prompt. Awards Selah to every player in the
	 * instance, clears the prompt, and updates the checkpoint.
	 */
	void CompleteCollection();

protected:
	/** Hand-placed in the level — the specific enemies that make up this encounter. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter")
	TArray<TObjectPtr<AGothicEnemyBase>> EncounterEnemies;
	
	/** Optional second wave — spawn point markers, not live enemies. If set and
	 *  not yet sprung, the player's first collection attempt spawns fresh enemies
	 *  here instead of completing the encounter — Encounter 3's interrupted Selah. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter")
	TArray<TObjectPtr<AGothicEnemySpawnPoint>> PendingWaveSpawnPoints;

private:
	int32 RemainingEnemyCount = 0;

	UPROPERTY()
	TObjectPtr<AGothicEnemyBase> LastEnemyToDie;

	UFUNCTION()
	void HandleEnemyDied(AGothicEnemyBase* DeadEnemy);
	
	// AGothicEncounterVolume.h — add to private section
	float CachedTotalSelah = 0.f;

	UPROPERTY()
	TSubclassOf<UGameplayEffect> CachedGainEffect;
	
	// GothicEncounterVolume.h — add to private section
	bool bPendingWaveTriggered = false;
};