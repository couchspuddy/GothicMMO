// BTTask_BossCry.h
// BT Task for the Bestial Lucid's Phase 2 Cry.
//
// The Cry replaces the Roar mechanically in Phase 2:
//   1. Applies passive damage to all surviving pillars (the encounter timer)
//   2. Fires a stagger cone in front of the boss (same as Roar)
//   3. The stagger cone punishes players who don't flank
//
// The Cry fires every N attack cycles in Phase 2, controlled by
// the BT's cycle counter. This task executes instantly — the BT
// handles the pause/recovery timing after it.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossCry.generated.h"

class UGameplayEffect;

UCLASS()
class GOTHICMMO_API UBTTask_BossCry : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BossCry();

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	virtual FString GetStaticDescription() const override;

protected:
	/** Damage applied to each surviving pillar. */
	UPROPERTY(EditAnywhere, Category = "Cry")
	float CryPillarDamage = 15.f;

	/** Radius of the stagger cone check (cm). */
	UPROPERTY(EditAnywhere, Category = "Cry|Stagger")
	float StaggerRadius = 600.f;

	/** Half-angle of the stagger cone (degrees). 90 = 180 degree frontal cone. */
	UPROPERTY(EditAnywhere, Category = "Cry|Stagger")
	float StaggerConeHalfAngle = 90.f;

	/** GameplayEffect applied to players caught in the cone (stun/slow). */
	UPROPERTY(EditAnywhere, Category = "Cry|Stagger")
	TSubclassOf<UGameplayEffect> StaggerEffect;
};