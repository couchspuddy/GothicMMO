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
class AController;
class APlayerController;
class UGothicSelahCollectBarWidget;

/**
 * How the party as a whole is doing. Replicated, because every client needs the
 * same answer at the same moment — a wipe vignette that only the host sees is
 * exactly the class of bug this GameState exists to prevent.
 *
 * The ladder is deliberately coarse. "Fightable" is AGothicCharacterBase::
 * IsFightableActor — alive and not downed — the SAME predicate the enemy AI and
 * the downed fork already use, so the party readout can never disagree with the
 * AI about who is still up.
 */
UENUM(BlueprintType)
enum class EGothicPartyState : uint8
{
	/** Everyone is up. Also the state of an EMPTY roster: a session with no
	 *  players in it yet has not wiped, and must not be treated as if it had. */
	Alive     UMETA(DisplayName = "Party Alive"),

	/** At least one member is fightable and at least one is not — somebody is on
	 *  the floor with a revive window running and somebody is left to work it. */
	InPeril   UMETA(DisplayName = "Party In Peril"),

	/** Members exist and NONE of them is fightable. The wipe condition. */
	Wiped     UMETA(DisplayName = "Party Wiped")
};

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

	// ── Party state (PR-5) ───────────────────────────────────────────────────
	//
	// The census lives HERE rather than on the GameMode for two reasons. The
	// GameMode is server-only, and AGothicPlayerCharacter::HasLivingPartyMember
	// is BlueprintPure and reachable from a client; and PlayerArray — the roster
	// this walks — is a GameState member, so putting the walk anywhere else means
	// reaching across for it. The GameMode owns the DECISION (evaluate, wipe);
	// this class owns the FACTS.

	/**
	 * Read-only party readout. Written only by SetPartyState on the authority.
	 * Blueprint hangs the wipe vignette / "you have fallen" screen off
	 * OnPartyStateChanged rather than polling this.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_PartyState, BlueprintReadOnly, Category = "Gothic|Party")
	EGothicPartyState PartyState = EGothicPartyState::Alive;

	/**
	 * Server-side setter. Copies SetSelahCollectPhase exactly — the house idiom
	 * on this class: guard on authority, write, ForceNetUpdate, then call the
	 * OnRep DIRECTLY, because the authority never receives its own and a
	 * listen-server host would otherwise be the one machine in the session that
	 * never saw the state it is driving.
	 *
	 * Idempotent: re-setting the current state does nothing at all, which is what
	 * lets AGothicGameMode::EvaluatePartyState be called from every transition
	 * without the wipe firing more than once.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gothic|Party")
	void SetPartyState(EGothicPartyState NewState);

	UFUNCTION(BlueprintPure, Category = "Gothic|Party")
	EGothicPartyState GetPartyState() const { return PartyState; }

	/** Fired on every machine (including the authority) when the party state
	 *  changes. The wipe screen, the audio sting and the vignette belong here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Party")
	void OnPartyStateChanged(EGothicPartyState NewState);

	/**
	 * Walks the roster and returns what the party state OUGHT to be right now.
	 * Pure — it writes nothing; AGothicGameMode::EvaluatePartyState is what turns
	 * the answer into a stored state and a wipe.
	 *
	 * IgnoreController exists for Logout, and it is the whole reason this takes a
	 * parameter. AGameModeBase::Logout runs BEFORE the exiting controller's
	 * PlayerState leaves PlayerArray (the removal happens in AController::
	 * Destroyed → CleanupPlayerState, later), so a census taken during Logout
	 * still counts the leaver as present — and the disconnect of the last living
	 * player, which is precisely the edge this PR was written for, would report
	 * the party as fine.
	 */
	UFUNCTION(BlueprintPure, Category = "Gothic|Party")
	EGothicPartyState ComputePartyState(const AController* IgnoreController = nullptr) const;

	/**
	 * True if anyone on the roster other than ExcludePS is fightable.
	 *
	 * This is the single census AGothicPlayerCharacter::HasLivingPartyMember now
	 * delegates to (PR-2 left that function as the named seam for exactly this).
	 * One walk, one definition of "still in the fight" — the downed fork and the
	 * wipe detector cannot drift apart because there is only one of them.
	 *
	 * SEAM FOR REAL PARTIES: when membership stops being "everyone in the world",
	 * this function and ComputePartyState above are the two places that change.
	 */
	UFUNCTION(BlueprintPure, Category = "Gothic|Party")
	bool HasFightablePartyMember(const APlayerState* ExcludePS = nullptr,
	                             const AController* IgnoreController = nullptr) const;

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

	UFUNCTION()
	void OnRep_PartyState();

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