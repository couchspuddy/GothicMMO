// GothicBossAIController_BestialLucid.h
// Bestial Lucid boss AI controller.
// Advances Phase 1 -> Phase 2 (the Stillness beat, per design doc) when her
// health first drops below a threshold. Phase 2 freezes the vital point at
// the heart and starts the timed ceiling collapse.
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
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

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

	/** Override to add Bestial Lucid-specific phase-advance behavior on top of the base bookkeeping. */
	virtual void OnPhaseAdvance() override;
	
	// AGothicBossAIController_BestialLucid.h — add to protected section
	/** Tag applied to destructible debris actors in the level (per Eagle's
	 *  Landing Blockout Scale doc's "reserved destructible zones"). Found
	 *  automatically at possess time — no manual per-instance wiring needed. */
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Boss|BestialLucid")
	FName DestructibleZoneTag = "BestialLucidZone";

	/** Seconds between zone collapse triggers during Phase 2. Doc range: 15-20s. */
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Boss|BestialLucid")
	float ZoneCollapseInterval = 17.5f;

	/**
	 * Fired when it's time for a zone to collapse. Blueprint owns the actual
	 * telegraph/debris visual — C++ only decides when and which zone.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Boss|BestialLucid")
	void TriggerZoneCollapse(int32 ZoneIndex, AActor* ZoneActor);

private:
	UPROPERTY()
	TObjectPtr<UGothicVitalPointComponent> CachedVitalPointComponent;

	UFUNCTION()
	void HandleVitalPointShifted(int32 NewIndex, FVector NewWorldLocation);
	/** Discovered via tag search in OnPossess — not manually assigned. */
	TArray<AActor*> DestructibleZones;
	
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> CachedASC;

	FDelegateHandle HealthChangedHandle;

	void HandleHealthChanged(const FOnAttributeChangeData& Data);

	/** Cycles through DestructibleZones sequentially, not randomly —
	 *  matches the doc's "could be as simple as cycling" preference. */
	int32 NextZoneCollapseIndex = 0;

	FTimerHandle ZoneCollapseTimerHandle;

	void HandleZoneCollapseTimer();
};