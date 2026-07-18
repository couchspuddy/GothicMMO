// GothicBTTask_FindNearestPillar.h
// Finds the nearest WallPoundTarget-tagged actor to the pawn, regardless of
// distance, and writes its location to a Vector key. Used ONLY by the Phase
// 2 transition sequence — this is the "regardless of where she is" part of
// "she moves to the nearest pillar to do the Wall Pound regardless of where
// she is." The normal in-combat Wall Pound branch never needs this; it only
// fires when GothicBTService_NearbyTerrainCheck's bool is already true
// (she's already near one), which this task deliberately ignores — it
// searches broadly rather than checking a fixed small radius, since during
// the transition she could be anywhere in the room.
//
// Same tag GothicBTService_NearbyTerrainCheck and GA_BestialLucidWallPound
// both already use — keep all three in sync if the tag name ever changes.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GothicBTTask_FindNearestPillar.generated.h"

UCLASS(meta = (DisplayName = "Gothic Find Nearest Pillar"))
class GOTHICMMO_API UGothicBTTask_FindNearestPillar : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UGothicBTTask_FindNearestPillar();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual FString GetStaticDescription() const override;

protected:
    /** Same tag as GothicBTService_NearbyTerrainCheck / GA_BestialLucidWallPound. */
    UPROPERTY(EditAnywhere, Category = "Transition")
    FName TerrainTag = FName("WallPoundTarget");

    /**
     * Search radius. Wide on purpose — unlike the in-combat proximity check,
     * this needs to find a pillar from anywhere in the room, not just
     * confirm one's already nearby. Should comfortably cover the Rotunda's
     * full diameter.
     */
    UPROPERTY(EditAnywhere, Category = "Transition")
    float SearchRadius = 2000.f;

    /** Where the found pillar's location gets written. Bind a Move To after this task to this key. */
    UPROPERTY(EditAnywhere, Category = "Transition")
    FBlackboardKeySelector OutputLocationKey;

    /**
     * How far from the pillar's own origin the computed destination sits,
     * offset back toward the pawn's current position. The pillar's raw
     * GetActorLocation() is frequently inside or right at the edge of its
     * own collision — Move To can't path onto that reliably and fails
     * silently, which is exactly what was happening here: FindNearestPillar
     * kept succeeding, Move To kept failing with no log of its own, and the
     * whole transition Sequence quietly restarted from the top. Standing
     * just outside the mesh, on the side already being approached from,
     * fixes that without needing to know anything about the pillar's shape.
     */
    UPROPERTY(EditAnywhere, Category = "Transition")
    float ApproachOffsetDistance = 150.f;
};