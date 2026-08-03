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
     * Cosmetic teardown on death. Destroys the shimmer, blanks the amber overlay
     * and kills the shift timer.
     *
     * Called from AGothicEnemyBase::OnDeath_Implementation. The previous text here
     * said "call from PlayDeathCosmetics" — a function that has never existed in
     * this project — and consequently NOTHING called it at all: corpses glowed and
     * kept shifting their vital point for the full CorpseLifetime.
     *
     * NOTE the remaining gap: OnDeath is the SERVER path, so on a dedicated-server
     * build remote clients still hold a live shimmer until the corpse is destroyed.
     * Closing that needs a multicast death cosmetic hook on AGothicCharacterBase,
     * which does not exist yet. Standalone and listen-host play — everything this
     * project currently runs — is fully covered.
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

    // GetNextVitalWorldLocation() was removed here. It existed to expose the
    // pre-committed NextVitalIndex to The Read, and the Read/telegraph path was
    // removed in the redesign — the function had zero callers in C++ and zero
    // references in any Blueprint graph.
    //
    // NextVitalIndex itself is deliberately KEPT: it is live internal state that
    // ShiftVitalPoint reads on every shift, and it remains replicated so a
    // future Read is a getter away rather than a redesign. Dropping it would
    // change the component's replicated surface for no gain.

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
     * Releases a frozen vital: the point shifts again and the shift timer
     * restarts (if bShiftOnTimer). The inverse of FreezeVitalPoint, added for
     * the boss leash reset — a boss who resets to Phase 1 with her Phase 2 vital
     * still locked is a boss whose weak spot is permanently solved.
     *
     * Leaves the vital on whatever index it was frozen at; the next shift moves
     * it. Re-snapping to index 0 here would teleport the weak spot at a moment
     * nobody is looking at her.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|VitalPoint")
    void UnfreezeVitalPoint();

    /**
     * Returns true if the given world position is close enough to the
     * current vital point to count as a vital hit.
     * Called by the damage pipeline to determine bonus damage.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|VitalPoint")
    bool IsVitalPointHit(const FVector& HitWorldLocation, float BonusRadius = 0.f) const;

    /** Returns the current active vital point index. */
    UFUNCTION(BlueprintPure, Category = "Gothic|VitalPoint")
    int32 GetActiveVitalIndex() const { return ActiveVitalIndex; }

    /**
     * HitDetectionRadius scaled by the owning mesh's world scale, which is what
     * hit detection and the debug draw actually use.
     *
     * The scale factor is the LARGEST component of the mesh's world scale, not
     * the average. On a non-uniformly scaled rig the average under-scales the
     * radius along the stretched axis, which is precisely the axis on which the
     * vital is hardest to reach; and an over-generous radius still demands a hit
     * on the body, so being generous is the safe direction to be wrong in.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|VitalPoint")
    float GetEffectiveHitRadius() const;

    // ── Read highlight ────────────────────────────────────────────────────

    /**
     * Parks the overlay's Read parameter off-world so the highlight is dark.
     *
     * Its counterpart SetReadHighlight() and the bReadHighlightActive flag were
     * removed: nothing had called them since the Read redesign, so the flag was
     * write-only and the setter was an unreachable entry point sitting on a
     * material parameter. This half survives because it is genuinely called —
     * on init, to establish the off state of the overlay material.
     */
    void ClearReadHighlight();

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
     * How close a hit needs to be to count as a vital point hit (cm) on a
     * 1.0-scale enemy. The shimmer visual should match this radius so the
     * feedback is honest.
     *
     * This is the AUTHORED radius, not the effective one — it is multiplied by
     * the owning mesh's world scale before use. See GetEffectiveHitRadius().
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|VitalPoint")
    float HitDetectionRadius = 30.f;

    /**
     * Projects the computed vital position onto the owner's collision surface
     * before it is used for hit detection or the overlay glow.
     *
     * Off, the vital sits wherever bone origin + offset lands. Measured, that is
     * two distinct failures: on a 4x-scaled rig (Bestial Lucid) the bone origins
     * are buried 60-100cm inside the body, so no surface shot ever reaches them
     * and the overlay has no surface pixels near enough to glow; and where an
     * author has hand-tuned a +-50cm LocalOffset (Thrall) the point is pushed
     * clean off the body into open air. Both are the same geometry bug — the
     * vital is not ON the mesh — and one projection fixes both.
     *
     * EditAnywhere so it can be flipped on a placed actor while comparing
     * against the debug draw.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|VitalPoint|Projection")
    bool bProjectVitalToSurface = true;

    /**
     * How far inside the collision surface the projected vital is placed (cm,
     * pre-scale). A vital sitting exactly ON the surface is a coin flip for
     * grazing shots — the impact point of a trace lands a hair outside the hull
     * and measures a positive distance. A few cm of inset costs nothing and
     * makes the edge of the shimmer a real target.
     *
     * Scaled by the mesh's world scale alongside the radius, so it stays the
     * same proportional depth on every enemy.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|VitalPoint|Projection")
    float SurfaceInsetDepth = 5.f;

    /**
     * How far outside the bone the projection ray starts (cm, pre-scale). Only
     * needs to clear the body's hull along the outward direction; the ray is
     * also automatically extended past an off-body LocalOffset so a hand-authored
     * point always sits between the ray's start and the surface.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|VitalPoint|Projection")
    float MaxSurfaceProjectionDistance = 400.f;

    /**
     * Editor only — draws the actual hit volume so it can be compared against
     * the shimmer.
     *
     * EditAnywhere, not EditDefaultsOnly: a debug toggle you cannot flip on the
     * placed actor you are actually debugging is no use, and the harness's
     * set_vital_debug_draw could not set it on instances at all.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gothic|VitalPoint")
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

    // ── Surface projection cache ─────────────────────────────────────────────
    //
    // The projection is a physics query, and GetCurrentVitalWorldLocation is
    // called every tick by the overlay and again on every shot — so it is
    // computed ONCE per shift and cached, matching the cadence the vital
    // actually moves at (the ~5s timer, the damage threshold, and freeze).
    //
    // What is cached is the result in BONE SPACE, not world space. That is the
    // whole trick: the per-frame path stays a single TransformPosition, so the
    // projected vital rides the animation exactly as the raw bone+offset point
    // did, and the shimmer and the hit volume still agree. The cost is that the
    // projection reflects the pose at shift time — a limb that swings after the
    // shift carries its vital along rather than re-hugging the new silhouette,
    // which is invisible at these radii.

    /** ActiveVitalIndex the cache below was computed for. INDEX_NONE = cold. */
    mutable int32 ProjectionCacheIndex = INDEX_NONE;

    /** Projected vital in the anchor bone's space. Meaningless unless bProjectionCacheValid. */
    mutable FVector ProjectedBoneSpaceOffset = FVector::ZeroVector;

    /** False when projection was skipped or failed — callers fall back to the raw point. */
    mutable bool bProjectionCacheValid = false;

    /**
     * Recomputes the projection cache for the current ActiveVitalIndex and emits
     * the VigilTimeline telemetry line. Const + mutable state so the per-frame
     * const accessors can refresh it lazily on an index change; that lazy path
     * is the ONLY thing that drives it, so every route that moves the vital
     * (shift, freeze-snap, OnRep) is covered without each having to remember to.
     */
    void RefreshVitalProjection() const;

    /** Moves the active vital to the pre-committed NextVitalIndex and rolls a new one. */
    void ShiftVitalPoint();

    void RollNextVitalIndex();

    /** Raw bone origin + transformed offset. No surface projection. */
    FVector ComputeWorldLocation(int32 Index) const;

    /**
     * The one scale factor everything on this component sizes against: the
     * largest component of the owning mesh's WORLD scale, 1.0 when there is no
     * mesh. The hit radius, the projection distances and the shimmer all read it,
     * so on the 4.005x Bestial Lucid they cannot disagree about how big the
     * target is — which they did, the shimmer drawing Thrall-sized over a 120cm
     * hit radius.
     */
    float GetMeshWorldScale() const;
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