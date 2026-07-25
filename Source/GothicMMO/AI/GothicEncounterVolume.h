// GothicEncounterVolume.h
// Placed per-encounter in the level. Holds hand-picked references to that
// encounter's enemies (matching the deliberate placement described in the
// Eagle's Landing Encounters doc — not auto-discovered). Tracks deaths,
// and once all enemies are down, activates a shared Selah prompt on the
// last enemy to die for the whole party to see and trigger.
//
// Wave sequencing (the Feral Retained Part 2 "interrupted Selah"): the initial
// roster clears -> prompt -> the first collection does NOT reward. It waits
// InterruptDelay ("~50% into the collect") then spawns the interrupt wave
// (PendingWaveSpawnPoints). When that wave falls, Wave3SpawnPoints spawns
// automatically (no prompt) — the "one more after". Only when Wave 3 falls does
// the prompt return and a collection actually reward. Encounters that set none
// of the wave fields reward on the first collect exactly as before.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/GothicEnemyBase.h"
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

	/** Living member count — the exact number RemainingEnemyCount already tracks,
 *  exposed for anything that needs to read pack strength without owning its
 *  own duplicate roster (the planned UGothicPackCoordinator, and the Bestial
 *  Lucid's opening-aggression bias reading her own Den's losses). */
	UFUNCTION(BlueprintPure, Category = "Gothic|Encounter")
	int32 GetLivingCount() const { return RemainingEnemyCount; }

	/** Fires whenever a member of this encounter dies — same event
	 *  HandleEnemyDied already reacts to internally, exposed so external
	 *  listeners (pack coordination, boss aggression) don't need to bind to
	 *  every individual enemy's own OnEnemyDied themselves. */
	UPROPERTY(BlueprintAssignable, Category = "Gothic|Encounter")
	FOnEnemyDied OnEncounterMemberDied;

	UFUNCTION(BlueprintCallable, Category = "Gothic|Encounter")
	void AddWaveToEncounter(const TArray<AGothicEnemyBase*>& NewWaveEnemies);

	/**
	 * Server-only. Called via the player's ServerCollectEncounterSelah RPC once
	 * a player triggers the shared prompt. Awards Selah to every player in the
	 * instance, clears the prompt, and updates the checkpoint.
	 */
	void CompleteCollection();

	/** Radius from this volume's location within which a player can meditate to
	 *  collect the Selah — the encounter's area, independent of any corpse. */
	UFUNCTION(BlueprintPure, Category = "Gothic|Encounter")
	float GetMeditationRange() const { return MeditationRange; }

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

	/** The INTERRUPT wave (Wave 2). Spawned InterruptDelay seconds after the
	 *  first collection attempt instead of rewarding — the interrupted Selah. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter|Waves")
	TArray<TObjectPtr<AGothicEnemySpawnPoint>> PendingWaveSpawnPoints;

	/** The FINAL wave (Wave 3), spawned automatically when the interrupt wave
	 *  falls — the "one more after". Leave empty for a single-interrupt encounter. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter|Waves")
	TArray<TObjectPtr<AGothicEnemySpawnPoint>> Wave3SpawnPoints;

	/** Seconds between the first collection attempt and the interrupt wave
	 *  crashing in — the "~50% into the collect" fake-out. 0 = immediate (the
	 *  original single-wave behavior). Should be ~half SelahCollectDuration. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter|Waves")
	float InterruptDelay = 0.f;

	/** Full length of the Selah collection channel — the time the fill-bar takes
	 *  to complete a real (uninterrupted) collection and pay out. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter|Waves")
	float SelahCollectDuration = 5.f;

	/** Radius from this volume's location within which a player can meditate to
	 *  collect — the area-based prompt, not a singular corpse. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter")
	float MeditationRange = 1200.f;

	// ── Run completion — the encounter that ends the excursion ────────────
	//
	// Tick this on the LAST encounter of a level (the boss). The return is
	// deliberately driven from collection rather than from the boss's death:
	// binding to the corpse's OnDestroyed fired either CorpseLifetime seconds
	// after the kill — before the player could reach the body — or the instant
	// FinalizeCollection destroyed the corpses, cutting the name reveal off
	// mid-cycle. Hanging it here means the beat always runs kill → prompt →
	// collect → names → return.

	/** Return every player to the hub once THIS encounter's reward is collected. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter|Completion")
	bool bReturnToHubOnComplete = false;

	/** Level to travel to. Must be in the packaged maps list. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter|Completion",
		meta = (EditCondition = "bReturnToHubOnComplete"))
	FName ReturnHubLevelName = TEXT("L_Hearth");

	/** Seconds from the Selah moment starting to the level change. Keep this
	 *  longer than the name cycle, or the reveal is cut short. */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|Encounter|Completion",
		meta = (EditCondition = "bReturnToHubOnComplete"))
	float ReturnDelaySeconds = 8.f;

private:
	/** Fires ReturnDelaySeconds after the Selah moment begins. */
	void ReturnToHub();

	FTimerHandle ReturnHubHandle;

	int32 RemainingEnemyCount = 0;

	UPROPERTY()
	TObjectPtr<AGothicEnemyBase> LastEnemyToDie;

	UFUNCTION()
	void HandleEnemyDied(AGothicEnemyBase* DeadEnemy);


	// AGothicEncounterVolume.h — add to private section
	float CachedTotalSelah = 0.f;

	UPROPERTY()
	TSubclassOf<UGameplayEffect> CachedGainEffect;

	/**
	 * Wave progression state:
	 *   0 = initial roster active, then awaiting the FIRST collect
	 *   1 = first collect taken, interrupt wave pending (timer running)
	 *   2 = interrupt wave (Wave 2) active
	 *   3 = final wave (Wave 3) active
	 *   4 = awaiting the FINAL collect (rewards)
	 */
	int32 WaveStage = 0;

	FTimerHandle InterruptTimerHandle;
	FTimerHandle CollectFinishHandle;

	/** Spawns one enemy per point, stamps pack IDs, folds them into the roster. */
	TArray<AGothicEnemyBase*> SpawnWaveFromPoints(const TArray<TObjectPtr<AGothicEnemySpawnPoint>>& Points);

	/** Timer callback — springs the interrupt wave (Wave 2). */
	void SpawnInterruptWave();

	/** Timer callback — the collection channel finished uninterrupted: reward. */
	void FinalizeCollection();

	/** Caches Selah totals + names and raises the shared prompt on the last corpse. */
	void ActivateSelahPrompt();

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
