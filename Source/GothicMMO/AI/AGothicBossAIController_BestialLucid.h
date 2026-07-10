// GothicBossAIController_BestialLucid.h
// Bestial Lucid boss AI controller.
// Listens for the vital point reaching its designated "most protected"
// location and triggers the Phase 1 -> Phase 2 transition (the Stillness
// beat, per design doc) via the base class's OnPhaseAdvance.
//
// Assumes possession of a pawn with a UGothicVitalPointComponent.

#pragma once

#include "CoreMinimal.h"
#include "AI/AGothicBossAIController.h"
#include "AGothicBossAIController_BestialLucid.generated.h"

class UGothicVitalPointComponent;

UCLASS()
class GOTHICMMO_API AGothicBossAIController_BestialLucid : public AGothicBossAIController
{
	GENERATED_BODY()

public:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

protected:
	/**
	 * The vital point index that, when reached, triggers the Stillness
	 * beat and advances to Phase 2. Set in Blueprint based on how many
	 * vital locations are configured on her VitalPointComponent.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Boss|BestialLucid")
	int32 Phase2TriggerVitalIndex = 2;

	/** Override to add Bestial Lucid-specific phase-advance behavior on top of the base bookkeeping. */
	virtual void OnPhaseAdvance() override;

private:
	UPROPERTY()
	TObjectPtr<UGothicVitalPointComponent> CachedVitalPointComponent;

	UFUNCTION()
	void HandleVitalPointShifted(int32 NewIndex, FVector NewWorldLocation);
};