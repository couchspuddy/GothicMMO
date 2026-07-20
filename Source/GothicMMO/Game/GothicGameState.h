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

UCLASS()
class GOTHICMMO_API AGothicGameState : public AGameStateBase
{
	GENERATED_BODY()

public:


	/** World location of the most recently completed encounter — the current checkpoint. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gothic|Checkpoint")
	FVector CheckpointLocation = FVector::ZeroVector;

	/** Fired on every client when a shared prompt becomes available. Blueprint hooks vignette/audio/UI here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
	void OnEncounterPromptActivated(AGothicEnemyBase* PromptCorpse);

	/** Fired on every client when the active prompt is collected (or otherwise cleared). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
	void OnEncounterPromptCollected();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	/**
 * Server-side setter for the shared prompt. ALWAYS use this — never assign
 * ActivePromptCorpse directly.
 *
 * OnRep fires only on clients receiving replicated data. It never fires on the
 * authority that authored the change, and never at all in standalone PIE where
 * no replication occurs. Direct assignment therefore fires the prompt events on
 * remote clients only — and on nobody in standalone. This calls OnRep by hand so
 * the authority sees its own change like everyone else.
 */
	UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
	void SetActivePromptCorpse(AGothicEnemyBase* NewPromptCorpse);

	/** Read-only access — assignment must go through SetActivePromptCorpse. */
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

protected:
	/** The corpse currently showing an active, uncollected shared Selah prompt. Null = no active prompt. */
	UPROPERTY(ReplicatedUsing = OnRep_ActivePromptCorpse, BlueprintReadOnly, Category = "Gothic|Selah")
	TObjectPtr<AGothicEnemyBase> ActivePromptCorpse;
	
	UFUNCTION()
	void OnRep_ActivePromptCorpse();
};