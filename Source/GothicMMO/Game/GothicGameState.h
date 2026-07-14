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
	/** The corpse currently showing an active, uncollected shared Selah prompt. Null = no active prompt. */
	UPROPERTY(ReplicatedUsing = OnRep_ActivePromptCorpse, BlueprintReadOnly, Category = "Gothic|Selah")
	TObjectPtr<AGothicEnemyBase> ActivePromptCorpse;

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

protected:
	UFUNCTION()
	void OnRep_ActivePromptCorpse();
};