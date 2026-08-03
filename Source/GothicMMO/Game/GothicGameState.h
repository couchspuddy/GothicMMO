// GothicGameState.h
// First custom GameState in the project. Exists specifically for state that
// must be identical and visible on every client at once — the shared Selah
// prompt and the current checkpoint. GameMode is server-only and PlayerState
// is per-player; neither fits "everyone sees the same thing."

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GothicGameState.generated.h"

class AGothicEnemyBase;
class AGothicEncounterVolume;
class APlayerController;
class UGothicSelahCollectBarWidget;

UCLASS()
class GOTHICMMO_API AGothicGameState : public AGameStateBase
{
	GENERATED_BODY()

public:


	/** World location of the most recently completed encounter — the current checkpoint. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gothic|Checkpoint")
	FVector CheckpointLocation = FVector::ZeroVector;

	/**
	 * Server-side. Records a checkpoint at FLOOR level under WorldLocation.
	 *
	 * The floor projection is the whole point. Encounter volumes recorded their
	 * own GetActorLocation(), which is a trigger box's CENTRE — EV4's sits 340 uu
	 * above its floor and the boss volume's 740 — so "respawn at the checkpoint"
	 * dropped the player out of the sky. IgnoreActor keeps the trace off the
	 * volume doing the recording.
	 *
	 * Stores the floor and deliberately NOT a lift: every reader
	 * (AGothicGameMode::RespawnPlayer, AGothicPlayerCharacter::TriggerFallRespawn)
	 * already raises the capsule clear of it, and lifting at both ends drops the
	 * player twice as far as either intended.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gothic|Checkpoint")
	void SetCheckpointFromLocation(const FVector& WorldLocation, AActor* IgnoreActor = nullptr);

	/** Fired on every client when a shared prompt becomes available. Blueprint hooks vignette/audio/UI here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
	void OnEncounterPromptActivated(AGothicEnemyBase* PromptCorpse);

	/** Fired on every client when the active prompt is collected (or otherwise cleared). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
	void OnEncounterPromptCollected();

	/**
	 * Fired on every client when the reward lands and the Selah moment begins —
	 * the name reveal belongs HERE, not on OnEncounterPromptActivated.
	 *
	 * That mistake is why the Accursed names used to cycle the instant the last
	 * enemy fell and then vanish the moment you pressed interact: the prompt
	 * event means "collection is available", not "you have collected". SelahNames
	 * is still populated when this fires (FinalizeCollection deliberately no
	 * longer clears it) so Blueprint can drive the cycle from it.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
	void OnSelahMomentStarted();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Server-side. Adds an ENCOUNTER (the persistent area) to the set offering a
	 * Selah "meditation" prompt — corpses despawn and would drop the prompt with
	 * them. The corpse is optional data for the name reveal.
	 *
	 * This is a SET, not a single slot. It used to be one pointer, and with
	 * overlapping encounter volumes the most recent completion silently evicted
	 * the previous one: clearing a second encounter stole the first's prompt, so
	 * collecting near the first ran CompleteCollection against the wrong volume —
	 * which is how the Feral Retained's interrupt wave went missing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
	void AddEncounterPrompt(AGothicEncounterVolume* Encounter, AGothicEnemyBase* PromptCorpse);

	/** Removes one encounter's meditation prompt. Others stay up. */
	UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
	void ClearEncounterPrompt(AGothicEncounterVolume* Encounter);

	/** Drops every pending prompt. */
	UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
	void ClearAllEncounterPrompts();

	/**
	 * The nearest pending encounter whose own MeditationRange covers WorldLocation,
	 * or null. Range is per-encounter, so a big arena and a small ambush can sit
	 * side by side without one swallowing the other.
	 */
	UFUNCTION(BlueprintPure, Category = "Gothic|Selah")
	AGothicEncounterVolume* GetPromptEncounterFor(const FVector& WorldLocation) const;

	/** True if any encounter is awaiting collection. */
	UFUNCTION(BlueprintPure, Category = "Gothic|Selah")
	bool HasPendingPrompt() const { return PendingPromptEncounters.Num() > 0; }

	/** True if this specific encounter is awaiting collection. Replaces the old
	 *  "am I the single active prompt?" identity check. */
	UFUNCTION(BlueprintPure, Category = "Gothic|Selah")
	bool IsPromptPending(AGothicEncounterVolume* Encounter) const
	{
		return Encounter && PendingPromptEncounters.Contains(Encounter);
	}

	/** Optional corpse tied to the prompt, for the name reveal. May be null/despawned. */
	UFUNCTION(BlueprintPure, Category = "Gothic|Selah")
	AGothicEnemyBase* GetActivePromptCorpse() const { return ActivePromptCorpse; }

	/**
	 * Names of the Accursed killed in the current encounter. Set server-side
	 * when an encounter completes, cleared on collection. Blueprint reads this
	 * to cycle names during the Selah moment.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gothic|Selah")
	TArray<FText> SelahNames;

	/** Server-side setter — collects names, caps to MaxSelahNames, replicates. */
	void SetSelahNames(const TArray<FText>& Names);

	/** Maximum names displayed during a single Selah moment. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
	int32 MaxSelahNames = 6;

	/**
	 * Server-only. Drives the shared collection fill-bar. Phase: 0 = none,
	 * 1 = collecting (fill over Duration), 2 = interrupted (break), 3 = completed
	 * (snap full). Replicates so every client's bar matches, and fires locally on
	 * the authority too so standalone PIE sees it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
	void SetSelahCollectPhase(uint8 NewPhase, float Duration);

protected:
	/** Every encounter currently awaiting collection — the persistent anchors that
	 *  drive OnEncounterPromptActivated/Collected. Empty = no prompt anywhere. */
	UPROPERTY(ReplicatedUsing = OnRep_ActivePrompt, BlueprintReadOnly, Category = "Gothic|Selah")
	TArray<TObjectPtr<AGothicEncounterVolume>> PendingPromptEncounters;

	/** Corpse from the most recent activation, passed to OnEncounterPromptActivated
	 *  for the name reveal. May despawn while prompts stay up. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gothic|Selah")
	TObjectPtr<AGothicEnemyBase> ActivePromptCorpse;

	/** Prompt count at the last OnRep, so the handler can tell an add from a removal
	 *  without replicating a separate flag. */
	int32 LastBroadcastPromptCount = 0;

	UFUNCTION()
	void OnRep_ActivePrompt();

	// ── Selah collection fill-bar ────────────────────────────────────────
	UPROPERTY(ReplicatedUsing = OnRep_SelahCollect, BlueprintReadOnly, Category = "Gothic|Selah")
	uint8 SelahCollectPhase = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gothic|Selah")
	float SelahCollectDuration = 0.f;

	UFUNCTION()
	void OnRep_SelahCollect();

	/** Drives one local player's bar to the current phase, creating it on demand. */
	void ApplyCollectPhaseToLocalPlayer(APlayerController* PC);

	/** The collect-bar widget class. Assign WBP_SelahCollectBar in BP_GothicGameState. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
	TSubclassOf<UGothicSelahCollectBarWidget> CollectBarWidgetClass;

	/** One bar per LOCAL player, not one bar per machine. Split-screen and the
	 *  listen-server host both need their own; each is identified by its own
	 *  GetOwningPlayer() so the GameState holds no controller pointers. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UGothicSelahCollectBarWidget>> CollectBarWidgets;
};