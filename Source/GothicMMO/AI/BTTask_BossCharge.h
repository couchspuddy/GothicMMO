// BTTask_BossCharge.h
// BT Task for the Bestial Lucid sweeping charge.
//
// Sequence:
//   1. Locks charge direction toward current target position
//   2. Boss moves at ChargeSpeed in that direction (no re-tracking)
//   3. On pillar impact: damages pillar, boss staggers (punish window)
//   4. On wall impact: boss staggers (no structural damage)
//   5. On max range reached: brief recovery
//
// The hitbox component handles player damage during the charge
// via the charge montage's anim notify state. This task only
// handles movement and environmental collision.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossCharge.generated.h"

class AGothicBossArenaManager;

UCLASS()
class GOTHICMMO_API UBTTask_BossCharge : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_BossCharge();

    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

    virtual void TickTask(
        UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        float DeltaSeconds) override;

    virtual uint16 GetInstanceMemorySize() const override;
    virtual FString GetStaticDescription() const override;

protected:
    /** Speed during the charge (cm/s). */
    UPROPERTY(EditAnywhere, Category = "Charge")
    float ChargeSpeed = 1200.f;

    /** Maximum distance before the charge ends if nothing is hit (cm). */
    UPROPERTY(EditAnywhere, Category = "Charge")
    float MaxChargeDistance = 1500.f;

    /** Damage dealt to a pillar on impact. */
    UPROPERTY(EditAnywhere, Category = "Charge")
    float PillarImpactDamage = 40.f;

    /** How close to a pillar counts as an impact (cm). */
    UPROPERTY(EditAnywhere, Category = "Charge")
    float PillarImpactRadius = 150.f;

    /** Forward trace distance for wall detection (cm). */
    UPROPERTY(EditAnywhere, Category = "Charge")
    float WallTraceDistance = 120.f;

    /** Stagger duration after impact — the punish window (seconds). */
    UPROPERTY(EditAnywhere, Category = "Charge")
    float StaggerDuration = 2.0f;
};

struct FBTBossChargeMemory
{
    FVector ChargeDirection;
    FVector ChargeStartLocation;
    float DistanceTraveled;
    float DefaultWalkSpeed;
    bool bImpacted;
};