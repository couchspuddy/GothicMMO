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

public:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

    /**
     * Called by GA_FeralBreakout immediately before LaunchCharacter.
     *
     * The launch itself was never the problem — it already overrides both axes.
     * What killed it was everything that happens on the ticks AFTER: the
     * behaviour tree re-issues a Move To toward the player, and in MOVE_Falling
     * that request is spent as air-control acceleration against the launch's XY
     * velocity. Measured in PIE: with the player standing OPPOSITE the leap
     * direction she gained her full +322uu of height and travelled 7-13uu
     * horizontally — she went straight up and landed where she stood.
     *
     * So the flight is made steering-proof from both ends: the brain is paused
     * (no new move requests) AND AirControl is driven to zero (an in-flight
     * request that slips through cannot bend the arc anyway). AirControl is set
     * on the Blueprint, not in C++, so zeroing it is the only value-independent
     * way to be sure.
     *
     * Both are undone on landing — ACharacter::LandedDelegate, with a timer
     * fallback so a missed landing can never leave her permanently brain-dead.
     */
    void BeginLeapFlight();

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

    /**
     * Hard ceiling on the leap flight, in seconds. If the landing never arrives
     * — she clips into geometry, gets teleported, dies mid-arc — this restores
     * her brain and her air control anyway. Comfortably longer than the ~1.3s
     * the measured arc spends airborne.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Feral",
        meta = (ClampMin = "0.5", ClampMax = "10.0"))
    float LeapFlightSafetySeconds = 3.f;

private:
    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> CachedASC;

    FDelegateHandle HealthChangedHandle;

    void HandleHealthChanged(const FOnAttributeChangeData& Data);

    /** Landing hook bound to the pawn's LandedDelegate for the leap's duration. */
    UFUNCTION()
    void HandleLeapLanded(const FHitResult& Hit);

    /** Fallback if the landing never fires — see LeapFlightSafetySeconds. */
    void HandleLeapFlightTimeout();

    /** Unpause the brain, restore AirControl, unbind, clear the timer. Idempotent. */
    void EndLeapFlight(const TCHAR* Reason);

    /** In-flight guard — EndLeapFlight is racing three callers (landing, timer, unpossess). */
    bool bLeapInFlight = false;

    /** AirControl as it was before the leap, restored verbatim on landing. */
    float SavedAirControl = 0.f;

    FTimerHandle LeapFlightSafetyHandle;

    /**
     * One-shot guard, separate from CurrentPhase: health keeps changing after
     * the break-out as more hits land, and without this every subsequent hit
     * would re-enter and try to break out again.
     */
    bool bBrokenOut = false;
};
