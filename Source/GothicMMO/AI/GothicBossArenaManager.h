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
 * The escalation curve had NO consumer of any kind: OnPillarDestroyed computed
 * the multiplier into a local and returned, and GetAggressionMultiplier had zero
 * callers project-wide — so destroying pillars changed nothing whatsoever about
 * the boss. This is the reachable half of the fix. What the multiplier should
 * actually SCALE is a design decision that has not been made (see the comment on
 * OnPillarDestroyed), so the value is broadcast where a Blueprint can act on it
 * today rather than being silently spent on a mechanic nobody chose.
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