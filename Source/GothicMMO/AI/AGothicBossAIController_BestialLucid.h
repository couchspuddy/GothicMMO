// GothicBossAIController_BestialLucid.h
// Bestial Lucid boss AI controller.
//
// Phase 1 -> 2 fires when health first drops below Phase2HealthThreshold.
// That crossing does NOT advance the phase directly anymore — it starts a
// scripted transition sequence (Cry, forced move to the nearest pillar,
// forced Wall Pound, then the actual phase advance) owned by the Behavior
// Tree, not this controller. The controller's job is narrower than it used
// to be: detect the threshold once, hand off to the BT via a Blackboard
// flag, and expose ONE public entry point (CompletePhase2Transition) for
// the BT's last step to call once the scripted beat is actually done.
//
// Zone Collapse (the old unconditional 17.5s timer cycling through tagged
// destructible zones) is retired as of this pass — superseded by Wall Pound,
// which does the same job but is player-influenceable (skippable through
// good positioning) rather than firing on a fixed clock regardless of what
// the player does. Running both would have meant the room kept shrinking on
// its own timer no matter how well the player kited her away from walls,
// which defeats the entire point of Wall Pound's proximity-skip condition.
//
// Assumes possession of a pawn with a UGothicVitalPointComponent and an ASC.

#pragma once

#include "CoreMinimal.h"
#include "AI/AGothicBossAIController.h"
#include "AGothicBossAIController_BestialLucid.generated.h"

class UGothicVitalPointComponent;
class UAbilitySystemComponent;
struct FOnAttributeChangeData;

UCLASS()
class GOTHICMMO_API AGothicBossAIController_BestialLucid : public AGothicBossAIController
{
	GENERATED_BODY()

public:
	AGothicBossAIController_BestialLucid();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	/**
	 * Called by the Behavior Tree's transition Sequence as its final step,
	 * once Cry, the forced move to a pillar, and the forced Wall Pound have
	 * all actually completed. This is the ONLY public entry point for
	 * finishing the transition — clears the pending flag and runs the real
	 * phase advance (Blackboard write, broadcast, vital freeze), which
	 * OnPhaseAdvance still owns exactly as before.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gothic|Boss|BestialLucid")
	void CompletePhase2Transition();

protected:
	/**
	 * Phase 1 -> 2 fires when health first drops to or below this fraction of
	 * MaxHealth. Fraction rather than a flat value so it survives health
	 * retuning and scales when she's balanced for a full Kindle.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Boss|BestialLucid",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Phase2HealthThreshold = 0.5f;

	/**
	 * Index into the pawn's VitalPointLocations that the vital resolves to in
	 * Phase 2 — the heart. Phase 1 shifts freely; Phase 2 lands here and stops.
	 *
	 * Author the heart as the LAST entry in her array, not index 0 — index 0 is
	 * where Phase 1 starts, and opening on the heart spoils the Phase 2 reveal.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Boss|BestialLucid")
	int32 Phase2LockedVitalIndex = 0;

	/**
	 * Blackboard Bool key set true the instant health crosses the threshold.
	 * The BT's transition Sequence gates on this (Observer Abort: Lower
	 * Priority) to take over from whatever's running. Stays true for the
	 * entire scripted beat — CompletePhase2Transition clears it once the
	 * sequence is genuinely done, which is also what unblocks HandleHealthChanged
	 * from ever firing this a second time.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Boss|BestialLucid")
	FName TransitionPendingBlackboardKey = "bTransitionPending";

	/** Override to add Bestial Lucid-specific phase-advance behavior on top of the base bookkeeping. */
	virtual void OnPhaseAdvance() override;

	/**
	 * Undo everything the Phase 2 beat set, on top of the base health + phase
	 * reset. Three pieces of state outlive a phase rollback on their own and
	 * each one alone is enough to leave her unkillable on the next pull:
	 * bTransitionTriggered (the one-shot guard — Phase 2 could never fire
	 * again), the pending Blackboard flag (the BT would resume a scripted
	 * transition she is no longer in), and the frozen vital point (Phase 1 with
	 * a Phase 2 weak spot locked open).
	 */
	virtual void OnLeashReset() override;

private:
	UPROPERTY()
	TObjectPtr<UGothicVitalPointComponent> CachedVitalPointComponent;

	UFUNCTION()
	void HandleVitalPointShifted(int32 NewIndex, FVector NewWorldLocation);

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> CachedASC;

	FDelegateHandle HealthChangedHandle;

	void HandleHealthChanged(const FOnAttributeChangeData& Data);

	/**
	 * One-shot guard separate from CurrentPhase — CurrentPhase deliberately
	 * doesn't flip to 2 until CompletePhase2Transition runs, but health can
	 * keep changing (more hits landing) during the scripted beat itself, and
	 * HandleHealthChanged would otherwise have nothing stopping it from
	 * re-triggering the transition mid-sequence.
	 */
	bool bTransitionTriggered = false;
};