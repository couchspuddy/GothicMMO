// GA_FeralBreakout.h
// The Feral Retained's Part 1 -> Part 2 break-out.
//
// Fires once, when AGothicFeralRetainedController detects her health crossing
// the break-out threshold in the arena. She crashes down among the waiting
// feral thralls, buffs them, calls in a reinforcement wave, and turns the whole
// pack onto the player. This does NOT violate the mini-boss doc's "no adds, pure
// 1v1" rule — that rule governs Part 1 (the upstairs duel). This is the beat
// AFTER the duel, downstairs, where the isolation deliberately ends.
//
// Shaped after GA_BestialLucidCry: montage hit-window (or instant fallback) ->
// PerformBreakout -> SphereOverlap-buff nearby enemies + tag-discovered spawn
// wave, all folded into the owning AGothicEncounterVolume so the arena completes
// and pays Selah through the existing path.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_FeralBreakout.generated.h"

class AGothicEnemyBase;

UCLASS()
class GOTHICMMO_API UGA_FeralBreakout : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_FeralBreakout();

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

protected:
    /** Radius around her in which already-placed feral thralls are rallied. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Breakout")
    float RallyRadius = 1800.f;

    /**
     * The buff applied to every rallied thrall (and every reinforcement).
     * Assign GE_FeralRally — grants State.Feral.Rallied and an AttackPower boost.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Breakout")
    TSubclassOf<UGameplayEffect> RallyBuffEffect;

    /** Actor tag on AGothicEnemySpawnPoint actors used for the reinforcement wave. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Breakout")
    FName RallySpawnTag = FName("FeralRally");

    /** Maximum reinforcements spawned by the break-out. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Breakout")
    int32 MaxWaveThralls = 4;

    /** Actor tag on a target placed on the arena floor. On break-out she leaps
     *  there — down from her upstairs Part 1 perch into the arena.
     *  NAME_None skips the leap and she rallies in place. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Breakout")
    FName LeapTargetTag = FName("FeralLeapTarget");

    /**
     * Arc height for the break-out leap, 0-1. LOWER is steeper and higher —
     * this reads backwards, and it is the engine's convention, not a typo:
     * SuggestProjectileVelocity_CustomArc treats the parameter as "how direct
     * is the shot", so 1.0 is a flat line drive and 0.0 is a near-vertical lob.
     *
     * Measured 2026-07-27 on a ~530uu leap, arc vs. what she actually does:
     *     0.85 -> rise   0uu, 0.00s airborne, travels 171uu  (a flat slide)
     *     0.55 -> rise  66uu, 0.34s airborne, travels 132uu  (a stumble)
     *     0.25 -> rise 444uu, 1.34s airborne, travels 620uu  (a leap)
     *
     * The comment here used to claim the opposite, which is worth knowing
     * because raising it to "get more air" makes her stop leaping altogether:
     * a flat solution keeps her grounded, and ground friction eats the launch
     * within a couple of hundred uu.
     *
     * She used to hard-teleport to the target, which read as a pop rather than
     * a break-out. Now she is launched on a ballistic arc, so the landing is
     * driven by gravity and she is genuinely airborne on the way down.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Breakout",
              meta = (ClampMin = "0.05", ClampMax = "0.95"))
    float LeapArcParam = 0.6f;

    // Base class montage callback — fires PerformBreakout at the hit window.
    virtual void OnMontageHitWindow(FGameplayEventData Payload) override;

private:
    /** Buff + aggro one enemy. Null-safe. */
    void RallyEnemy(AGothicEnemyBase* Enemy, AActor* AggroTarget);

    /** Spawns the reinforcement wave from tagged points; fills OutWave. */
    void SpawnRallyWave(AActor* AggroTarget, TArray<AGothicEnemyBase*>& OutWave);

    /** The whole payload — rally nearby, spawn the wave, fold into the encounter. */
    void PerformBreakout();
};
