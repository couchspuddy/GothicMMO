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
    static const FName TargetActor    = TEXT("TargetActor");      // AActor*
    static const FName TargetLocation = TEXT("TargetLocation");   // FVector
    static const FName bCanSeeTarget  = TEXT("bCanSeeTarget");    // bool
    static const FName bIsInCombat    = TEXT("bIsInCombat");      // bool
    static const FName PatrolOrigin   = TEXT("PatrolOrigin");     // FVector
    static const FName AttackRange    = TEXT("AttackRange");      // float
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

private:
    /** Cached patrol spawn point — enemy returns here when leash breaks. */
    FVector PatrolOrigin;

    /** Periodic check to see if the target escaped the leash range. */
    FTimerHandle LeashCheckTimer;
    void CheckLeash();
};
