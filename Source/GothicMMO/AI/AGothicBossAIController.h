// GothicBossAIController.h
// Base class for all boss-tier AI controllers.
// Extends the standard enemy controller with generic phase tracking.
// Subclasses (e.g. Bestial Lucid) implement their own trigger conditions
// and override OnPhaseAdvance to define what changes per phase.
// The base class only owns the bookkeeping — current phase, and a
// broadcast event other systems (Behavior Tree, Blackboard, VFX) can react to.

#pragma once

#include "CoreMinimal.h"
#include "AI/GothicEnemyAIController.h"
#include "AGothicBossAIController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBossPhaseChanged, int32, NewPhase);

UCLASS()
class GOTHICMMO_API AGothicBossAIController : public AGothicEnemyAIController
{
	GENERATED_BODY()

public:
	// AGothicBossAIController.h — add to public section
	virtual void OnPossess(APawn* InPawn) override;
	AGothicBossAIController();

	/** Current phase, starting at 1. Subclasses advance this via OnPhaseAdvance. */
	UFUNCTION(BlueprintPure, Category = "Gothic|Boss")
	int32 GetCurrentPhase() const { return CurrentPhase; }

	/** Broadcast whenever the phase changes — Blueprint/BT can bind to this. */
	UPROPERTY(BlueprintAssignable, Category = "Gothic|Boss")
	FOnBossPhaseChanged OnBossPhaseChanged;

protected:
	/**
	 * Called by subclasses when their specific trigger condition for
	 * advancing to the next phase is met. Increments CurrentPhase,
	 * sets the Blackboard key (if configured), and broadcasts the event.
	 * Does NOT contain any boss-specific trigger logic itself — subclasses
	 * decide *when* to call this, this function only handles *what happens*
	 * generically once it's called.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gothic|Boss")
	virtual void OnPhaseAdvance();

	/** Blackboard key to write the current phase into, for BT nodes to branch on. */
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Boss")
	FName PhaseBlackboardKey = "CurrentPhase";

	int32 CurrentPhase = 1;
};