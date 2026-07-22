// GothicEnemyAIController.h
// AI Controller for all Gothic enemies.
// Runs a Behavior Tree (assign BT_EnemyCombat in Blueprint).
// Manages the Blackboard and exposes helper functions to BT Tasks.
//
// Engagement model:
//   Enemies do NOT run directly to the player. They approach to a
//   PreferredEngageDistance, decelerate in the last ApproachDecelDistance,
//   and wait a staggered delay before committing. Attack tasks then
//   close the remaining gap with a lunge built into the animation.
//
// Blueprint child: BP_GothicEnemyAIController
//   - Set BehaviorTreeAsset to BT_EnemyCombat
//   - Assign to enemy Blueprint's AIControllerClass

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GenericTeamAgentInterface.h"
#include "GothicEnemyAIController.generated.h"

class UBehaviorTree;
class UBlackboardComponent;

// ============================================================================
// Blackboard Key Names
// Keep these in sync with your Blackboard asset (BB_Enemy).
// ============================================================================
namespace GothicBBKeys
{
    static const FName TargetActor          = TEXT("TargetActor");            // AActor*
    static const FName TargetLocation       = TEXT("TargetLocation");         // FVector
    static const FName bCanSeeTarget        = TEXT("bCanSeeTarget");          // bool
    static const FName bIsInCombat          = TEXT("bIsInCombat");            // bool
    static const FName PatrolOrigin          = TEXT("PatrolOrigin");           // FVector
    static const FName AttackRange           = TEXT("AttackRange");            // float
    static const FName EngageDistance        = TEXT("EngageDistance");          // float — stop here, don't stand on player
    static const FName StaggerDelay          = TEXT("StaggerDelay");           // float — randomized per-enemy approach delay
}

UCLASS()
class GOTHICMMO_API AGothicEnemyAIController : public AAIController
{
    GENERATED_BODY()

public:
    AGothicEnemyAIController();

    virtual FGenericTeamId GetGenericTeamId() const override
    {
        return FGenericTeamId(1);
    }

    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

    /**
     * Called by AGothicEnemyBase::SetCombatTarget when perception fires.
     * Updates the Blackboard and transitions the BT to combat mode.
     * Rolls a new StaggerDelay each time combat is entered.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI")
    void SetBlackboardTarget(AActor* NewTarget);

    /**
     * Clears the combat target and returns to patrol/idle state.
     * Call when target dies or escapes detection range.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI")
    void ClearCombatTarget();

    /**
     * Returns the current Blackboard target as a typed actor.
     * Useful in BT Tasks to avoid casting to UObject manually.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    AActor* GetTargetActor() const;

    /**
     * Returns true if the target is within MeleeAttackRange.
     * This gates the attack task — NOT the approach distance.
     * The enemy holds at EngageDistance, then the attack task
     * checks this before lunging.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    bool IsTargetInAttackRange() const;

    // -------------------------------------------------------------------------
    // Engagement tuning — read by BTService_ApproachSpeed
    // -------------------------------------------------------------------------

    /** The distance the enemy tries to hold before attacking (cm). */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetPreferredEngageDistance() const { return PreferredEngageDistance; }

    /** Distance at which the enemy begins decelerating (cm). */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetApproachDecelDistance() const { return ApproachDecelDistance; }

    /** Speed multiplier at engage distance (0-1). */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetDecelSpeedMultiplier() const { return DecelSpeedMultiplier; }

    /** The default walk speed before any decel scaling. Cached on possess. */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetDefaultWalkSpeed() const { return DefaultWalkSpeed; }
    
    /** Pauses the BT for the given duration — pack regroup on member death. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI")
    void EnterRegroupPause(float Duration);


protected:
    /** Assign in BP_GothicEnemyAIController. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

    /**
     * Standard melee attack range (cm).
     * This is the "can I attack" gate, not the approach distance.
     * Should be SMALLER than PreferredEngageDistance — the attack's
     * lunge animation covers the gap.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Attack")
    float MeleeAttackRange = 200.f;

    /**
     * Distance the enemy holds from the target before attacking (cm).
     * Set this per-tier in Blueprint children:
     *   Thrall: ~250   (close but not on top)
     *   Retained: ~350 (deliberate, measured)
     *   Feral: ~300    (fast closer, earns the gap)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Engagement")
    float PreferredEngageDistance = 300.f;

    /**
     * Distance at which the enemy starts decelerating (cm).
     * When closer than this to the target, speed scales down linearly
     * from full to DecelSpeedMultiplier at EngageDistance.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Engagement")
    float ApproachDecelDistance = 500.f;

    /**
     * Speed multiplier at engagement distance.
     * 0.6 means 60% of MaxWalkSpeed when arriving at stance distance.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Engagement")
    float DecelSpeedMultiplier = 0.6f;

    /**
     * Range for the randomized stagger delay (seconds).
     * Each time combat is entered, a random value in this range is
     * written to the Blackboard. The BT uses a Wait node reading
     * StaggerDelay before beginning the approach.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI|Engagement")
    FVector2D StaggerDelayRange = FVector2D(0.3f, 1.2f);

    /**
     * Range at which the enemy gives up chasing and returns to patrol (cm).
     * Should be larger than the perception LoseSightRadius.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI")
    float LeashRange = 3000.f;
    

private:
    /** Cached patrol spawn point — enemy returns here when leash breaks. */
    FVector PatrolOrigin;

    /** Cached default walk speed from the possessed pawn's movement component. */
    float DefaultWalkSpeed = 0.f;

    /** Periodic check to see if the target escaped the leash range. */
    FTimerHandle LeashCheckTimer;
    void CheckLeash();
};