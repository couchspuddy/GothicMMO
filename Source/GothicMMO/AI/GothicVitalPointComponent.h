// GothicVitalPointComponent.h
// Manages the moving vital point system for all Accursed enemies.
// Owned by GothicEnemyBase. Receives damage notifications from the enemy
// and handles shift logic, location tracking, and delegate broadcasting.
// Deliberately GAS-agnostic — the enemy base handles the GAS bridge.
// Blueprint children of each enemy type define their own vital point
// locations using bone names and local offsets, making this rig-agnostic.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GothicVitalPointComponent.generated.h"

// ── Structs ──────────────────────────────────────────────────────────────────

/**
 * One possible vital point location on an enemy.
 * Defined per enemy Blueprint using bone name + local offset.
 * Rig-agnostic — swapping the skeletal mesh only requires
 * updating these entries in the Blueprint, not touching C++.
 */
USTRUCT(BlueprintType)
struct FVitalPointLocation
{
    GENERATED_BODY()

    /** The bone to anchor this vital point to. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vital Point")
    FName BoneName = NAME_None;

    /**
     * Local offset from the bone's world position.
     * Use this to position the shimmer on the surface of the mesh
     * rather than at the bone's exact origin.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vital Point")
    FVector LocalOffset = FVector::ZeroVector;
};

// ── Delegates ─────────────────────────────────────────────────────────────────

/**
 * Broadcast when the vital point shifts to a new location.
 * NewIndex = the index into VitalPointLocations of the new active point.
 * NewWorldLocation = the world position of the new vital point.
 * Used by The Read ability to predict the next location.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnVitalPointShifted,
    int32, NewIndex,
    FVector, NewWorldLocation);

// ── Component ─────────────────────────────────────────────────────────────────

UCLASS(ClassGroup=(Gothic), meta=(BlueprintSpawnableComponent))
class GOTHICMMO_API UGothicVitalPointComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGothicVitalPointComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // ── Called by GothicEnemyBase ────────────────────────────────────────────

    /**
     * Called by GothicEnemyBase when damage is applied to this enemy.
     * Accumulates damage and triggers a shift when the threshold is reached.
     * Server only — shift logic is authoritative.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|VitalPoint")
    void NotifyDamageTaken(float DamageAmount);

    // ── Accessors ────────────────────────────────────────────────────────────

    /**
     * Returns the current world position of the active vital point.
     * Computed each frame from the bone transform + offset.
     * Valid on all machines — location is replicated via ActiveVitalIndex.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|VitalPoint")
    FVector GetCurrentVitalWorldLocation() const;

    /**
     * Returns the world position of the NEXT vital point before it shifts.
     * This is what The Read ability exposes to the Hunter.
     * Returns zero vector if only one vital point is defined.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|VitalPoint")
    FVector GetNextVitalWorldLocation() const;

    /**
     * Returns true if the given world position is close enough to the
     * current vital point to count as a vital hit.
     * Called by the damage pipeline to determine bonus damage.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|VitalPoint")
    bool IsVitalPointHit(const FVector& HitWorldLocation) const;

    /** Returns the current active vital point index. */
    UFUNCTION(BlueprintPure, Category = "Gothic|VitalPoint")
    int32 GetActiveVitalIndex() const { return ActiveVitalIndex; }

    // ── Delegates ────────────────────────────────────────────────────────────

    /** Fires when the vital point shifts. Subscribe to this for The Read. */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|VitalPoint")
    FOnVitalPointShifted OnVitalPointShifted;

    // ── Configuration — set in Blueprint per enemy type ───────────────────────

    /**
     * All possible vital point locations for this enemy.
     * Define these in the Blueprint child using bone names from your rig.
     * Minimum 2 recommended — with only 1 the point never shifts.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|VitalPoint")
    TArray<FVitalPointLocation> VitalPointLocations;

    /**
     * Damage accumulated to the current vital point before it shifts.
     * Scale this down per enemy tier in Blueprint:
     * Thrall = high (150+), Retained = medium (80), Lucid = low (40), Boss = very low + timer
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|VitalPoint")
    float ShiftThreshold = 100.f;

    /**
     * How close a hit needs to be to count as a vital point hit (cm).
     * The shimmer visual should match this radius so the feedback is honest.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|VitalPoint")
    float HitDetectionRadius = 30.f;

    /**
     * If true, the vital point also shifts on an independent timer
     * in addition to the damage threshold.
     * Set true on Lucid and Boss tier enemies.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|VitalPoint")
    bool bShiftOnTimer = false;

    /**
     * Interval in seconds for the timer-based shift.
     * Only relevant if bShiftOnTimer is true.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|VitalPoint",
        meta = (EditCondition = "bShiftOnTimer"))
    float ShiftTimerInterval = 5.f;

protected:
    /** Skeletal mesh reference cached on BeginPlay for bone queries. */
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> CachedMesh;

    /** Index into VitalPointLocations of the currently active vital point. */
    UPROPERTY(ReplicatedUsing = OnRep_ActiveVitalIndex)
    int32 ActiveVitalIndex = 0;

    /** Damage accumulated since the last shift. Reset on shift. */
    float AccumulatedDamage = 0.f;

    /** Timer handle for the independent shift timer (Lucid/Boss tier). */
    FTimerHandle ShiftTimerHandle;

    // ── Internal ─────────────────────────────────────────────────────────────

    /** Selects the next vital point index. Currently sequential, 
     *  can be made random by swapping the implementation. */
    void ShiftVitalPoint();

    /** Computes world position for a given vital point index. */
    FVector ComputeWorldLocation(int32 Index) const;

    /** Timer callback for independent shift. */
    void OnShiftTimerFired();

    /** Replication callback — clients update visuals when index changes. */
    UFUNCTION()
    void OnRep_ActiveVitalIndex();

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};