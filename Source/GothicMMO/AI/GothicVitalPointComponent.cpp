// GothicVitalPointComponent.cpp

#include "AI/GothicVitalPointComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
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

void UGothicVitalPointComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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

    // Advance to next index, wrapping around
    ActiveVitalIndex = (ActiveVitalIndex + 1) % VitalPointLocations.Num();
    AccumulatedDamage = 0.f;

    const FVector NewLocation = ComputeWorldLocation(ActiveVitalIndex);

    UE_LOG(LogTemp, Log, TEXT("VitalPoint: %s shifted to index %d — location %s"),
        *GetOwner()->GetName(), ActiveVitalIndex, *NewLocation.ToString());

    // Broadcast so The Read ability and any other listeners know
    OnVitalPointShifted.Broadcast(ActiveVitalIndex, NewLocation);

    // OnRep will fire automatically on clients via replication
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

    const int32 NextIndex = (ActiveVitalIndex + 1) % VitalPointLocations.Num();
    return ComputeWorldLocation(NextIndex);
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
    }

    bIsFrozen = true;

    // Kill the timer outright rather than relying solely on the guard below —
    // no reason to let it keep firing into a no-op every ShiftTimerInterval.
    if (ShiftTimerHandle.IsValid())
    {
        GetWorld()->GetTimerManager().ClearTimer(ShiftTimerHandle);
    }

    UE_LOG(LogTemp, Log, TEXT("VitalPoint: %s frozen at index %d"),
        *GetOwner()->GetName(), ActiveVitalIndex);
}