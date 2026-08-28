// GothicBTTask_EvadeStep.h
// Computes a lateral sidestep point relative to the current target and writes it
// to a Vector key, for a stock BTTask_MoveTo placed after it to walk to. This is
// the movement half of the Evader affix (docs/ENEMY_AFFIXES.md): a duelist weaves
// aside instead of blocking, so pairing this behind GothicBTDecorator_HasAffix
// (Evader) — or the State.Attacking read the doc calls for — gives an enemy a
// step-off the instant the player commits.
//
// Same division of labour as GothicBTTask_ComputeRepositionPoint: this task only
// picks WHERE, and is instant (writes the point, ends immediately) — the paired
// Move To does the walking, reusing proven movement code rather than reimplementing
// move-completion tracking.
//
// Side selection and the angle/jitter roll go through FGothicDeterminism so a
// seeded measurement run replays identically; never raw FMath::Rand*.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GothicBTTask_EvadeStep.generated.h"

UCLASS(meta = (DisplayName = "Gothic Evade Step"))
class GOTHICMMO_API UGothicBTTask_EvadeStep : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UGothicBTTask_EvadeStep();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** The current target the sidestep is measured against. */
	UPROPERTY(EditAnywhere, Category = "Evade")
	FBlackboardKeySelector TargetActorKey;

	/** Where the computed point is written. Bind the paired Move To to this same key. */
	UPROPERTY(EditAnywhere, Category = "Evade")
	FBlackboardKeySelector OutputPointKey;

	/**
	 * How far (uu) the enemy steps off. Displacement from the pawn's current
	 * position, not a separation from the target.
	 *
	 * Placeholder default — the real number comes from an in-editor pass measured
	 * against the player's attack reach and the enemy's own recovery window, not
	 * from reasoning here.
	 */
	UPROPERTY(EditAnywhere, Category = "Evade", meta = (ClampMin = "0.0"))
	float StepDistance = 250.f;

	/**
	 * Angle band (degrees, off the toward-target bearing) the sidestep direction is
	 * rolled within, to either side at random. Centred near 90 so the step reads as
	 * a lateral weave: below ~60 it drifts into an approach, above ~120 into a
	 * retreat. A range rather than a fixed 90 so repeated evades don't retrace one
	 * line into a rut. Placeholder defaults — tune in-editor.
	 */
	UPROPERTY(EditAnywhere, Category = "Evade", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MinAngleOffset = 70.f;

	UPROPERTY(EditAnywhere, Category = "Evade", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxAngleOffset = 110.f;
};
