// GothicBTService_NearbyTerrainCheck.h
// Single job: is a tagged wall/pillar within range of this pawn right now.
// Feeds Wall Pound's skip condition — "avoid the walls" only teaches the
// player anything if good positioning visibly costs her a cycle, so this
// needs to be a plain, honest proximity check, not folded into an unrelated
// service.
//
// Deliberately its own service rather than a bolt-on to GothicBTService_
// CombatSync: CombatSync's job is ability readiness and target-relative
// state — this is a static-geometry query with nothing to do with the
// target. Same "one service, one job" principle as WeightedActionSelect;
// stacking unrelated concerns into one class is exactly what this morning's
// architecture pass exists to catch.
//
// Writes a plain Bool. No new decorator class needed — the tree already
// uses stock BTDecorator_Blackboard everywhere (bChargeReady, bInAttackRange,
// etc.), and this bool slots into that same pattern.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GothicBTService_NearbyTerrainCheck.generated.h"

UCLASS(meta = (DisplayName = "Gothic Nearby Terrain Check"))
class GOTHICMMO_API UGothicBTService_NearbyTerrainCheck : public UBTService
{
    GENERATED_BODY()

public:
    UGothicBTService_NearbyTerrainCheck();

    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        float DeltaSeconds) override;
    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual FString GetStaticDescription() const override;

protected:
    /**
     * Actor tag marking valid Wall Pound targets. Tag the specific walls and
     * pillars in the Rotunda meant to be slammable — deliberately opt-in
     * rather than "any static geometry," since that would also catch floor
     * and ceiling collision with no way to exclude them cleanly.
     */
    UPROPERTY(EditAnywhere, Category = "Terrain Check")
    FName TerrainTag = FName("WallPoundTarget");

    /** How close a tagged actor needs to be to count. Tune against the Rotunda's actual scale in engine. */
    UPROPERTY(EditAnywhere, Category = "Terrain Check")
    float CheckRadius = 400.f;

    /** Bool key written each tick. */
    UPROPERTY(EditAnywhere, Category = "Terrain Check")
    FBlackboardKeySelector ResultKey;
};