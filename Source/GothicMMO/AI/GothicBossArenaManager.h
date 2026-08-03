// GothicBossArenaManager.h
// Level-placed actor that manages the four Rotunda pillars.
// Tracks pillar state, provides aggression scaling to the boss BT,
// and handles the Cry passive damage distribution to all pillars.
//
// Setup in editor:
//   1. Place one of these in the Rotunda level
//   2. Assign all four GothicRotundaPillar references
//   3. The boss BT queries this actor for aggression multiplier
//   4. BTTask_BossCry calls ApplyCryDamage on this actor
//   5. Assign BossActor (or set BossClass on the Blueprint) so the ambient
//      collapse timer knows when the fight is happening — see below
//
// THE ARENA DEGRADES ON TWO CLOCKS
//
// Wall Pound is the boss-driven pillar destroyer: it fires where the Pounce
// chain ends, so it only takes a pillar when the fight has drifted near one.
// That alone lets a careful player keep all four standing forever by kiting her
// into open floor, which makes the arena a static box.
//
// So there is a second, ambient clock: CollapseIntervalByPillarCount drops a
// random surviving pillar on a timer that ACCELERATES as pillars fall. The
// arena is on its way down whatever the player does; kiting changes which
// pillars go and how fast, not whether.
//
// The endgame is deliberately not a wipe. With all four down there is no
// fight-ending collapse and no timer left to run — what remains is maximum
// aggression and four sealed zones. The arena finishes shrinking and then just
// stays small.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GothicBossArenaManager.generated.h"

class AGothicRotundaPillar;
class AGothicEnemyBase;

/**
 * Fires when a pillar falls, carrying the new aggression multiplier and the new
 * standing-pillar count.
 *
 * This is the PRESENTATION hook — music, arena lighting, VFX, anything a
 * Blueprint wants to do the moment the arena escalates.
 *
 * It is not how the AI reads aggression. The behavior tree polls
 * GetAggressionMultiplier() directly (GothicBTService_WeightedActionSelect and
 * GothicBTTask_ComputeRepositionPoint), because BT nodes are shared objects with
 * per-instance NodeMemory and cannot safely bind a dynamic delegate.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnArenaAggressionChanged, float, NewAggressionMultiplier, int32, PillarsRemaining);

UCLASS()
class GOTHICMMO_API AGothicBossArenaManager : public AActor
{
    GENERATED_BODY()

public:
    AGothicBossArenaManager();

    /** How many pillars are still standing. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Arena")
    int32 GetPillarsRemaining() const;

    /**
     * Aggression multiplier based on pillars destroyed.
     * 4 pillars = 1.0, 3 = 1.15, 2 = 1.35, 1 = 1.6, 0 = 2.0
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Arena")
    float GetAggressionMultiplier() const;

    /** Called by BTTask_BossCry. Applies passive damage to all surviving pillars. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Arena")
    void ApplyCryDamage(float DamagePerPillar);

    /** Returns the nearest non-destroyed pillar to a world location. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Arena")
    AGothicRotundaPillar* GetNearestSurvivingPillar(FVector FromLocation) const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Arena")
    bool AnyPillarsRemaining() const { return GetPillarsRemaining() > 0; }

    /** Broadcast on every pillar loss with the recomputed aggression multiplier. */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|Arena")
    FOnArenaAggressionChanged OnArenaAggressionChanged;

protected:
    virtual void BeginPlay() override;

    /** Assign all four pillar actors from the level. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TArray<TObjectPtr<AGothicRotundaPillar>> Pillars;

    /**
     * Aggression values per pillar count.
     * Index 0 = 0 pillars remaining, Index 4 = 4 pillars remaining.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TArray<float> AggressionByPillarCount;

    /**
     * Seconds between ambient collapses, indexed by pillars REMAINING — same
     * idiom as AggressionByPillarCount, so index 4 is the full arena and index 1
     * is the last pillar standing.
     *
     * Index 0 is unused and exists only to keep the two arrays indexed the same
     * way: with nothing left to collapse the timer disarms rather than reading
     * it.
     *
     * THESE NUMBERS ARE UNMEASURED STARTING POINTS, NOT CHOSEN VALUES. They were
     * picked to have the right SHAPE — roughly 90s of grace at a full arena
     * accelerating to 50s once she is down to one pillar, so the pressure builds
     * without the first two minutes being about the ceiling. Nothing has been
     * played against them. The tuning pass wants a measured time-to-kill for the
     * fight and an interval set as a fraction of it; treat any number here that
     * survives that pass unchanged as a coincidence.
     *
     * An empty array — or one too short to index — disables the ambient timer
     * entirely, which is the correct default for any arena that wants Wall Pound
     * to be the only thing that takes a pillar. Leaving it empty is a supported
     * configuration, not a misconfiguration, so it is not warned about.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TArray<float> CollapseIntervalByPillarCount;

    /**
     * The boss whose combat state gates the ambient timer.
     *
     * Optional. If it is null at BeginPlay the manager falls back to
     * GetActorOfClass(BossClass); if that finds nothing either, the ambient
     * timer stays disarmed and says so once. Explicit beats discovered — an
     * arena with more than one enemy of the boss's class in it wants this set.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TObjectPtr<AGothicEnemyBase> BossActor;

    /**
     * Fallback used to find the boss when BossActor is unset. Set this on the
     * Blueprint (BP_GothicBossArenaManager) to the arena's boss class and the
     * manager needs no per-instance wiring at all.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TSubclassOf<AGothicEnemyBase> BossClass;

    /**
     * How often the manager samples the boss's combat state (seconds).
     *
     * A poll, not a delegate, and deliberately so: the combat latch this reads
     * (AGothicEnemyBase::GetCombatTarget) broadcasts nothing, and the alternative
     * — adding a delegate to UGothicCombatStateComponent — would put a new
     * multicast on every character in the project to serve one arena actor. At
     * 1Hz against a collapse interval measured in tens of seconds the sampling
     * error is irrelevant.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena",
              meta = (ClampMin = "0.1"))
    float CombatPollInterval = 1.f;

private:
    UFUNCTION()
    void OnPillarDestroyed(AGothicRotundaPillar* Pillar);

    /**
     * True only on the manager that GetActorOfClass hands out.
     *
     * L_EaglesLanding contains FOUR identically-wired BP_GothicBossArenaManager
     * instances pointing at the same pillars, and every consumer in the project
     * reaches them through GetActorOfClass — which returns an arbitrary one. Four
     * managers each running the ambient timer would collapse the arena four times
     * as fast as authored, and the bug would present as "the tuning numbers are
     * wrong" rather than as a level defect.
     *
     * So each manager asks at BeginPlay whether it is the one GetActorOfClass
     * returns, and only that one arms anything. This keeps the C++ correct
     * regardless of how many copies the level holds — now, and the next time one
     * gets duplicated.
     */
    bool bIsElectedManager = false;

    /** Last sampled combat state, so the poll can act on edges rather than levels. */
    bool bFightActive = false;

    FTimerHandle CombatPollTimerHandle;
    FTimerHandle AmbientCollapseTimerHandle;

    /** Resolves BossActor, falling back to GetActorOfClass(BossClass). */
    AGothicEnemyBase* ResolveBoss();

    /** 1Hz sample of the boss's combat latch; arms/disarms on the transitions. */
    void PollCombatState();

    /** Arms (or re-arms) the ambient collapse timer for the CURRENT pillar count. */
    void ArmAmbientCollapseTimer();

    /** Stops the ambient clock. Safe when nothing is armed. */
    void DisarmAmbientCollapseTimer();

    /** Timer expiry: drop one random surviving pillar, then re-arm. */
    void TriggerAmbientCollapse();
};