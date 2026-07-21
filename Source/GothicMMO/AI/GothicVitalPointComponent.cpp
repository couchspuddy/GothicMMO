// GothicVitalPointComponent.cpp

#include "AI/GothicVitalPointComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

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
        UpdateVitalMaterialPosition();

        // Keep the Read highlight following the bone while active
        if (bReadHighlightActive)
        {
            const FVector NextPos = GetNextVitalWorldLocation();
            OverlayDMI->SetVectorParameterValue(ReadPosParamName,
                FLinearColor(NextPos.X, NextPos.Y, NextPos.Z, 1.f));
    
            UE_LOG(LogTemp, Warning, TEXT("VitalPoint TICK: ReadHighlight active | Pos=%s"), *NextPos.ToString());
        }
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

    UE_LOG(LogTemp, Log, TEXT("VitalPoint: %s accumulated %.1f / %.1f damage"),
        *GetOwner()->GetName(), AccumulatedDamage, ShiftThreshold);

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
    // broadcasting — GA_Read's shift handler immediately re-queries
    // GetNextVitalWorldLocation, so the new prediction has to exist by then.
    ActiveVitalIndex  = NextVitalIndex;
    AccumulatedDamage = 0.f;
    RollNextVitalIndex();

    const FVector NewLocation = ComputeWorldLocation(ActiveVitalIndex);

    UE_LOG(LogTemp, Log, TEXT("VitalPoint: %s shifted to index %d (next: %d) — location %s"),
        *GetOwner()->GetName(), ActiveVitalIndex, NextVitalIndex, *NewLocation.ToString());

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

FVector UGothicVitalPointComponent::GetNextVitalWorldLocation() const
{
    if (VitalPointLocations.Num() <= 1)
    {
        return FVector::ZeroVector;
    }

    return ComputeWorldLocation(NextVitalIndex);
}

bool UGothicVitalPointComponent::IsVitalPointHit(const FVector& HitWorldLocation) const
{
    const FVector CurrentLocation = GetCurrentVitalWorldLocation();
    const float Distance = FVector::Dist(HitWorldLocation, CurrentLocation);

    const int32 BoneIdx = (CachedMesh && VitalPointLocations.IsValidIndex(ActiveVitalIndex))
        ? CachedMesh->GetBoneIndex(VitalPointLocations[ActiveVitalIndex].BoneName) : -2;

    UE_LOG(LogTemp, Warning, TEXT("VitalCheck: Idx=%d Bone=%s BoneIdx=%d | Dist=%.1f Radius=%.1f | Hit=%d"),
        ActiveVitalIndex,
        VitalPointLocations.IsValidIndex(ActiveVitalIndex)
            ? *VitalPointLocations[ActiveVitalIndex].BoneName.ToString() : TEXT("BADIDX"),
        BoneIdx, Distance, HitDetectionRadius, Distance <= HitDetectionRadius ? 1 : 0);

    return Distance <= HitDetectionRadius;
}

void UGothicVitalPointComponent::OnShiftTimerFired()
{
    // Independent timer shift — only on server, only if alive
    if (!GetOwner()->HasAuthority())
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("VitalPoint: %s timer-triggered shift"),
        *GetOwner()->GetName());

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

    UE_LOG(LogTemp, Log, TEXT("VitalPoint: Client received shift — index %d"),
        ActiveVitalIndex);
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

        UE_LOG(LogTemp, Log, TEXT("VitalPoint: %s locked to index %d — location %s"),
            *GetOwner()->GetName(), ActiveVitalIndex, *NewLocation.ToString());

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

    UE_LOG(LogTemp, Log, TEXT("VitalPoint: %s frozen at index %d"),
        *GetOwner()->GetName(), ActiveVitalIndex);
}

// ── Shimmer ───────────────────────────────────────────────────────────────────

void UGothicVitalPointComponent::SpawnShimmer()
{
    if (!VitalShimmerSystem)
    {
        // Loud on purpose — an unassigned shimmer reproduces the exact
        // "hidden damage multiplier" defect this exists to close.
        UE_LOG(LogTemp, Warning,
            TEXT("VitalPoint: %s has vital points but no VitalShimmerSystem assigned — the vital is invisible to players"),
            *GetOwner()->GetName());
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

        UE_LOG(LogTemp, Log, TEXT("VitalPoint: Overlay material applied on %s"),
            *GetOwner()->GetName());

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

void UGothicVitalPointComponent::SetReadHighlight(const FVector& WorldPos)
{
    if (!OverlayDMI) return;

    bReadHighlightActive = true;
    OverlayDMI->SetVectorParameterValue(ReadPosParamName,
        FLinearColor(WorldPos.X, WorldPos.Y, WorldPos.Z, 1.f));
}

void UGothicVitalPointComponent::ClearReadHighlight()
{
    bReadHighlightActive = false;

    if (!OverlayDMI) return;

    OverlayDMI->SetVectorParameterValue(ReadPosParamName,
        FLinearColor(0.f, 0.f, -99999.f, 0.f));
}