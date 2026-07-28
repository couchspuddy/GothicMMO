// GothicPlayerAnimInstance.cpp

#include "Character/GothicPlayerAnimInstance.h"
#include "Character/GothicPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/SkeletalMesh.h"

void UGothicPlayerAnimInstance::CachePinBoneRestTransform()
{
    bPinBoneRestCached = false;

    const USkeletalMeshComponent* MeshComp = GetSkelMeshComponent();
    const USkeletalMesh* SkelMesh = MeshComp ? MeshComp->GetSkeletalMeshAsset() : nullptr;
    if (!SkelMesh)
    {
        return;
    }

    const FReferenceSkeleton& Ref = SkelMesh->GetRefSkeleton();
    const int32 BoneIdx = Ref.FindBoneIndex(UpperBodyPinBoneName);
    if (BoneIdx == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicPlayerAnimInstance: '%s' is not a bone on %s — upper-body pin disabled."),
            *UpperBodyPinBoneName.ToString(), *GetNameSafe(SkelMesh));
        return;
    }

    // Walk to the root accumulating local ref-pose transforms. This is why the value
    // is not a constant in the header: the correct rest orientation is whatever the
    // rig says it is, and guessing it wrong is what threw the character sideways.
    const TArray<FTransform>& LocalPose = Ref.GetRefBonePose();
    FTransform Accum = FTransform::Identity;
    for (int32 i = BoneIdx; i != INDEX_NONE; i = Ref.GetParentIndex(i))
    {
        Accum *= LocalPose[i];
    }

    PinBoneRestTransform = Accum;
    bPinBoneRestCached = true;
}

void UGothicPlayerAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    OwnerCharacter = Cast<AGothicPlayerCharacter>(TryGetPawnOwner());

    CachePinBoneRestTransform();

    if (!OwnerCharacter)
    {
        // Not fatal — the ABP's own locomotion values come from its event graph and
        // will still work. Only the aim offset goes dead, so say so plainly rather
        // than leaving a weapon that mysteriously refuses to track.
        UE_LOG(LogTemp, Warning,
            TEXT("GothicPlayerAnimInstance: owner is not AGothicPlayerCharacter — "
                 "aim offset will stay centred."));
    }
}

void UGothicPlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    // Re-resolve rather than trusting the cache. NativeInitializeAnimation can run
    // before possession on a respawn, in which case TryGetPawnOwner was null then
    // and would stay null forever.
    if (!OwnerCharacter)
    {
        OwnerCharacter = Cast<AGothicPlayerCharacter>(TryGetPawnOwner());
        if (!OwnerCharacter)
        {
            return;
        }
    }

    bIsAiming = OwnerCharacter->IsAiming();

    // GetBaseAimRotation, not GetControlRotation: the latter is only meaningful on
    // the machine that owns the controller, so a remote player's weapon would sit
    // permanently level. BaseAimRotation is replicated for exactly this purpose.
    const FRotator AimRotation = OwnerCharacter->GetBaseAimRotation();

    const float Range = FMath::Max(1.f, AimPitchRangeDegrees);

    // NormalizeAxis first: Pitch arrives as 0..360, so looking down reads as ~350
    // rather than -10, and dividing that by 90 pegs the offset at full up.
    const float TargetPitch = FMath::Clamp(
        FRotator::NormalizeAxis(AimRotation.Pitch) / Range, -1.f, 1.f);

    // Yaw relative to where the body faces, not absolute world yaw — an absolute
    // value would deflect the weapon simply because the player turned around.
    const float TargetYaw = FMath::Clamp(
        FRotator::NormalizeAxis(AimRotation.Yaw - OwnerCharacter->GetActorRotation().Yaw) / Range,
        -1.f, 1.f);

    if (AimInterpSpeed > 0.f && DeltaSeconds > 0.f)
    {
        AimPitch = FMath::FInterpTo(AimPitch, TargetPitch, DeltaSeconds, AimInterpSpeed);
        AimYaw   = FMath::FInterpTo(AimYaw,   TargetYaw,   DeltaSeconds, AimInterpSpeed);
    }
    else
    {
        AimPitch = TargetPitch;
        AimYaw   = TargetYaw;
    }

    // ── Upper-body pin ───────────────────────────────────────────────────
    if (!bPinBoneRestCached)
    {
        UpperBodyPinAlpha = 0.f; // fail safe: no rest transform, no pin
        return;
    }

    const UCharacterMovementComponent* Move = OwnerCharacter->GetCharacterMovement();
    const bool bRelease =
        (bReleasePinInAir && Move && Move->IsFalling()) ||
        (bReleasePinWhileSprinting && OwnerCharacter->IsSprinting());

    // Blended rather than switched, or the weapon would teleport into frame the
    // instant a sprint ends.
    const float TargetAlpha = bRelease ? 0.f : 1.f;
    UpperBodyPinAlpha = (DeltaSeconds > 0.f)
        ? FMath::FInterpTo(UpperBodyPinAlpha, TargetAlpha, DeltaSeconds, PinBlendSpeed)
        : TargetAlpha;

    const float PinPitch = FMath::Clamp(
        FRotator::NormalizeAxis(AimRotation.Pitch), -MaxPinPitchDegrees, MaxPinPitchDegrees);

    // Pre-multiply so the pitch is applied in COMPONENT space — the frame the camera
    // is in. Post-multiplying would pitch about the bone's own twisted axis, which on
    // this rig points sideways.
    const FQuat PitchQ = FRotator(PinPitch, 0.f, 0.f).Quaternion();
    const FQuat RestQ  = PinBoneRestTransform.GetRotation();

    // The tuning offset is applied in COMPONENT space too, outermost.
    //
    // It was originally post-multiplied, i.e. in the bone's own frame — which is
    // technically fine and practically useless: spine_01 rests at roll -90 / pitch 86
    // / yaw -90, so "yaw" in that frame is not left-right on screen and nobody can
    // tune it by eye. Applied here instead, the fields mean what they look like:
    // Yaw swings the weapon horizontally, Pitch raises and lowers it, Roll tilts it.
    const FQuat FinalQ = UpperBodyRotationOffset.Quaternion() * PitchQ * RestQ;

    UpperBodyPinRotation = FinalQ.Rotator();

    // The bone stays where the rig puts it.
    //
    // An earlier version orbited this point about the camera, to keep the weapon's
    // offset from the eye invariant under pitch. The geometry was right and it did
    // lock the weapon — but it swung the entire torso around the player's head to do
    // it, which reads as the view lurching even though the camera never moved.
    //
    // That is no longer this system's job. The weapon is parented to the camera now
    // (AGothicPlayerCharacter::ApplyWeaponAttachment), so it is locked to the crosshair
    // without the body having to contort to put it there. What remains here is only
    // the third-person pose other players see.
    UpperBodyPinLocation = PinBoneRestTransform.GetLocation() + UpperBodyLocationOffset;
}
