// GothicEnemyAIController.h
// AI Controller for all Gothic enemies.
// Runs a Behavior Tree (assign BT_EnemyCombat in Blueprint).
// Manages the Blackboard and exposes helper functions to BT Tasks.
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
    static const FName TargetActor     = TEXT("TargetActor");     // AActor*
    static const FName TargetLocation  = TEXT("TargetLocation");  // FVector
    static const FName bCanSeeTarget   = TEXT("bCanSeeTarget");   // bool
    static const FName bIsInCombat     = TEXT("bIsInCombat");     // bool
    static const FName PatrolOrigin    = TEXT("PatrolOrigin");    // FVector
    static const FName AttackRange     = TEXT("AttackRange");     // float
    static const FName ChosenAction    = TEXT("ChosenAction");    // FName   — WeightedActionSelect output
    static const FName RepositionPoint = TEXT("RepositionPoint"); // FVector — ComputeRepositionPoint output
    static const FName bPackRegroup    = TEXT("bPackRegroup");    // bool    — PackSubsystem sync pause
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
     * Returns true if the enemy can reach its attack range to the target.
     * Used by BT Decorators to gate attack tasks.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    bool IsTargetInAttackRange() const;
    
    void EnterRegroupPause(float Duration);

protected:
    /** Assign in BP_GothicEnemyAIController. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

    /**
     * Standard melee attack range (cm).
     * Blueprint children override this per-enemy type.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI")
    float MeleeAttackRange = 200.f;

    /**
     * Range at which the enemy gives up chasing and returns to patrol (cm).
     * Should be larger than the perception LoseSightRadius.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI")
    float LeashRange = 3000.f;

    /**
     * Editor/debug only — dumps full AI state on the existing 2s leash timer.
     * Answers "why did she stop" definitively in one PIE pass by sampling every
     * variable that could gate the Behavior Tree, from every source that writes one.
     *
     * Turn this on for the boss and the Feral Retained; leave it off elsewhere.
     * Strip or leave behind the flag once the disengagement defect is closed.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Gothic|AI|Debug")
    bool bDebugAIState = false;

private:
    /** Cached patrol spawn point — enemy returns here when leash breaks. */
    FVector PatrolOrigin;

    /** Periodic check to see if the target escaped the leash range. */
    FTimerHandle LeashCheckTimer;
    void CheckLeash();
    
    /** Clears bPackRegroup after the pause duration. One handle per
     *  controller — a retrigger (shouldn't happen inside the subsystem's
     *  lockout window, but defensively) resets rather than stacks. */
    FTimerHandle RegroupPauseTimer;

    /** Single-line multi-value state dump. Called from CheckLeash when bDebugAIState. */
    void LogAIState(APawn* OwnerPawn);

    /**
     * Verifies every key in GothicBBKeys exists on the running Blackboard asset
     * with the expected type. Called on possess, before any writes.
     *
     * Exists because UBlackboardComponent::SetValueAs* fails silently on a missing
     * or mistyped key — no warning, no error, no effect. BB_BestialLucid shipped
     * without bIsInCombat/bCanSeeTarget and cost a night: the boss held a target,
     * saw the player at 1m, ran her tree, and never fought, because two writes
     * evaporated. This makes that a startup error instead of a mystery.
     */
    void ValidateBlackboardKeys();
};