// GothicBTService_RequestAttackToken.h
// Drives UGothicAttackTokenSubsystem from the Behavior Tree: mirrors "am I
// allowed to attack this tick" into a Blackboard bool so a decorator can gate
// the attack branch.
//
// Put this on the ATTACK branch (the composite whose children are the attack
// sequence), not the root — the token should be requested only while the tree
// is actually trying to attack, and the release on OnCeaseRelevant then fires
// the instant the enemy leaves that branch (out of range, staggered, aborted).
//
// The service never touches the attack logic itself. It only writes the bool;
// a Blackboard decorator on the attack branch reads it (Bool == true) and the
// existing tasks are unchanged. This is the same projection pattern as
// GothicBTService_CombatSync (GAS/state → observable Blackboard bool).

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GothicBTService_RequestAttackToken.generated.h"

UCLASS(meta = (DisplayName = "Gothic Request Attack Token"))
class GOTHICMMO_API UGothicBTService_RequestAttackToken : public UBTService
{
    GENERATED_BODY()

public:
    UGothicBTService_RequestAttackToken();

    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        float DeltaSeconds) override;
    virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual FString GetStaticDescription() const override;

protected:
    /**
     * Bool key the token verdict is written into. Defaults to "HasAttackToken";
     * the wiring session must create a Bool key of that name (or repoint this
     * selector). An FBlackboardKeySelector, not an FName — immune to the
     * silent-typo no-op that FName keys suffer (gotcha #2).
     */
    UPROPERTY(EditAnywhere, Category = "Attack Tokens")
    FBlackboardKeySelector HasTokenKey;

private:
    /** Writes bHasToken into HasTokenKey; logs only on a genuine change. */
    void WriteToken(UBehaviorTreeComponent& OwnerComp, bool bHasToken) const;
};
