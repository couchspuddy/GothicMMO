// GothicVitalPointComponent.h
// Manages the moving vital point system for all Accursed enemies.
// Owned by GothicEnemyBase. Receives damage notifications from the enemy
// and handles shift logic, location tracking, and delegate broadcasting.
// Deliberately GAS-agnostic — the enemy base handles the GAS bridge.
// Blueprint children of each enemy type define their own vital point
// locations using bone names and local offsets, making this rig-agnostic.
//
// July 17 additions:
//   - Shimmer visual: a Niagara component attached directly to the active
//     vital's bone. The scene graph moves it with the animation for free —
//     no tick, which matters because AGothicCharacterBase disables actor tick
//     and enemy Blueprints therefore have no working Event Tick.
//   - Randomized shift destination: the next vital is rolled at random
//     (excluding the current index) and PRE-COMMITTED, so The Read predicts
//     a genuinely unknowable-but-honest future instead of index+1.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DrawDebugHelpers.h"
#include "GothicVitalPointComponent.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;

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
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

    /**
     * Cosmetic teardown on death. Destroys the shimmer and kills the shift
     * timer. Call from PlayDeathCosmetics — that path runs on EVERY machine
     * (server via OnDeath, clients via MulticastOnDeath), which is exactly
     * the set of machines that own a shimmer instance. Without this, corpses
     * glow for the full CorpseLifetime.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|VitalPoint")
    void HandleOwnerDeath();

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
     *
     * The next index is rolled at random (excluding the current one) at the
     * moment the current vital becomes active, and replicated — so The Read's
     * prediction is pre-committed truth, not a computable pattern. When the
     * vital is frozen, "next" is the current index: The Read honestly reports
     * that it isn't going anywhere.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|VitalPoint")
    FVector GetNextVitalWorldLocation() const;

    /**
     * Permanently stops the vital point from shifting, from either the
     * damage threshold or the independent timer. Used by boss phase
     * transitions (e.g. Bestial Lucid Phase 2) where the vital becomes
     * a fixed, known target. Not reversible — bosses don't un-freeze mid-fight.
     *
     * Pass a valid LockIndex to snap the vital there before freezing — Bestial
     * Lucid's Phase 2 resolves it to the heart rather than locking whatever
     * limb the last shift happened to land on. INDEX_NONE freezes in place.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|VitalPoint")
    void FreezeVitalPoint(int32 LockIndex = -1);

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

    // ── Read highlight — called by GA_Read ────────────────────────────────

    /** Activates the Read overlay on the mesh at the given world position. */
    void SetReadHighlight(const FVector& WorldPos);

    /** Clears the Read overlay. Called when GA_Read ends. */
    void ClearReadHighlight();

    /** True while GA_Read is actively highlighting the next vital. */
    bool bReadHighlightActive = false;

    // ── Delegates ────────────────────────────────────────────────────────────

    /** Fires when the vital point shifts. Subscribe to this for The Read. */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|VitalPoint")
    FOnVitalPointShifted OnVitalPointShifted;

    // ── Configuration — set in Blueprint per enemy type ───────────────────────

    /**
     * All possible vital point locations for this enemy.
     * Define these in the Blueprint child using bone names from your rig.
     * Minimum 2 recommended — with only 1 the point never shifts.
     * 4+ is where the randomized shift starts earning its keep — with 2 the
     * "random" destination is always the other one.
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

    /** Editor only — draws the actual hit volume so it can be compared against the shimmer. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Gothic|VitalPoint")
    bool bDebugDrawVital = false;

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

    // ── Shimmer — the vital's player-visible tell ─────────────────────────────

    /**
     * Niagara system rendered at the active vital point on every machine with
     * a screen (never spawned on a dedicated server). Assign the same asset
     * BP_GA_Read uses for ReadIndicatorSystem, distinguished by color/scale
     * below — the Read indicator and the shimmer should read as the same
     * visual language, because The Read is literally showing a future shimmer.
     *
     * Keep it small and faint (~HitDetectionRadius so the feedback is honest).
     * It's a wound, not a waypoint.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|VitalPoint|Shimmer")
    TObjectPtr<UNiagaraSystem> VitalShimmerSystem;

    /**
     * Niagara user parameter to tint. Leave None to skip tinting.
     * Most VFX pack systems expose "Color" — check the asset's User Parameters.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|VitalPoint|Shimmer")
    FName ShimmerColorParameter = FName("Color");

    /** Shimmer tint. Dimmer than the Read indicator — current vital = quiet. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|VitalPoint|Shimmer")
    FLinearColor ShimmerColor = FLinearColor(0.85f, 0.75f, 0.4f, 0.6f);

    /** Uniform scale applied to the spawned shimmer component. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|VitalPoint|Shimmer")
    float ShimmerScale = 1.f;

    // ── Material overlay — mesh-integrated vital highlight ────────────────────
    //
    // The material-based approach draws the vital glow ON the mesh surface
    // rather than as a floating particle. Requires two Vector parameters in
    // the enemy's master material:
    //   - VitalPointWorldPos  → current vital location (warm amber glow)
    //   - ReadPointWorldPos   → predicted next vital from The Read (cooler tint)
    //
    // The shader samples distance from each pixel's AbsoluteWorldPosition to
    // the parameter and creates emissive output within the radius. Both default
    // to (0,0,-99999) = off-world = no visible glow.

    /** Material parameter name for the current vital world position. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|VitalPoint|Material")
    FName VitalPosParamName = FName("VitalPointWorldPos");

    /** Material parameter name for The Read's predicted position. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|VitalPoint|Material")
    FName ReadPosParamName = FName("ReadPointWorldPos");

    /**
     * Base material for the vital overlay. Create a simple translucent
     * emissive material (M_VitalOverlay) and assign it here in the
     * enemy Blueprint's VitalPointComponent. The C++ creates a DMI from
     * it and applies it via SetOverlayMaterial — no editing of
     * existing character materials needed.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|VitalPoint|Material")
    TObjectPtr<UMaterialInterface> VitalOverlayMaterial;

protected:
    /** Skeletal mesh reference cached on BeginPlay for bone queries. */
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> CachedMesh;

    /** Index into VitalPointLocations of the currently active vital point. */
    UPROPERTY(ReplicatedUsing = OnRep_ActiveVitalIndex)
    int32 ActiveVitalIndex = 0;

    /**
     * The pre-committed destination of the NEXT shift. Rolled on the server
     * whenever a vital becomes active; replicated so The Read's client-side
     * query returns the same answer the server will act on.
     *
     * Replicates in the same actor bunch as ActiveVitalIndex, so in practice
     * they arrive together — but if a frame ever shows a one-tick-stale Next
     * on a remote client, this is where to look.
     */
    UPROPERTY(Replicated)
    int32 NextVitalIndex = 0;

    /** Damage accumulated since the last shift. Reset on shift. */
    float AccumulatedDamage = 0.f;

    /** Timer handle for the independent shift timer (Lucid/Boss tier). */
    FTimerHandle ShiftTimerHandle;

    /** Once true, ShiftVitalPoint() becomes a no-op regardless of cause. */
    bool bIsFrozen = false;

    /** The live shimmer instance. Null on dedicated servers by design. */
    UPROPERTY()
    TObjectPtr<UNiagaraComponent> ShimmerComponent;

    /** Dynamic Material Instances created for the mesh overlay glow. */
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> OverlayDMI;

    // ── Internal ─────────────────────────────────────────────────────────────

    /** Moves the active vital to the pre-committed NextVitalIndex and rolls a new one. */
    void ShiftVitalPoint();

    void RollNextVitalIndex();
    FVector ComputeWorldLocation(int32 Index) const;
    void OnShiftTimerFired();
    void SpawnShimmer();
    void UpdateShimmerAttachment();

    /** Creates Dynamic Material Instances for all mesh slots. */
    void CreateVitalMaterials();

    /** Updates the VitalPointWorldPos parameter on all DMIs. Called every tick. */
    void UpdateVitalMaterialPosition();

    /** Replication callback — clients update visuals when index changes. */
    UFUNCTION()
    void OnRep_ActiveVitalIndex();

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};