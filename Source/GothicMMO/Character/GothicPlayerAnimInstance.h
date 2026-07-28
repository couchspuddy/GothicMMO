// GothicPlayerAnimInstance.h
// Thin C++ AnimInstance for the player, supplying the aim data the AnimGraph
// cannot compute for itself.
//
// WHY THIS EXISTS
// ---------------
// ABP_GothicPlayerCharacter already computes its own locomotion values
// (GroundSpeed, Direction, IsFalling) in its event graph, and those stay where
// they are. What it had no way to know was where the player is LOOKING — and
// without that, the upper body is a fixed pose and the weapon points wherever
// the animation happens to point rather than at what you are aiming at.
//
// Mirrors UGothicEnemyAnimInstance: cache game-state values here, let the ABP
// read them. The state machine and blends stay in the ABP where they are
// visible; this class only answers questions.
//
// Setup: reparent ABP_GothicPlayerCharacter to this class. Its existing
// Blueprint variables do not collide with anything declared here, so the event
// graph keeps working untouched.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GothicPlayerAnimInstance.generated.h"

class AGothicPlayerCharacter;

UCLASS()
class GOTHICMMO_API UGothicPlayerAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // -----------------------------------------------------------------
    // Aim offset input
    // -----------------------------------------------------------------

    /**
     * Look pitch, NORMALIZED to -1..1 — not degrees.
     *
     * AO_Pistol's axes run -1..1 (its samples are MF_Pistol_Idle_ADS_AO_CD at -1
     * and _CU at +1), so feeding raw degrees would peg the offset at full up or
     * full down the instant the player looked anywhere but dead level. Divided by
     * AimPitchRangeDegrees and clamped, so -1 is looking fully down and +1 fully up.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation|Aim")
    float AimPitch = 0.f;

    /**
     * Look yaw, normalized the same way.
     *
     * Provided for completeness and currently INERT: every sample in AO_Pistol
     * sits at x=0, so the aim offset has no horizontal poses to blend to and this
     * value changes nothing. It is here so that adding left/right samples later is
     * an asset change rather than a code change — do not wire it up expecting
     * movement until those samples exist.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation|Aim")
    float AimYaw = 0.f;

    /** True while the player holds aim. Mirrors AGothicPlayerCharacter::IsAiming. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation|Aim")
    bool bIsAiming = false;

    /**
     * Degrees of look angle that map to full deflection. 90 means looking
     * straight up reaches +1. Lower it to make the weapon swing further for less
     * head movement — the aim offset then exaggerates rather than tracks.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|Aim",
              meta = (ClampMin = "15.0", ClampMax = "90.0"))
    float AimPitchRangeDegrees = 90.f;

    /**
     * Smoothing on AimPitch. The camera can snap a long way in one frame on a
     * fast mouse flick, and an unsmoothed offset snaps the arms with it; this
     * lets the weapon lag the look very slightly, which reads as weight.
     * Raise toward 30 for a tighter, more responsive feel; 0 disables smoothing.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|Aim",
              meta = (ClampMin = "0.0"))
    float AimInterpSpeed = 15.f;

    // -----------------------------------------------------------------
    // Upper-body pin
    //
    // THE PROBLEM THIS SOLVES. The weapon hangs off a hand socket, so it inherits
    // every bone between it and the root. The camera hangs off the mesh COMPONENT,
    // which animation never touches. Any motion in that bone chain therefore shows
    // up as the weapon sliding around the screen, and the crosshair — which is just
    // the centre of the camera — does not follow it.
    //
    // Filtering the blend higher up the spine only ever moved the leak: at spine_03
    // the sway came from spine_01/02, and flattening the pelvis still left its
    // vertical bob. The chain always has one more animated parent.
    //
    // So this stops inheriting entirely. Replacing spine_01's COMPONENT-space
    // transform makes the pose above it independent of the pelvis: the children
    // keep their local aim-pose transforms, so torso, arms and weapon move as one
    // rigid unit fixed to the component — exactly the frame the camera lives in.
    // Legs hang off the pelvis and are untouched.
    //
    // Pitching that unit by the look angle then keeps the muzzle on the crosshair,
    // because the weapon's forward and the camera's forward stay parallel.
    // -----------------------------------------------------------------

    /** Component-space location for the pinned bone. Drive Transform(Modify)Bone's
     *  Translation pin with this. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation|UpperBody")
    FVector UpperBodyPinLocation = FVector::ZeroVector;

    /** Component-space rotation for the pinned bone: rest orientation turned by the
     *  look pitch. Drive the Rotation pin with this. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation|UpperBody")
    FRotator UpperBodyPinRotation = FRotator::ZeroRotator;

    /** 0..1 strength of the pin. Drive the Alpha pin with this. */
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Animation|UpperBody")
    float UpperBodyPinAlpha = 0.f;

    /**
     * Bone the upper body is pinned at. spine_01 is the lowest bone that carries
     * the torso without carrying the legs — pelvis would take thigh_l/thigh_r with
     * it and freeze the walk.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|UpperBody")
    FName UpperBodyPinBoneName = TEXT("spine_01");

    /**
     * Release the pin while sprinting, so the body animates freely and the weapon
     * is allowed to leave frame. This is the sprint behaviour by design, not a
     * side effect.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|UpperBody")
    bool bReleasePinWhileSprinting = true;

    /** Release the pin in the air, so jump and fall read as real motion. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|UpperBody")
    bool bReleasePinInAir = true;

    /** How fast the pin blends in and out. Too high and sprint transitions snap. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|UpperBody",
              meta = (ClampMin = "0.5"))
    float PinBlendSpeed = 8.f;

    /**
     * Look pitch is clamped to this before it turns the torso. Full look pitch is
     * ±90, which would fold the spine in half; the weapon leaves frame past this
     * regardless, and letting the torso follow that far looks broken from outside.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|UpperBody",
              meta = (ClampMin = "0.0", ClampMax = "89.0"))
    float MaxPinPitchDegrees = 70.f;

    /**
     * Fine adjustment for lining the muzzle up with the crosshair, applied in
     * COMPONENT space so the fields behave the way they read: Yaw swings the weapon
     * left and right, Pitch raises and lowers it, Roll tilts it.
     *
     * The aim pose was authored for a third-person camera, so the weapon will not
     * sit dead centre by default. Tune this once and it stays.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|UpperBody")
    FRotator UpperBodyRotationOffset = FRotator::ZeroRotator;

    /** Fine positional adjustment, component space. Same purpose as the rotation
     *  offset — moves the whole torso-and-weapon unit. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Animation|UpperBody")
    FVector UpperBodyLocationOffset = FVector::ZeroVector;

protected:
    UPROPERTY()
    TObjectPtr<AGothicPlayerCharacter> OwnerCharacter;

private:
    /** Reference-pose transform of UpperBodyPinBoneName in component space.
     *  Read from the ref skeleton rather than hardcoded: identity is NOT neutral on
     *  this rig — replacing the pelvis with (0,0,0) once turned the whole character
     *  90 degrees, because its rest yaw is -90. */
    FTransform PinBoneRestTransform = FTransform::Identity;
    bool bPinBoneRestCached = false;

    void CachePinBoneRestTransform();
};
