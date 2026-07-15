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
class UBoxComponent;
class AGothicPlayerCharacter;

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
	
	/**
	 * Aggro trigger. Sized per instance in the level — the default extent is a
	 * placeholder, not a suggestion. Root component, so moving the actor moves it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Encounter")
	TObjectPtr<UBoxComponent> TriggerBox;

	/**
	 * Opt-in. When true, the first player to enter TriggerBox sets every living
	 * enemy in EncounterEnemies onto them.
	 *
	 * Defaults false deliberately: five volumes are already placed in Eagle's
	 * Landing and none of them have a sized box yet. Enabling this by default
	 * would have Encounter 2's second-floor group aggroing through the floor.
	 * Size the box first, then turn this on, one encounter at a time.
	 */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter")
	bool bAggroEnemiesOnOverlap = false;
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
	/** Aggro fires once per encounter, not once per player. */
	bool bAggroTriggered = false;

	UFUNCTION()
	void HandleTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);
};