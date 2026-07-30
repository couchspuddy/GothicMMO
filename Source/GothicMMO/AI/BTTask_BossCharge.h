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
// Player damage during the charge is applied HERE, by this task, per tick.
//
// It was originally meant to come from the melee hitbox component driven by
// the charge montage's anim notify state — but BP_GA_BestialLucid's charge has
// no montage assigned (MontageToPlay is None), so the notify state never ran,
// the hitbox was never enabled, and the boss could run straight through the
// player at 1200cm/s for zero damage. The charge was, in practice, a
// repositioning move with a pillar-breaking side effect.
//
// If a charge montage is ever authored, the notify-state path and this one
// would both be live; gate one off at that point rather than letting the
// player eat the charge twice.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossCharge.generated.h"

class AGothicBossArenaManager;
class UGameplayEffect;
class ACharacter;
struct FBTBossChargeMemory;

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

    /**
     * Raw damage sent as the Data.Damage SetByCaller when the charge connects.
     *
     * 45 raw becomes ~57 after the pipeline (+20 boss AttackPower, -8 player
     * Defense), which is roughly 24% of the player's ~242 pool. Sized to make
     * the charge the single most expensive thing the boss does that the player
     * can actually see coming, without two-shotting anyone.
     */
    UPROPERTY(EditAnywhere, Category = "Charge")
    float ChargeDamage = 45.f;

    /** Radius around the boss that counts as being run over (cm). */
    UPROPERTY(EditAnywhere, Category = "Charge")
    float ChargeHitRadius = 120.f;

    /**
     * GE used for the charge hit. Same Data.Damage SetByCaller contract as the
     * melee hitbox — assign GE_EnemyMeleeDamage (or a charge-specific variant)
     * on the BT node or the charge does nothing but move the boss.
     */
    UPROPERTY(EditAnywhere, Category = "Charge")
    TSubclassOf<UGameplayEffect> ChargeDamageEffect;

private:
    /** Overlaps pawns around the boss and damages each at most once per charge. */
    void ApplyChargeDamage(ACharacter* BossChar, FBTBossChargeMemory* Memory) const;
};

struct FBTBossChargeMemory
{
    FVector ChargeDirection;
    FVector ChargeStartLocation;
    float DistanceTraveled;
    float DefaultWalkSpeed;
    bool bImpacted;

    /**
     * Who this charge has already hit.
     *
     * Raw pointers and a fixed array on purpose: BT node memory is a zeroed
     * byte block with no constructor or destructor run over it (this node is
     * bCreateNodeInstance = false), so anything with non-trivial lifetime —
     * TSet, TArray, TWeakObjectPtr bookkeeping — is unsafe here. These are only
     * ever compared for identity, never dereferenced, so a stale pointer from a
     * destroyed actor is harmless.
     */
    static constexpr int32 MaxTrackedHits = 8;
    AActor* HitPawns[MaxTrackedHits];
    int32 NumHitPawns;
};