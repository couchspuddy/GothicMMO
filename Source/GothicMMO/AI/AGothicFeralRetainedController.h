// AGothicFeralRetainedController.h
// AI controller for the Feral Retained mini-boss.
//
// Owns the Part 1 -> Part 2 break-out trigger. Mirrors the Bestial Lucid
// controller's threshold detection exactly — binds the pawn's Health attribute,
// fires a one-shot when health first crosses BreakoutHealthThreshold — but where
// the boss hands off to a multi-beat scripted BT sequence, the Retained's
// break-out is a single ability (GA_FeralBreakout), so this activates it
// directly and advances the phase. No new BT surface needed.
//
// The health threshold is a stand-in for "Part 1 duel resolved": until the
// upstairs duel exists, being brought low in the arena IS the break-out cue,
// and Part 1's gating layers on top later without changing this.

#pragma once

#include "CoreMinimal.h"
#include "AI/AGothicBossAIController.h"
#include "GameplayTagContainer.h"
#include "AGothicFeralRetainedController.generated.h"

class UAbilitySystemComponent;
struct FOnAttributeChangeData;

UCLASS()
class GOTHICMMO_API AGothicFeralRetainedController : public AGothicBossAIController
{
    GENERATED_BODY()

// The leap flight guard this class used to own (BeginLeapFlight, the landing
// hook, the safety timer, LeapFlightSafetySeconds) now lives on
// AGothicEnemyAIController — every enemy that launches itself needs it, not
// just this one. Behaviour here is unchanged; it is inherited verbatim.

public:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

protected:
    /**
     * Break-out fires when health first drops to or below this fraction of
     * MaxHealth. Fraction, not a flat value, so it survives health retuning.
     * She does not die at the crossing — the threshold sits above 0, so she
     * is alive to break out (barring a single hit that overkills from above
     * the threshold straight to dead; acceptable for first pass).
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Feral",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BreakoutHealthThreshold = 0.35f;

    /**
     * AssetTag of the break-out ability to activate. Set to Ability.Feral.Breakout
     * — must match the AbilityTags on BP_GA_FeralBreakout. Falls back to a runtime
     * tag request if left unset in the Blueprint.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Feral")
    FGameplayTag BreakoutAbilityTag;

private:
    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> CachedASC;

    FDelegateHandle HealthChangedHandle;

    void HandleHealthChanged(const FOnAttributeChangeData& Data);

    /**
     * One-shot guard, separate from CurrentPhase: health keeps changing after
     * the break-out as more hits land, and without this every subsequent hit
     * would re-enter and try to break out again.
     */
    bool bBrokenOut = false;
};
