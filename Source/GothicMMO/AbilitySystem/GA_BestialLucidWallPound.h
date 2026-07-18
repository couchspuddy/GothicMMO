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
};