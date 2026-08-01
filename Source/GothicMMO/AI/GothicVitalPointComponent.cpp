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
        DrawDebugSphere(GetWorld(), GetCurrentVitalWorldLocation(),
            HitDetectionRadius, 12, FColor::Yellow, false, -1.f, 0, 0.5f);
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

    const FVector NewLocation = ComputeWorldLocation(ActiveVitalIndex);


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

FVector UGothicVitalPointComponent::GetCurrentVitalWorldLocation() const
{
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
    const float EffectiveRadius = FMath::Max(0.f, HitDetectionRadius + BonusRadius);

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
             "(HitDetectionRadius=%.1f + BonusRadius=%.1f) => %s"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"),
        ActiveVitalIndex,
        *HitWorldLocation.ToCompactString(),
        *CurrentLocation.ToCompactString(),
        Distance,
        EffectiveRadius,
        HitDetectionRadius,
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
    const FVector NewLocation = ComputeWorldLocation(ActiveVitalIndex);
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

        const FVector NewLocation = ComputeWorldLocation(ActiveVitalIndex);


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
    ShimmerComponent->SetWorldScale3D(FVector(ShimmerScale));

    UpdateShimmerAttachment();
}

void UGothicVitalPointComponent::UpdateShimmerAttachment()
{
    if (!ShimmerComponent || !VitalPointLocations.IsValidIndex(ActiveVitalIndex))
    {
        return;
    }

    const FVitalPointLocation& VPL = VitalPointLocations[ActiveVitalIndex];

    if (CachedMesh && VPL.BoneName != NAME_None)
    {
        // Bones are valid attachment sockets. From here the animation moves the
        // shimmer — zero per-frame code. SetRelativeLocation is in bone space,
        // which matches ComputeWorldLocation's TransformVector(LocalOffset), so
        // the visual and the hit check agree by construction.
        ShimmerComponent->AttachToComponent(CachedMesh,
            FAttachmentTransformRules::SnapToTargetNotIncludingScale, VPL.BoneName);
        ShimmerComponent->SetRelativeLocation(VPL.LocalOffset);
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