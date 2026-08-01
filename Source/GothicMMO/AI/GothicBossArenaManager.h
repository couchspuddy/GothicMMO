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

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GothicBossArenaManager.generated.h"

class AGothicRotundaPillar;

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

private:
    UFUNCTION()
    void OnPillarDestroyed(AGothicRotundaPillar* Pillar);
};