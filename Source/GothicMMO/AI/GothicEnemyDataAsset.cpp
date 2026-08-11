// GothicEnemyDataAsset.cpp

#include "AI/GothicEnemyDataAsset.h"

UBehaviorTree* UGothicEnemyDataAsset::ResolveBehaviorTree() const
{
    // Look the tree up by this asset's own Role. A missing entry returns null,
    // which the controller treats as "fall back to my BehaviorTreeAsset" — the
    // data asset can classify an enemy without also having to own its brain.
    const TObjectPtr<UBehaviorTree>* Found = RoleBehaviorTrees.Find(Role);
    return Found ? Found->Get() : nullptr;
}
