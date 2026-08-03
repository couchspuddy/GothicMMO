// GA_BestialLucidWallPound.h
// She claws the nearest tagged wall/pillar. Self-contained: does its own
// overlap query for WallPoundTarget-tagged actors rather than reading
// anything off the Blackboard, matching how GA_HuntersStrike does its own
// trace instead of reaching into AI internals — abilities in this project
// don't know or care that a BT is what activated them.
//
// Deliberately environmental-only in this pass, per what's actually been
// specified: every stated damage number (the grab, the rubble AOE) belongs
// to Slam's wall collision, not to this ability. If Wall Pound should also
// hit the player directly, that's a small addition to the same overlap
// query below — flagged, not assumed.
//
// The actual collapse/VFX is a BlueprintImplementableEvent on the target
// wall actor, not hardcoded here — keeps the "how a wall collapses" content
// question in Blueprint/level-design hands, where it belongs, while this
// ability only decides WHEN and WHICH wall.
//
// THE SMASH IS NO LONGER INSTANT
//
// ActivateAbility used to do everything: Super (which kicks the Blueprint
// graph's montage off asynchronously), the overlap search, the collapse call,
// and then EndAbility on the SAME FRAME. Ending the ability tears down the
// graph's PlayMontageAndWait task, so the montage was cancelled the instant it
// started — the pillar fell with no animation in front of it, and the boss's
// most legible telegraph was invisible.
//
// This class now uses the same split as GA_BestialLucidCharge: C++ owns the
// payload and exposes it as one BlueprintCallable, the graph owns the montage
// and decides WHEN in the animation the payload fires. See
// ExecuteWallPoundImpact.
//
// REQUIRED BLUEPRINT GRAPH (BP_GA_BestialLucid_WallPound)
//
//   ActivateAbility
//     -> PlayMontageAndWait (MontageToPlay)
//     -> at the strike point (an AnimNotify, or a Delay tuned to the impact
//        frame) -> ExecuteWallPoundImpact
//     -> EndAbility on ALL of the montage task's completion pins
//        (OnCompleted, OnBlendOut, OnInterrupted, OnCancelled).
//
// A graph that forgets any of that does not hang the fight — see
// WallPoundSafetyTimeout below — but it does waste the cooldown.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_BestialLucidWallPound.generated.h"

UCLASS()
class GOTHICMMO_API UGA_BestialLucidWallPound : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_BestialLucidWallPound();

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

    virtual void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled) override;

    /**
     * The payload. Finds the nearest surviving WallPoundTarget pillar and drops
     * it. Call from the graph at the STRIKE POINT of the montage — the frame her
     * claws land on stone — not at activation.
     *
     * Idempotent per activation: a graph wired to call this twice, or a notify
     * that fires on a looping section, cannot collapse two pillars off one
     * attack.
     *
     * Finding nothing in range is a legitimate outcome, not an error. The whiff
     * IS the design: Wall Pound is the Pounce chain's finisher and it only bites
     * when the fight has drifted near a pillar, so kiting the boss into open
     * floor is the counterplay to the arena degrading.
     */
    UFUNCTION(BlueprintCallable, Category = "Wall Pound")
    void ExecuteWallPoundImpact();

protected:
    /** Same tag GothicBTService_NearbyTerrainCheck already looks for — keep these in sync. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wall Pound")
    FName TerrainTag = FName("WallPoundTarget");

    /** How far to search for a target wall. Should roughly match (or be slightly
     *  tighter than) the terrain check's CheckRadius — if this doesn't find
     *  anything, the BT shouldn't have activated this in the first place, but
     *  a mismatch between the two radii is the first thing to check if it does. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wall Pound")
    float SearchRadius = 400.f;

    /**
     * WEDGE GUARD. Seconds after activation at which the ability force-ends
     * itself regardless of what the Blueprint graph is doing.
     *
     * Handing the ability's lifetime to a Blueprint graph is the price of the
     * montage split, and the failure mode is worse here than it is for Charge.
     * Charge's equivalent risk is a boss left mid-flight, which
     * AGothicEnemyAIController::AbortLeapFlight unwinds from EndAbility; the
     * risk here is that the graph never reaches EndAbility at all — a montage
     * interrupted on a pin nobody wired, an asset with no montage assigned, a
     * half-finished graph — and the attack-commitment lock that gates the boss's
     * whole decision pool is then held forever. The boss stops attacking for the
     * rest of the fight and nothing in the log says why.
     *
     * So there is always a clock. It is a BACKSTOP, not a duration: it must sit
     * comfortably above the longest Wall Pound montage, because ending early
     * would cut the animation short exactly the way the old same-frame EndAbility
     * did. 5s against a montage of roughly 1.5-2.5s. If it ever fires, that is a
     * broken graph and it says so at Error.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wall Pound",
              meta = (ClampMin = "0.1"))
    float WallPoundSafetyTimeout = 5.f;

private:
    /** Force-end fired by the safety timer. Loud: reaching it means a broken graph. */
    void HandleWallPoundSafetyTimeout();

    /**
     * True once ExecuteWallPoundImpact has run for the current activation.
     * Reset on activation, not on end, so an ability instance reused across
     * activations starts each one able to fire exactly once.
     */
    bool bImpactFiredThisActivation = false;

    FTimerHandle WallPoundSafetyTimerHandle;
};