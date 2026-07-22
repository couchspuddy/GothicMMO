// BTService_ApproachSpeed.h
// BT Service — runs while the enemy is approaching the target.
// Scales MaxWalkSpeed down linearly as the enemy enters the
// deceleration zone (ApproachDecelDistance → EngageDistance).
// Restores full speed when further than the decel threshold.
//
// Attach this service to the approach/MoveTo branch in BT_EnemyCombat.
// It reads tuning values from the owning AGothicEnemyAIController.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "GothicBTService_ApproachSpeed.generated.h"

UCLASS()
class GOTHICMMO_API UBTService_ApproachSpeed : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_ApproachSpeed();

	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};