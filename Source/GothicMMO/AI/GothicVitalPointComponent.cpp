// GothicVitalPointComponent.cpp

#include "AI/GothicVitalPointComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "GothicMMO.h"

UGothicVitalPointComponent::UGothicVitalPointComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(true);
}

void UGothicVitalPointComponent::BeginPlay()
{
    Super::BeginPlay();

    // Cache the skeletal mesh from the owning character
    if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
    {
        CachedMesh = OwnerChar->GetMesh();
    }

    if (!CachedMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicVitalPointComponent: No skeletal mesh found on %s"),
            *GetOwner()->GetName());
    }

    // Validate that we have at least one vital point defined
    if (VitalPointLocations.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicVitalPointComponent: No vital point locations defined on %s — assign in Blueprint"),
            *GetOwner()->GetName());
        return;
    }

    // Server pre-commits the first shift destination so The Read has a real
    // answer from frame one. Clients receive it via replication.
    if (GetOwner()->HasAuthority())
    {
        RollNextVitalIndex();
    }

    // The shimmer is cosmetic — every machine with a screen spawns its own,
    // a dedicated server never does. Standalone and listen hosts count as
    // "machines with a screen."
    if (GetOwner()->GetNetMode() != NM_DedicatedServer)
    {
        SpawnShimmer();
        CreateVitalMaterials();
    }

    // Start the independent timer if configured
    // Only runs on server — shift logic is authoritative
    if (bShiftOnTimer && GetOwner()->HasAuthority())
    {
        GetWorld()->GetTimerManager().SetTimer(
            ShiftTimerHandle,
            this,
            &UGothicVitalPointComponent::OnShiftTimerFired,
            ShiftTimerInterval,
            true); // looping
    }
}

void UGothicVitalPointComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Defensive teardown — HandleOwnerDeath is the intended path, but if the
    // actor is destroyed without dying (level transition, encounter cleanup)
    // the shimmer must not outlive its parent.
    if (ShimmerComponent)
    {
        ShimmerComponent->DestroyComponent();
        ShimmerComponent = nullptr;
    }

    Super::EndPlay(EndPlayReason);
}

void UGothicVitalPointComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Update the material overlay position every frame so the glow
    // follows the bone through animation
    if (OverlayDMI)
    {
        // The overlay's only job is "shoot here" -- it tracks the CURRENT vital
        // and nothing else. It used to also paint the predicted next vital via
        // ReadPointWorldPos, from when The Read was a telegraph; that path had
        // no callers left after the redesign, so the parameter sat off-world
        // permanently. Removed along with the per-frame Warning log that sat
        // inside this branch and would have spammed every tick had it ever run.
        UpdateVitalMaterialPosition();
    }

#if WITH_EDITOR
    if (bDebugDrawVital && VitalPointLocations.Num() > 0 && GetWorld())
    {
        // Effective, not authored — this draw's whole purpose is to show the
        // volume hit detection actually uses, and on a scaled enemy those differ.
        DrawDebugSphere(GetWorld(), GetCurrentVitalWorldLocation(),
            GetEffectiveHitRadius(), 12, FColor::Yellow, false, -1.f, 0, 0.5f);
    }
#endif
}

void UGothicVitalPointComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UGothicVitalPointComponent, ActiveVitalIndex);
    DOREPLIFETIME(UGothicVitalPointComponent, NextVitalIndex);
}

void UGothicVitalPointComponent::NotifyDamageTaken(float DamageAmount)
{
    // Server only — clients never drive shift logic
    if (!GetOwner()->HasAuthority())
    {
        return;
    }

    if (VitalPointLocations.Num() <= 1)
    {
        return;
    }

    AccumulatedDamage += DamageAmount;


    if (AccumulatedDamage >= ShiftThreshold)
    {
        ShiftVitalPoint();
    }
}

void UGothicVitalPointComponent::ShiftVitalPoint()
{
    if (bIsFrozen)
    {
        return;
    }
    if (VitalPointLocations.Num() <= 1)
    {
        return;
    }

    // Move to the pre-committed destination, then roll the NEXT one BEFORE
    // broadcasting, so no listener can observe a NextVitalIndex that still points
    // at the vital that just became current.
    //
    // No listener currently observes it — the Read/telegraph path was removed in
    // the redesign, and its getter has now been removed too. The ordering is kept
    // because it is the invariant a pre-committed prediction is supposed to have,
    // so a future Read reads correct state on the frame it subscribes, not
    // because anything depends on it today.
    ActiveVitalIndex  = NextVitalIndex;
    AccumulatedDamage = 0.f;
    RollNextVitalIndex();

    // Projected, not raw — this is the shift that refreshes the projection cache
    // and emits the VigilTimeline line, and listeners must receive the same
    // position hit detection will use.
    const FVector NewLocation = GetCurrentVitalWorldLocation();


    // Broadcast so The Read ability and any other listeners know
    OnVitalPointShifted.Broadcast(ActiveVitalIndex, NewLocation);

    // The server's own shimmer (standalone / listen host) follows here;
    // remote clients follow via OnRep_ActiveVitalIndex.
    UpdateShimmerAttachment();

    // OnRep will fire automatically on clients via replication
}

void UGothicVitalPointComponent::RollNextVitalIndex()
{
    const int32 Num = VitalPointLocations.Num();

    if (bIsFrozen || Num <= 1)
    {
        NextVitalIndex = ActiveVitalIndex;
        return;
    }

    // Uniform over [0, Num) excluding ActiveVitalIndex: roll into a range one
    // smaller, then step over the active index. No reroll loop, no bias.
    int32 Roll = FMath::RandRange(0, Num - 2);
    if (Roll >= ActiveVitalIndex)
    {
        ++Roll;
    }
    NextVitalIndex = Roll;
}

FVector UGothicVitalPointComponent::ComputeWorldLocation(int32 Index) const
{
    if (!CachedMesh || !VitalPointLocations.IsValidIndex(Index))
    {
        return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
    }

    const FVitalPointLocation& VPL = VitalPointLocations[Index];

    if (VPL.BoneName == NAME_None)
    {
        return GetOwner()->GetActorLocation() + VPL.LocalOffset;
    }

    // Get the bone's world transform and apply the local offset
    const FTransform BoneTransform = CachedMesh->GetBoneTransform(
        CachedMesh->GetBoneIndex(VPL.BoneName));

    return BoneTransform.GetLocation() + BoneTransform.TransformVector(VPL.LocalOffset);
}

float UGothicVitalPointComponent::GetMeshWorldScale() const
{
    if (!CachedMesh)
    {
        return 1.f;
    }

    // Largest axis, not the average — see the header for why erring large is the
    // safe direction. GetComponentScale is the world scale, so it already folds
    // in the capsule's scale as well as the mesh child's (on the Bestial Lucid
    // that product is 1.5 x 2.67 = 4.005, and 30cm on a 4x body is a pinhead).
    return FMath::Max(CachedMesh->GetComponentScale().GetAbsMax(), KINDA_SMALL_NUMBER);
}

float UGothicVitalPointComponent::GetEffectiveHitRadius() const
{
    return HitDetectionRadius * GetMeshWorldScale();
}

void UGothicVitalPointComponent::RefreshVitalProjection() const
{
    ProjectionCacheIndex     = ActiveVitalIndex;
    bProjectionCacheValid    = false;
    ProjectedBoneSpaceOffset = FVector::ZeroVector;

    if (!bProjectVitalToSurface || !CachedMesh || !GetOwner())
    {
        return;
    }
    if (!VitalPointLocations.IsValidIndex(ActiveVitalIndex))
    {
        return;
    }

    const FVitalPointLocation& VPL = VitalPointLocations[ActiveVitalIndex];

    const int32 BoneIndex = CachedMesh->GetBoneIndex(VPL.BoneName);
    if (VPL.BoneName == NAME_None || BoneIndex == INDEX_NONE)
    {
        // Actor-relative fallback points have no bone to project along or cache
        // against; they keep the raw path.
        return;
    }

    const FTransform BoneTransform = CachedMesh->GetBoneTransform(BoneIndex);
    const FVector    BoneOrigin    = BoneTransform.GetLocation();
    const FVector    RawPoint      = BoneOrigin + BoneTransform.TransformVector(VPL.LocalOffset);

    const float MeshScale = GetMeshWorldScale();

    // The outward direction is "away from the actor's vertical axis, through the
    // raw point". On a buried point (offset zero, the Lucid) that pushes the
    // vital out to the nearest flank rather than an arbitrary face; on an
    // off-body point (the authored +-50cm offsets, the Thrall) it is simply the
    // direction the author pushed it, so the surface we find is the one they
    // were aiming past.
    //
    // Note this construction is purely HORIZONTAL — AxisPoint borrows the raw
    // point's own Z — so the projected point always came back at exactly the raw
    // point's height. That is fine for a flank and useless for a spine bone,
    // whose raw point sits within a couple of centimetres of the actor axis: the
    // direction degenerates, the forward fallback fires, and that single ray
    // missed the physics body on 100% of attempts. Five of fourteen authored
    // indices bailed deterministically that way (Thrall 3/4/7, Feral 1, Lucid 1),
    // while index 5 on the SAME bone succeeded 61/64 purely because its authored
    // offset pushed it off-axis. So the direction is now a candidate LIST, not a
    // single guess.
    const FVector AxisPoint = FVector(GetOwner()->GetActorLocation().X,
                                      GetOwner()->GetActorLocation().Y,
                                      RawPoint.Z);
    const FVector OutwardDir = (RawPoint - AxisPoint).GetSafeNormal();

    const FVector Forward = GetOwner()->GetActorForwardVector();
    const FVector Right   = GetOwner()->GetActorRightVector();

    // The primary is the horizontal outward direction when it is real; otherwise
    // forward, because the player shoots from the front. Everything after index 0
    // is fallback and only ever runs when the primary found nothing.
    const FVector PrimaryDir = OutwardDir.IsNearlyZero() ? Forward : OutwardDir;

    // Ordered best-guess-first: the primary, then a radial sweep around the actor
    // axis at the raw point's height (front, flanks, back — the order a player is
    // most likely to be standing in), then two vertically-angled rays for a bone
    // whose only nearby collision body is above or below it, which no horizontal
    // ray can ever reach. Eight worst-case LineTraceComponent calls, once per
    // shift (~5s) per enemy; the cost does not register.
    TArray<FVector, TInlineAllocator<8>> Directions;
    Directions.Add(PrimaryDir);
    Directions.Add(Forward);
    Directions.Add(Right);
    Directions.Add(-Right);
    Directions.Add(-Forward);
    Directions.Add((PrimaryDir + FVector::UpVector).GetSafeNormal());
    Directions.Add((PrimaryDir - FVector::UpVector).GetSafeNormal());

    // Start outside the hull and trace INWARD to the bone origin. The first
    // blocking hit is the surface crossing on the way in, which pulls an off-body
    // point back onto the body and lifts a buried point out to the skin.
    //
    // GetClosestPointOnCollision was the obvious first candidate and is the wrong
    // tool: it returns 0 with no usable point when the query point is INSIDE the
    // body, which is exactly the Lucid case this exists for.
    //
    // LineTraceComponent hits only this mesh, so no channel is involved — the
    // ECC_Weapon setup on enemy meshes is irrelevant here, and a mesh whose
    // collision is disabled for world queries still projects correctly. On a
    // skeletal mesh this resolves against the physics asset's bodies unless
    // per-poly collision is on; the physics asset is a capsule approximation of
    // the silhouette, which is both cheaper and steadier than the render mesh.
    const float StartDistance = FMath::Max(MaxSurfaceProjectionDistance * MeshScale,
                                           FVector::Dist(BoneOrigin, RawPoint) + 100.f * MeshScale);

    const FVector TraceEnd = BoneOrigin;

    FCollisionQueryParams Params(FName(TEXT("VitalSurfaceProjection")), /*bTraceComplex=*/ false);

    FHitResult BestHit;
    bool  bHit          = false;
    float BestDistToRaw = MAX_flt;
    int32 RaysTried     = 0;

    for (int32 DirIndex = 0; DirIndex < Directions.Num(); ++DirIndex)
    {
        ++RaysTried;

        FHitResult Hit;
        if (CachedMesh->LineTraceComponent(
                Hit, BoneOrigin + Directions[DirIndex] * StartDistance, TraceEnd, Params))
        {
            // Nearest to the RAW point wins, not nearest to the bone: the raw
            // point is where the author said the vital is, so the surface closest
            // to it is the one that honours the authoring.
            const float DistToRaw = FVector::Dist(Hit.ImpactPoint, RawPoint);
            if (!bHit || DistToRaw < BestDistToRaw)
            {
                BestHit       = Hit;
                BestDistToRaw = DistToRaw;
                bHit          = true;
            }
        }

        // The primary direction is the pre-fan behaviour and it already works on
        // nine of the fourteen indices. When it hits, stop: the common case stays
        // exactly one ray and cannot regress. The fan is a fallback, not a survey.
        if (bHit && DirIndex == 0)
        {
            break;
        }
    }

    if (!bHit)
    {
        // No collision anywhere around this bone — a physics asset with no body
        // for this limb, most often. With the fan in place this should be rare
        // and genuinely means "no surface", where before it also meant "one
        // unlucky ray". Verbose, not Warning: it is a data gap to look up when
        // someone reports an unhittable vital, not a per-frame alarm.
        // (LogTemp is clamped to Warning project-wide and would swallow it.)
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VitalProjection: %s vital[%d] bone=%s found no collision surface after %d rays "
                 "— keeping the raw point"),
            *GetOwner()->GetName(), ActiveVitalIndex, *VPL.BoneName.ToString(), RaysTried);
        return;
    }

    // Sink a little way in from the surface along the ray, capped so a thin limb
    // can never invert the point past its own bone.
    const FVector SurfacePoint = BestHit.ImpactPoint;
    const FVector InwardDir    = (BoneOrigin - SurfacePoint).GetSafeNormal();
    const float   Inset        = FMath::Min(SurfaceInsetDepth * MeshScale,
                                            FVector::Dist(SurfacePoint, BoneOrigin) * 0.5f);

    const FVector ProjectedPoint = SurfacePoint + InwardDir * Inset;

    ProjectedBoneSpaceOffset = BoneTransform.InverseTransformPosition(ProjectedPoint);
    bProjectionCacheValid    = true;

    // One line per shift. The raw-vs-projected delta is the whole measurement:
    // it is how far the vital was off the body before this fix, per enemy, per
    // index — which turns the next "I can't hit the vital" report into a grep.
    //
    // Standard VigilTimeline shape (t, actor, system, event, then fields): the
    // original spelling dropped the `t=` stamp and could not be correlated
    // against a scenario timeline at all. `rays=` is the fan's cost meter — a 1
    // is the primary direction working, anything higher is a bone that needed the
    // fallback and is worth an authored offset.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|VitalProjection|REFRESH|index=%d|bone=%s|raw=%s|projected=%s|")
        TEXT("depth=%.1f|radius=%.1f|rays=%d"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
        *GetOwner()->GetName(),
        ActiveVitalIndex,
        *VPL.BoneName.ToString(),
        *RawPoint.ToCompactString(),
        *ProjectedPoint.ToCompactString(),
        FVector::Dist(RawPoint, ProjectedPoint),
        GetEffectiveHitRadius(),
        RaysTried);
}

FVector UGothicVitalPointComponent::GetCurrentVitalWorldLocation() const
{
    // Refresh only when the vital has actually moved. Everything else — every
    // tick of the overlay, every shot — rides the cache.
    if (ProjectionCacheIndex != ActiveVitalIndex)
    {
        RefreshVitalProjection();
    }

    if (bProjectionCacheValid && CachedMesh && VitalPointLocations.IsValidIndex(ActiveVitalIndex))
    {
        const int32 BoneIndex = CachedMesh->GetBoneIndex(VitalPointLocations[ActiveVitalIndex].BoneName);
        if (BoneIndex != INDEX_NONE)
        {
            return CachedMesh->GetBoneTransform(BoneIndex).TransformPosition(ProjectedBoneSpaceOffset);
        }
    }

    return ComputeWorldLocation(ActiveVitalIndex);
}

bool UGothicVitalPointComponent::IsVitalPointHit(const FVector& HitWorldLocation, float BonusRadius) const
{
    const FVector CurrentLocation = GetCurrentVitalWorldLocation();
    const float Distance = FVector::Dist(HitWorldLocation, CurrentLocation);

    // BonusRadius comes from the *shooter's* VitalPointRadius secondary stat.
    // The radius itself belongs to the target's component, so the attacker's
    // contribution has to be passed in rather than read here — this component
    // has no idea who is shooting it.
    // GetEffectiveHitRadius() is the authored radius scaled by the mesh's world
    // scale, so 30cm means the same proportional target on a Thrall and on a
    // 4x Bestial Lucid. The shooter's bonus is added AFTER the scale: it is a
    // flat cm widening granted by the attacker's own stat, and scaling someone
    // else's stat by the target's size would make the same upgrade worth four
    // times more against big enemies.
    const float ScaledRadius    = GetEffectiveHitRadius();
    const float EffectiveRadius = FMath::Max(0.f, ScaledRadius + BonusRadius);

    const bool bIsHit = Distance <= EffectiveRadius;

    // Logged unconditionally because the only callers are the per-shot damage path
    // in GA_Fire and the offline combat probe — nothing ticks this. The numbers
    // matter more than the verdict: "vital: false" never explained anything, so the
    // impact point, the vital's location AT TEST TIME, the measured distance and the
    // threshold's two halves all go out on one line. BonusRadius reads 0 today
    // (VitalPointRadius is a stat nothing rolls) and seeing that zero explicitly is
    // itself the answer to "was the shooter's radius bonus applied?".
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VitalGeometry: %s vital[%d] impact=%s vital=%s dist=%.1f threshold=%.1f "
             "(ScaledRadius=%.1f + BonusRadius=%.1f) => %s"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"),
        ActiveVitalIndex,
        *HitWorldLocation.ToCompactString(),
        *CurrentLocation.ToCompactString(),
        Distance,
        EffectiveRadius,
        ScaledRadius,
        BonusRadius,
        bIsHit ? TEXT("VITAL") : TEXT("body"));

    return bIsHit;
}

void UGothicVitalPointComponent::OnShiftTimerFired()
{
    // Independent timer shift — only on server, only if alive
    if (!GetOwner()->HasAuthority())
    {
        return;
    }


    ShiftVitalPoint();
}

void UGothicVitalPointComponent::OnRep_ActiveVitalIndex()
{
    // Called on clients when ActiveVitalIndex replicates
    // Compute the new world location and broadcast so Blueprint
    // can update the shimmer visual without any additional RPC
    const FVector NewLocation = GetCurrentVitalWorldLocation();
    OnVitalPointShifted.Broadcast(ActiveVitalIndex, NewLocation);

    // The client-side shimmer follows the replicated index.
    UpdateShimmerAttachment();

}

void UGothicVitalPointComponent::FreezeVitalPoint(int32 LockIndex)
{
    if (!GetOwner()->HasAuthority())
    {
        return;
    }

    // Snap to the designated index before locking, if one was given and it's real.
    if (VitalPointLocations.IsValidIndex(LockIndex) && LockIndex != ActiveVitalIndex)
    {
        ActiveVitalIndex = LockIndex;
        AccumulatedDamage = 0.f;

        const FVector NewLocation = GetCurrentVitalWorldLocation();


        // Same pattern as ShiftVitalPoint: ActiveVitalIndex replicates and OnRep
        // covers clients, but the server has to broadcast for itself or The Read
        // and the shimmer won't follow the jump.
        OnVitalPointShifted.Broadcast(ActiveVitalIndex, NewLocation);
        UpdateShimmerAttachment();
    }

    bIsFrozen = true;

    // A frozen vital has no future. The Read now truthfully reports the
    // current location as "next" instead of pointing at a shift that will
    // never come. NextVitalIndex replicates, so client Reads agree.
    NextVitalIndex = ActiveVitalIndex;

    // Kill the timer outright rather than relying solely on the guard below —
    // no reason to let it keep firing into a no-op every ShiftTimerInterval.
    if (ShiftTimerHandle.IsValid())
    {
        GetWorld()->GetTimerManager().ClearTimer(ShiftTimerHandle);
    }

}

void UGothicVitalPointComponent::UnfreezeVitalPoint()
{
    if (!GetOwner()->HasAuthority() || !bIsFrozen)
    {
        return;
    }

    bIsFrozen = false;

    // NextVitalIndex was collapsed onto the active index by the freeze — roll a
    // real one again or The Read keeps reporting "next" as the current point.
    RollNextVitalIndex();

    if (bShiftOnTimer && !ShiftTimerHandle.IsValid())
    {
        GetWorld()->GetTimerManager().SetTimer(
            ShiftTimerHandle,
            this,
            &UGothicVitalPointComponent::OnShiftTimerFired,
            ShiftTimerInterval,
            true); // looping
    }
}

// ── Shimmer ───────────────────────────────────────────────────────────────────

void UGothicVitalPointComponent::SpawnShimmer()
{
    if (!VitalShimmerSystem)
    {
        // The shimmer is the SECOND of two independent tells. VitalOverlayMaterial
        // is the first, and it alone is enough: CreateVitalMaterials drives the
        // overlay DMI's vital-position parameter every time the point moves, so
        // the amber marker already shows players where to aim.
        //
        // Only escalate when BOTH are missing — that is the real "hidden damage
        // multiplier" defect this guard exists to catch. Warning on the shimmer
        // alone claimed the vital was invisible when it wasn't, which cost a
        // debugging session chasing a mechanic that was working.
        if (!VitalOverlayMaterial)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("VitalPoint: %s has vital points but NEITHER VitalOverlayMaterial nor VitalShimmerSystem "
                     "is assigned — the vital is genuinely invisible to players"),
                *GetOwner()->GetName());
        }
        else
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("VitalPoint: %s has no VitalShimmerSystem; falling back to the overlay material alone"),
                *GetOwner()->GetName());
        }
        return;
    }

    if (!CachedMesh || ShimmerComponent)
    {
        return;
    }

    // Spawn parented to the mesh; UpdateShimmerAttachment does the actual
    // bone targeting so spawn and shift share one code path.
    ShimmerComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
        VitalShimmerSystem,
        CachedMesh,
        NAME_None,
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        EAttachLocation::SnapToTarget,
        /*bAutoDestroy=*/ false);

    if (!ShimmerComponent)
    {
        return;
    }

    if (ShimmerColorParameter != NAME_None)
    {
        ShimmerComponent->SetVariableLinearColor(ShimmerColorParameter, ShimmerColor);
    }

    // Scale is set inside UpdateShimmerAttachment, after the attach — the attach
    // rules keep relative scale, so setting a world scale here and re-parenting
    // there is how the size silently stopped tracking the mesh.
    UpdateShimmerAttachment();
}

void UGothicVitalPointComponent::UpdateShimmerAttachment()
{
    if (!ShimmerComponent || !VitalPointLocations.IsValidIndex(ActiveVitalIndex))
    {
        return;
    }

    const FVitalPointLocation& VPL = VitalPointLocations[ActiveVitalIndex];

    // Make sure the projection for this index exists before reading it, so the
    // shimmer placed at spawn is the projected one and not a one-shift-stale raw.
    if (ProjectionCacheIndex != ActiveVitalIndex)
    {
        RefreshVitalProjection();
    }

    // The shimmer must sit where the hit volume is, not where the data says.
    const FVector ShimmerBoneSpaceOffset =
        bProjectionCacheValid ? ProjectedBoneSpaceOffset : VPL.LocalOffset;

    if (CachedMesh && VPL.BoneName != NAME_None)
    {
        // Bones are valid attachment sockets. From here the animation moves the
        // shimmer — zero per-frame code. SetRelativeLocation is in bone space,
        // which is the same space the projection cache is stored in, so the
        // visual and the hit check agree by construction.
        ShimmerComponent->AttachToComponent(CachedMesh,
            FAttachmentTransformRules::SnapToTargetNotIncludingScale, VPL.BoneName);
        ShimmerComponent->SetRelativeLocation(ShimmerBoneSpaceOffset);
        ShimmerComponent->SetRelativeRotation(FRotator::ZeroRotator);
    }
    else if (GetOwner() && GetOwner()->GetRootComponent())
    {
        // NAME_None fallback mirrors ComputeWorldLocation: actor-relative offset.
        ShimmerComponent->AttachToComponent(GetOwner()->GetRootComponent(),
            FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        ShimmerComponent->SetRelativeLocation(VPL.LocalOffset);
        ShimmerComponent->SetRelativeRotation(FRotator::ZeroRotator);
    }

    // ShimmerScale alone is a FLAT world scale, and a flat world scale is a lie
    // on any enemy that is not 1.0: on the 4.005x Bestial Lucid it drew a
    // Thrall-sized mote over a 120cm hit volume, so the indicator told the player
    // to aim at a pinhead when the real target was a beachball. Multiply by the
    // same factor GetEffectiveHitRadius uses and the apparent size tracks the
    // actual target size on every enemy, by construction. Set AFTER the attach,
    // because the attach rules preserve relative scale and would otherwise
    // reintroduce the mesh scale a second time.
    ShimmerComponent->SetWorldScale3D(FVector(ShimmerScale * GetMeshWorldScale()));
}

void UGothicVitalPointComponent::HandleOwnerDeath()
{
    if (ShimmerComponent)
    {
        ShimmerComponent->Deactivate();
        ShimmerComponent->DestroyComponent();
        ShimmerComponent = nullptr;
    }

    // Clear the mesh overlay so corpses don't glow
    if (OverlayDMI)
    {
        const FLinearColor OffWorld(0.f, 0.f, -99999.f, 0.f);
        OverlayDMI->SetVectorParameterValue(VitalPosParamName, OffWorld);
        OverlayDMI->SetVectorParameterValue(ReadPosParamName, OffWorld);
    }

    if (GetWorld() && ShiftTimerHandle.IsValid())
    {
        GetWorld()->GetTimerManager().ClearTimer(ShiftTimerHandle);
    }
}

// ── Material overlay ─────────────────────────────────────────────────────────

void UGothicVitalPointComponent::CreateVitalMaterials()
{
    if (!CachedMesh || !VitalOverlayMaterial) return;

    OverlayDMI = UMaterialInstanceDynamic::Create(VitalOverlayMaterial, this);
    if (OverlayDMI)
    {
        CachedMesh->SetOverlayMaterial(OverlayDMI);


        // Initialize — vital is live, Read defaults to off-world
        UpdateVitalMaterialPosition();
        ClearReadHighlight();
    }
}

void UGothicVitalPointComponent::UpdateVitalMaterialPosition()
{
    if (!OverlayDMI) return;

    const FVector Pos = GetCurrentVitalWorldLocation();
    OverlayDMI->SetVectorParameterValue(VitalPosParamName,
        FLinearColor(Pos.X, Pos.Y, Pos.Z, 1.f));
}

void UGothicVitalPointComponent::ClearReadHighlight()
{
    if (!OverlayDMI) return;

    OverlayDMI->SetVectorParameterValue(ReadPosParamName,
        FLinearColor(0.f, 0.f, -99999.f, 0.f));
}