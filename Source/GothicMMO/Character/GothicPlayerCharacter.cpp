// GothicPlayerCharacter.cpp

#include "Character/GothicPlayerCharacter.h"
#include "Character/GothicInputHandlerComponent.h"
#include "AbilitySystem/GothicAbilitySet.h"
#include "AbilitySystem/GothicInputConfig.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystem/GothicGameplayTags.h"   // Perk.Weapon.* — the handling and economy effects
#include "Game/GothicPlayerState.h"
#include "Game/GothicGameMode.h"
#include "Game/GothicGameInstance.h"
#include "Game/GothicGameState.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameStateBase.h"       // PlayerArray — the party-aliveness walk
#include "HAL/IConsoleManager.h"               // Gothic.Revive
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UI/GothicHUD.h"
#include "UI/GothicHintManagerComponent.h"
#include "AI/GothicSteadfastComponent.h"
#include "AI/GothicCombatStateComponent.h"
#include "UI/GothicHUDWidget.h"
#include "UI/GothicInventoryWidget.h"
#include "Blueprint/UserWidget.h"              // Selah name-cycle widget — teardown poll
#include "Camera/CameraComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"                // USkeletalMesh — grip-socket mount diagnostics
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Items/GothicInventoryComponent.h"
#include "Game/GothicGameState.h"
#include "AI/GothicEnemyBase.h"
#include "AI/GothicEncounterVolume.h"
#include "UI/GothicHUD.h"
#include "GameFramework/PlayerController.h"
#include "Items/GothicItemDefinition.h"
#include "AbilitySystem/GA_TheLovedandTheLost.h"
#include "AbilitySystem/GA_NotAtAll.h"
#include "GothicMMO.h"                      // LogVigilCombat

namespace
{
    /**
     * The `t=` stamp every VigilTimeline line carries. Free function for the
     * same reason the arena manager has one — logging should not require a
     * header change — and it answers 0 rather than asserting for a pawn
     * logging outside its world, which is a diagnostic, not a place to fail.
     */
    float GASInitTimelineNow(const AActor* Actor)
    {
        const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
        return World ? World->GetTimeSeconds() : 0.f;
    }

    // ── Weapon perk coefficients (WEAPON_PERK_TABLES.md) ────────────────────
    //
    // Constants, not catalog reads. FGothicWeaponPerkEntry::Magnitude is 0 in the
    // authored asset, so reading it today would zero every effect silently; these
    // are the doc's numbers and a real Magnitude can supersede them later without
    // moving a single call site.

    /** Dead Hand — "recoil pitch -30%". */
    constexpr float DeadHandRecoilPitchReduction = 0.30f;

    /** True Bore — "yaw spread -50%". */
    constexpr float TrueBoreYawSpreadReduction = 0.50f;

    /** Well-Tended — "Steadfast refill restores 50% more reserve ammo". */
    constexpr float WellTendedYieldScale = 1.5f;

    /** Charitable Toll — "costs 1 fewer charge (minimum 1)". */
    constexpr int32 CharitableTollChargeDiscount = 1;

    /** Frugal Hand — "one ammo tier lower, at half Steadfast cost". */
    constexpr float FrugalHandCostScale = 0.5f;

    /** Overcharge — "one ammo tier higher, at 1.5x Steadfast cost". */
    constexpr float OverchargeCostScale = 1.5f;
}

AGothicPlayerCharacter::AGothicPlayerCharacter()
{

    PrimaryActorTick.bCanEverTick = true;

    // The player is the only actor that collides with the Bleed. AGothicBleedGate
    // types its barrier as ArenaBlock, whose project-default response is Ignore,
    // so this opt-in is what makes a gate solid — and its absence on the Accursed
    // is what lets them walk through one. Set here rather than on the Blueprint's
    // capsule so it is versioned with the gate that depends on it, and cannot be
    // lost to a Blueprint data revert.
    GetCapsuleComponent()->SetCollisionResponseToChannel(
        ECC_GameTraceChannel2 /*ArenaBlock*/, ECR_Block);

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    // Classic FP assembly: the eye is parented to the CAPSULE at the historical seat and
    // bUsePawnControlRotation owns look direction, so the view yaws AND pitches rigidly with
    // the reticle. No bone, no socket, no warp rig — the rigid, boring, dependable mount, and
    // that rigidity is the point (the true-FP warp-rig approach, PRs #85-#90, was retired as
    // unfixable without the never-imported FP template character). The camera seat and the
    // arms->camera attach are BOTH re-enforced on the live instance in
    // PostInitializeComponents -> EnforceFirstPersonCameraMount, because the BP's serialized
    // hierarchy can fight a constructor-only attach (the banked trap).
    FirstPersonCamera->bUsePawnControlRotation = true;
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    // Historical eye seat: X=20 forward, Z = 170 - CapsuleHalfHeight (= 82 at the ACharacter
    // default 88) — where the camera has always sat. EnforceFirstPersonCameraMount re-asserts
    // this exact seat at runtime.
    FirstPersonCamera->SetRelativeLocation(
        FVector(20.f, 0.f, 170.f - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));

    // First-person FOV render path (added UE 5.5, present in 5.8). With the arms + FP weapon
    // flagged FirstPersonPrimitiveType=FirstPerson (below), the camera renders them at their
    // OWN field of view and a compressed depth range, so the gun never clips the world near
    // plane. Enabled here structurally; the value is (re)pushed from FirstPersonFOV in
    // BeginPlay so a BP CDO override of that UPROPERTY still lands.
    FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
    FirstPersonCamera->FirstPersonFieldOfView = FirstPersonFOV;

    // Owner never sees their own third-person body (a swinging shoulder/head in the
    // FP camera), but MUST still cast its shadow — a first-person player with no shadow
    // reads as floating. SetOwnerNoSee + bCastHiddenShadow gives exactly that: hidden
    // to the owner, shadow retained. These flags are evaluated per-viewer, so remote
    // players' bodies are completely unaffected — everyone else sees the full Manny.
    GetMesh()->SetOwnerNoSee(true);
    GetMesh()->bCastHiddenShadow = true;

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = true;
    bUseControllerRotationRoll  = false;

    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->MaxWalkSpeed              = 500.f;  // Overwritten by WalkSpeed in BeginPlay
    GetCharacterMovement()->JumpZVelocity             = 700.f;

    // DEPRECATED / RETIRED — the native "WeaponMesh" subobject. It is no longer driven by
    // any code (all runtime work moved to FPWeaponMesh, created below). It is still created
    // here only so the WeaponMeshComponent UPROPERTY it backs keeps a valid layout for the
    // BP-derived class that carries the pointer-redirect onto the ghost "UWeaponMesh";
    // NeutralizeDuplicateWeaponMesh hides both this and the ghost at runtime so neither
    // renders. Do NOT re-wire this into equip/pose/visibility — see the header.
    WeaponMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
    WeaponMeshComponent->SetupAttachment(GetMesh(), TEXT("HandGrip_R"));
    WeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMeshComponent->SetOnlyOwnerSee(true);
    WeaponMeshComponent->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;

    // First-person arms — a skeletal mesh parented to the CAMERA (classic assembly),
    // owner-only. As a camera child it inherits the view's yaw AND pitch rigidly, so the
    // arms — and the gun they hold — track the reticle by construction, with no bone chain
    // the camera knows nothing about and no per-angle correction. The camera-relative resting
    // seat is ArmsOffset/ArmsRotation (applied in EnforceFirstPersonCameraMount and BeginPlay,
    // and reconstituted each frame by UpdateFirstPersonWeaponPose); a nominal seat is set here
    // so the CDO is sane before that runs.
    //
    // NO CopyPoseFromMesh, NO ControlRig, NO tick prerequisite: the mesh runs with AnimClass
    // = None and the single-node ArmsIdlePose fallback drives it (see BeginPlay). SetOnlyOwnerSee
    // is why this needs NO possession-race handling — every pawn's arms render only on their
    // own owner's machine, so on every OTHER machine this component is inert by construction.
    // No mesh is assigned in C++ (SkeletalMesh stays null — renders nothing until the editor
    // pass assigns SKM_Manny_Simple or a Paragon kit).
    FirstPersonArmsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArms"));
    FirstPersonArmsMesh->SetupAttachment(FirstPersonCamera);
    FirstPersonArmsMesh->SetRelativeLocationAndRotation(ArmsOffset, ArmsRotation);
    FirstPersonArmsMesh->SetOnlyOwnerSee(true);
    FirstPersonArmsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonArmsMesh->CastShadow = false;
    FirstPersonArmsMesh->bCastHiddenShadow = false;
    // Flag the arms as first-person so the camera's FirstPersonFieldOfView/depth-compression
    // apply to them (UE 5.8). Pairs with the FP weapon flag above; the TP body is untouched.
    FirstPersonArmsMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;

    // The arms->camera attach above is a constructor SetupAttachment; it is re-asserted on
    // the live instance in EnforceFirstPersonCameraMount (PostInitializeComponents) because
    // the BP's serialized hierarchy can override a constructor-only attach (the banked trap).

    // First-person weapon — the ONE honest gun, BORN on the hand. A child of the arms mesh
    // at FPWeaponGripSocket, so it rides the animated hand by construction: no runtime
    // re-parent and — crucially — no Blueprint pointer-redirect can move it, because
    // "FPWeaponMesh" is a brand-new name with ZERO legacy serialization (the redirect that
    // hijacked WeaponMeshComponent onto the ghost "UWeaponMesh" cannot bind a name the BP
    // never serialized). This constructor SetupAttachment is safe for that same reason — the
    // attach-cycle trap only bites names the BP already has a serialized hierarchy for.
    // Owner-only + FirstPerson-flagged, exactly like the arms it rides; no collision, no
    // shadow (the TP body/weapon carry the world silhouette). FPWeaponGripSocket's inline
    // default (HandGrip_R) is already in place here; if the assigned arms mesh lacks the
    // socket the attach falls back to the mesh root and ApplyWeaponAttachment corrects it.
    FPWeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FPWeaponMesh"));
    FPWeaponMesh->SetupAttachment(FirstPersonArmsMesh, FPWeaponGripSocket);
    FPWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FPWeaponMesh->SetOnlyOwnerSee(true);
    FPWeaponMesh->CastShadow = false;
    FPWeaponMesh->bCastHiddenShadow = false;
    FPWeaponMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;

    // Third-person weapon — the mirror of FPWeaponMesh for OTHER players. Rides
    // the third-person body's hand socket so a remote pawn shows the gun in Manny's hand.
    // OwnerNoSee is the exact complement of the FP weapon's OnlyOwnerSee: the local player
    // sees only their camera-mounted gun, everyone else sees only this one. Casts a shadow
    // (a floating-gun shadow is fine and expected); no collision. The mesh is assigned in
    // lockstep with the FP weapon by RefreshWeaponVisuals — nothing is set here. Socket
    // read from the member default (ThirdPersonWeaponSocket) which is already initialized.
    ThirdPersonWeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThirdPersonWeaponMesh"));
    ThirdPersonWeaponMesh->SetupAttachment(GetMesh(), ThirdPersonWeaponSocket);
    ThirdPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ThirdPersonWeaponMesh->SetOwnerNoSee(true);  // everyone EXCEPT the local player

    // Create the input handler component
    InputHandler = CreateDefaultSubobject<UGothicInputHandlerComponent>(TEXT("InputHandler"));

    // Tutorial hints. A default subobject rather than a BP-added component so
    // every player pawn has one whether or not BP_GothicPlayerCharacter is
    // touched — the hint call sites in this file dereference it unconditionally,
    // and a hint system that is only present when a designer remembered to add
    // it is a hint system that silently stops existing.
    HintManager = CreateDefaultSubobject<UGothicHintManagerComponent>(TEXT("HintManager"));
}

void AGothicPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    // The FP camera mount (arms re-seat -> camera-on-head -> FP FOV) was already enforced in
    // PostInitializeComponents -> EnforceFirstPersonCameraMount, which runs before BeginPlay
    // and after the Blueprint's serialized hierarchy is applied. Nothing re-attaches here;
    // BeginPlay only captures the resting world FOV, which depends on whatever the Blueprint
    // serialized onto the camera and so is read after that has landed.
    if (FirstPersonCamera)
    {
        // Whatever the Blueprint set is the resting FOV — read it once rather than
        // hardcoding 90 here and having the two disagree the moment someone tunes it.
        HipFieldOfView = FirstPersonCamera->FieldOfView;
    }


    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Subsystem->AddMappingContext(DefaultMappingContext, 0);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: DefaultMappingContext is null"));
            }
        }
    }

    // Initialize ammo for all weapon slots from their data assets
    for (FGothicWeaponSlot& Slot : WeaponSlots)
    {
        Slot.InitFromData();
    }

    // Show the starting weapon mesh through the single visuals seam rather than
    // setting the static mesh by hand here. RefreshWeaponVisuals sets the mesh, its
    // scale, and the attachment in one place — the same call every equip and swap
    // already routes through — and is null-safe on an empty slot or mesh-less data.
    //
    // The ATTACHMENT it applies is locality-dependent (camera for the local player,
    // hand socket for a remote), and BeginPlay runs BEFORE possession on clients and
    // respawned pawns, so IsLocallyControlled() is false here and the local gun would
    // land on the hand socket. That is corrected by re-running the attachment from
    // PossessedBy / OnRep_Controller once locality is actually known.
    RefreshWeaponVisuals(ActiveWeaponIndex);

    // A constructor SetOwnerNoSee is overridden by whatever the Blueprint serialized
    // on the inherited mesh, and the Blueprint had it off — which is the entire reason
    // the player could see their own shoulder swinging through frame. Enforce it here,
    // where the Blueprint cannot win.
    if (GetMesh() && bHideBodyInFirstPerson)
    {
        GetMesh()->SetOwnerNoSee(true);
    }

    // Mirror the enforcement for the FP arms: a constructor SetOnlyOwnerSee is likewise
    // overridden by whatever the Blueprint serialized on the inherited component, and if
    // the BP flipped it off every OTHER player would see this owner-only body clipped onto
    // the Manny they already render. Re-assert it here where the Blueprint cannot win.
    if (FirstPersonArmsMesh)
    {
        FirstPersonArmsMesh->SetOnlyOwnerSee(true);
    }

    // Pose the first-person arms. PLACEMENT is applied here (classic assembly): the arms
    // are a CHILD of the camera, so ArmsOffset/ArmsRotation are their camera-relative resting
    // seat. Re-applied here — after EnforceFirstPersonCameraMount's pre-BeginPlay pass — so a
    // BP CDO override of these knobs lands; UpdateFirstPersonWeaponPose then reconstitutes this
    // seat every frame as the base of its sprint/kick arms write. Null-safe: no mesh assigned
    // yet still seats the (empty) component correctly.
    //
    // The classic assembly runs the arms with AnimClass = None, so the single-node idle
    // FALLBACK below is the live pose path (not a fallback for an ABP that no longer exists):
    // the editor pass assigns a full-body mesh with no ABP, which would otherwise render as a
    // T-pose. A null ArmsIdlePose leaves the mesh in its ref pose.
    if (FirstPersonArmsMesh)
    {
        FirstPersonArmsMesh->SetRelativeLocationAndRotation(ArmsOffset, ArmsRotation);

        // If an AnimClass is somehow assigned, the animation blueprint OWNS the pose and we
        // must NOT force single-node playback (it would tear the ABP down). The classic
        // assembly assigns None, so the single-node idle below is the normal path; this guard
        // only protects a mesh that arrives with an unexpected AnimClass.
        if (FirstPersonArmsMesh->GetAnimClass() != nullptr)
        {
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|t=%.3f|%s|ArmsPose|SKIPPED|reason=anim-class-assigned"),
                GASInitTimelineNow(this), *GetName());
        }
        else if (ArmsIdlePose)
        {
            // Drive the idle as a single looping node — but NOT through PlayAnimation.
            // On this component the mesh's serialized AnimationMode is ALREADY
            // AnimationSingleNode, so SetAnimationMode(SingleNode) sees bNeedChange==false
            // and short-circuits WITHOUT (re)spawning the UAnimSingleNodeInstance
            // (USkeletalMeshComponent::SetAnimationMode, UE 5.8). PlayAnimation then calls
            // SetAnimation, which finds GetSingleNodeInstance()==nullptr and drops the asset
            // SILENTLY: AnimationData.AnimToPlay stays None and the arms sit in ref pose —
            // measured twice, hours apart, on the live pawn.
            //
            // Fix per engine source: write the single-node payload into the serialized
            // AnimationData struct first (that is exactly what InitializeAnimScriptInstance
            // pushes into a freshly spawned instance via AnimationData.Initialize), then
            // FORCE a reinit so the instance is rebuilt regardless of the bNeedChange branch.
            FirstPersonArmsMesh->AnimationData.AnimToPlay = ArmsIdlePose;
            FirstPersonArmsMesh->AnimationData.bSavedLooping = true;
            FirstPersonArmsMesh->AnimationData.bSavedPlaying = true;
            FirstPersonArmsMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
            FirstPersonArmsMesh->InitAnim(/*bForceReinit=*/true);

            // Read back the mode AND the asset after the call — the two facts that would
            // have caught this failing silently through three diagnosis passes.
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|t=%.3f|%s|ArmsPose|APPLIED|pose=%s|mode=%d|animToPlay=%s"),
                GASInitTimelineNow(this), *GetName(), *ArmsIdlePose->GetName(),
                (int32)FirstPersonArmsMesh->GetAnimationMode(),
                FirstPersonArmsMesh->AnimationData.AnimToPlay
                    ? *FirstPersonArmsMesh->AnimationData.AnimToPlay->GetName() : TEXT("None"));
        }
        else
        {
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|t=%.3f|%s|ArmsPose|SKIPPED|reason=null-ArmsIdlePose"),
                GASInitTimelineNow(this), *GetName());
        }

        // Head removal — LOAD-BEARING. The arms are seated low-forward of the camera
        // (ArmsOffset), so a full-body mesh's head/neck sits just behind the near plane and
        // can clip the frame edges. Hiding the head bone keeps the owner's own skull out of
        // frame. GetBoneIndex returns INDEX_NONE when no mesh is assigned yet or the bone is
        // absent, so the whole thing is a safe no-op then; NAME_None disables it entirely.
        if (FPHeadBoneToHide != NAME_None &&
            FirstPersonArmsMesh->GetBoneIndex(FPHeadBoneToHide) != INDEX_NONE)
        {
            FirstPersonArmsMesh->HideBoneByName(FPHeadBoneToHide, PBO_None);
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|t=%.3f|%s|ArmsHead|HIDDEN|bone=%s"),
                GASInitTimelineNow(this), *GetName(), *FPHeadBoneToHide.ToString());
        }
    }

    // Delay HUD polling until widget is fully constructed
    bHUDReady = false;
    FTimerHandle HUDInitTimer;
    GetWorldTimerManager().SetTimer(HUDInitTimer, [this]()
    {
        // Owner-scoped, like PushAmmoToHUD. This timer runs on every machine for
        // every pawn, so player 0 meant that on a listen server a REMOTE player's
        // pawn pushed its health and its crosshair onto the host's HUD.
        APlayerController* PC =
            IsLocallyControlled() ? Cast<APlayerController>(GetController()) : nullptr;
        AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;


        bHUDReady = true;

        // First ammo readout — nothing has fired or reloaded yet, so without this
        // the magazine display stays blank until the player's first shot.
        PushAmmoToHUD();

        // First health readout, for the same reason and with a worse symptom.
        // WBP_HUD_LayoutA's Construct hardcodes the bar to 100%, and UpdateHealth
        // only fires on a health CHANGE — so a player who does not start at full
        // health saw a full bar that snapped to the truth on the first hit,
        // reading as one huge chunk of damage regardless of the amount. Push the
        // real ratio once here so the bar starts honest.
        if (GothicHUD && AttributeSet)
        {
            GothicHUD->UpdateHealth(AttributeSet->GetHealth(), AttributeSet->GetMaxHealth());
        }

        // Re-baseline the stun indicator on a fresh/respawned pawn. The tag event
        // only fires on a CHANGE, so without this a HUD that survives a death mid-
        // stun (the widget is owned by the PlayerController, not the pawn) would
        // keep the previous life's indicator lit. Reads the current tag off the
        // ASC — honest true (with remaining time) or an explicit clear.
        if (AbilitySystemComponent)
        {
            const bool bStunnedNow = AbilitySystemComponent->HasMatchingGameplayTag(
                GothicTags::State_Stunned);
            PushStunStateToHUD(bStunnedNow, bStunnedNow ? GetStunRemainingDuration() : 0.f);
        }

        // Apply the active weapon's crosshair on spawn. The HUD boots on Melee by
        // default and the starting weapon is equipped before the HUD widget exists,
        // so without this the crosshair stays Melee until the first weapon swap.
        if (GothicHUD && WeaponSlots.IsValidIndex(ActiveWeaponIndex))
        {
            if (const UGothicWeaponData* ActiveWeapon = WeaponSlots[ActiveWeaponIndex].WeaponData)
            {
                GothicHUD->SetCrosshairType(ActiveWeapon->CrosshairType);
            }
        }
    }, 0.1f, false);
}

void AGothicPlayerCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    InitGASFromPlayerState();

    // The server half of the possession race. BeginPlay attached the weapon while
    // IsLocallyControlled() was still false (BeginPlay precedes possession on
    // respawned pawns — the same ordering that forces lazy ASC resolution in
    // GothicSteadfastComponent), which routes the listen-server host's own gun to the
    // hand socket instead of the camera. InitGASFromPlayerState above already re-runs
    // the visuals when the PlayerState is present, but it early-returns and retries
    // when the PlayerState has not yet resolved; re-attaching here closes that gap
    // unconditionally now that the controller is known.
    ReapplyActiveWeaponAttachment();

    // Same possession seam, same locality dependence: enforce the FP-mesh visibility
    // gate now that the controller (hence IsLocallyControlled) is known.
    UpdateFirstPersonVisibility();
}

void AGothicPlayerCharacter::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();
    InitGASFromPlayerState();
}

void AGothicPlayerCharacter::OnRep_Controller()
{
    Super::OnRep_Controller();

    // The client half of the possession race. On the owning client the pawn spawns
    // and runs BeginPlay before its Controller replicates in, so the weapon was
    // attached as a remote's (hand socket, hidden by SetOnlyOwnerSee) rather than to
    // the camera. This is the moment IsLocallyControlled() first becomes true on the
    // client; re-run the attachment so the local player's gun snaps to the camera
    // mount. Mirrors the lazy-resolution pattern components use for the same
    // BeginPlay-precedes-possession disease.
    ReapplyActiveWeaponAttachment();

    // This is also the moment the OWNING client's locality first resolves, and the
    // moment a remote proxy's stays false — re-run the FP-mesh visibility gate so the
    // local player's arms/gun show and every remote pawn's are hidden deterministically.
    UpdateFirstPersonVisibility();
}

void AGothicPlayerCharacter::ReapplyActiveWeaponAttachment()
{
    if (WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        ApplyWeaponAttachment(WeaponSlots[ActiveWeaponIndex].WeaponData);
    }
}

void AGothicPlayerCharacter::UpdateFirstPersonVisibility()
{
    // Deterministic locality enforcement for the first-person-only meshes. See the
    // header for why SetOnlyOwnerSee alone leaks: it resolves against owner/connection
    // state that isn't reliably settled on a freshly-replicated remote pawn, so a
    // remote player's folded full-body FP mesh renders as a contorted figure until
    // ownership resolves. bHiddenInGame doesn't depend on that resolution.
    //
    // Applied unconditionally on every machine — including a dedicated server. There,
    // remote pawns being hidden is correct (nothing owner-sees them) and also keeps the
    // FP mesh's render output from being spent on views that can never matter; this touches
    // VISIBILITY only, never ticking or VisibilityBasedAnimTickOption, so the FP mesh's pose
    // evaluation is unchanged (out of scope by design).
    const bool bLocal = IsLocallyControlled();
    const bool bShouldHide = !bLocal;

    if (FPWeaponMesh)
    {
        FPWeaponMesh->SetHiddenInGame(bShouldHide);
    }
    if (FirstPersonArmsMesh)
    {
        FirstPersonArmsMesh->SetHiddenInGame(bShouldHide);
    }

    // Log once per transition, not per call — this runs from several seams and per
    // Tick-adjacent code would spam otherwise.
    const int8 NewState = bLocal ? 1 : 0;
    if (NewState != LastFPVisibilityLocal)
    {
        LastFPVisibilityLocal = NewState;
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|FPVis|local=%d|hidden=%d"),
            GASInitTimelineNow(this), *GetName(), NewState, bShouldHide ? 1 : 0);
    }
}

void AGothicPlayerCharacter::InitGASFromPlayerState()
{

    AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    if (!PS)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: PlayerState is null — skipping GAS init"));

        // On a CLIENT this repairs itself: the PlayerState replicates in and
        // OnRep_PlayerState calls straight back here. On the AUTHORITY there is
        // no such second call — OnRep never fires on the server — so PossessedBy
        // is the only driver this function will ever get, and returning here
        // ends the pawn's initialization permanently: no ability sets, no
        // starting kit, and therefore none of GE_EquipmentStats' MaxHealth on
        // top of the GE_InitStats 200. The host never notices because its pawn
        // is possessed through an earlier path with its PlayerState long since
        // resolved; a player who joins later is the one that loses the race.
        if (HasAuthority())
        {
            ScheduleGASInitRetry();
        }
        return;
    }

    // A retry that got us here has done its job. Stop the timer before the work
    // below rather than after, so an early return cannot leave it running.
    GetWorldTimerManager().ClearTimer(GASInitRetryTimer);

    AbilitySystemComponent = PS->GetGothicASC();
    AttributeSet           = PS->GetGothicAttributeSet();

    // A player's ASC and AttributeSet live on the PlayerState, so they SURVIVE
    // the pawn — everything OnDeath wrote into them is still there when the
    // replacement pawn possesses. State.Dead above all: it sits in the
    // ActivationBlockedTags of every ability, so a respawned player who kept it
    // would come back unable to fire, melee, or use a single power. Nothing
    // removed it, because nothing ever respawned anybody.
    //
    // Cleared here rather than in the game mode because this is the one path
    // every fresh player pawn takes — respawn, checkpoint restart, first spawn —
    // and because it has to happen BEFORE the ability sets are granted below:
    // the passives re-activate through TryActivateAbility, which the tag blocks.
    //
    // SetLooseGameplayTagCount rather than RemoveLooseGameplayTag: loose tags are
    // ref-counted, and a death reported twice leaves a count of two that a single
    // remove would not pay off.
    if (HasAuthority() && AbilitySystemComponent)
    {
        AbilitySystemComponent->SetLooseGameplayTagCount(
            GothicTags::State_Dead, 0);

        // Same outlives-the-pawn hazard for the two vulnerability windows the pack
        // surge decorator reads. Both are applied on this authority ASC (reload via
        // the reload-hold RPC, Selah in TriggerSelahMoment), and a pawn that died
        // mid-window would otherwise hand a stuck count to its replacement.
        AbilitySystemComponent->SetLooseGameplayTagCount(GothicTags::State_Reloading, 0);
        AbilitySystemComponent->SetLooseGameplayTagCount(GothicTags::State_Selah, 0);

        // The reactive action-tag windows (State.Firing / State.Whiffed /
        // State.Attacking) have the same outlives-the-pawn hazard. Their removal
        // timers are world-anchored and DO fire across a death (the ASC survives on
        // the PlayerState), so a normal death/respawn already self-clears — but a
        // pawn that died mid-window and gets a REUSED pawn back before the timer
        // elapses would inherit the open count. Cancel the timers and zero the counts
        // here so the fresh pawn always starts clean.
        AbilitySystemComponent->ClearTimedLooseTags();

        // The movement half of the Selah lock is a plain pawn bool, not an ASC
        // tag, so the clear above does not touch it. On a truly fresh pawn it is
        // already false; on a REUSED pawn (checkpoint restart) a lock stranded
        // from the previous life would ride in and freeze the new one. Clear the
        // flag and its fallback timer to match the tag.
        bSelahMomentLock = false;
        GetWorldTimerManager().ClearTimer(SelahMomentLockHandle);
        SelahMomentWidget.Reset();
        bSelahMomentWidgetRegistered = false;

        // Same hazard, same fix, for the downed flag. A fresh pawn is never
        // downed, and the flag lives on the PlayerState — which survives the pawn
        // that WAS downed. OnReviveWindowExpired already clears it on the way out,
        // so this is belt and braces for every other way a downed player can end
        // up with a new pawn (a fall respawn, a debug Gothic.SetDowned left set,
        // a level transition). SetDowned is a no-op when it agrees, so the normal
        // spawn pays nothing for it.
        PS->SetDowned(false);
    }

    // Belt and braces on the same inherited-ASC hazard: whatever State.Sprinting
    // count the previous pawn left behind, this fresh pawn is not sprinting, so
    // the tag is forced to match. Unlike the State.Dead clear above this is NOT
    // authority-gated — the sprint tag is owning-client state, applied wherever
    // the sprint input runs, so an authority-only reset would miss the client
    // that actually holds it.
    SyncSprintTag();

    InitializeGAS();

    if (HasAuthority() && AttributeSet)
    {
        // Come back alive. GE_InitStats_Player overrides Health with a sentinel
        // that PreAttributeBaseChange clamps to MaxHealth, so this is usually
        // already true by the time we get here — but a Blueprint shipping without
        // a DefaultAttributeEffect would respawn the player on the 0 health they
        // died at, and they would then die to the next point of damage, forever.
        // Guarded on MaxHealth so it can never write a zero of its own.
        if (AttributeSet->GetMaxHealth() > 0.f)
        {
            AttributeSet->SetHealth(AttributeSet->GetMaxHealth());
        }

        // Restore the Reckoning progress banked in OnDeath. -1 means nothing is
        // pending, which is every spawn that did not follow a death.
        const float CachedSuperMeter = PS->ConsumeCachedSuperMeterOnDeath();
        if (CachedSuperMeter >= 0.f)
        {
            AttributeSet->SetSuperMeter(CachedSuperMeter);
        }
    }

    // Grant ability sets — data driven, replaces old StartupAbilities array.
    //
    // The latch is the ASC that was granted into, not a bool saying a grant
    // happened. This function runs at least twice per pawn and re-reads
    // AbilitySystemComponent from the PlayerState at the top of every pass, so a
    // bool records the wrong fact: it says work was done without saying which
    // ASC received it. A player joining a listen server is where those two come
    // apart — their PlayerState resolves while the pawn is already possessed,
    // the first pass latches, the second pass swaps in the ASC the pawn actually
    // keeps, and the grant block skips it. The pawn is then authoritative,
    // possessed, and holding an ASC with zero specs: the exact shape read off
    // the GASInit telemetry, where the same pawn printed abilities=8 and then
    // abilities=0 two milliseconds apart.
    //
    // Comparing against the ASC closes that: a grant is owed to every ASC this
    // pawn ever points at, and only skipped for the one it already filled.
    //
    // Respawn is unchanged. The pawn is replaced, so this starts null on the new
    // one and the sets are given again to the surviving PlayerState ASC —
    // UGothicAbilitySet::GiveToAbilitySystem asks for an existing spec before
    // granting, so nothing duplicates, and the re-run is what brings back the
    // passives OnDeath's CancelAllAbilities shut down.
    //
    // HasAuthority() and nothing else. Nothing here is locally-controlled-
    // guarded and nothing here may become so — a remote player's pawn is not
    // locally controlled on the server, and gameplay grants are owed to every
    // pawn the authority owns. Clients never reach this block at all, which is
    // deliberate: GiveAbility is authority-only in GAS and a client attempt
    // would only add a misleading warning to the log. HUD work stays local-only;
    // see BindHUDAttributeDelegates.
    if (HasAuthority() && AbilitySystemComponent &&
        AbilitiesGrantedIntoASC.Get() != AbilitySystemComponent)
    {
        int32 GrantedSets = 0;
        for (const TObjectPtr<UGothicAbilitySet>& AbilitySet : StartupAbilitySets)
        {
            if (AbilitySet)
            {
                AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, this);
                ++GrantedSets;
            }
        }

        // Latch only on work actually done. A pass over an empty or all-null
        // StartupAbilitySets is not a completed grant, and marking it as one is
        // how a pawn ends up alive, controllable, and unable to activate a
        // single ability for the rest of its life.
        if (GrantedSets > 0)
        {
            AbilitiesGrantedIntoASC = AbilitySystemComponent;
        }
    }

    // Setup ability input bindings now that ASC is confirmed valid
    if (IsLocallyControlled() && InputHandler && AbilitySystemComponent)
    {
        if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
        {
            InputHandler->SetupAbilityInputBindings(EIC, AbilitySystemComponent);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: InputComponent not yet available for ability binding"));
        }
    }

    // HUD attribute delegates. Idempotent — see BindHUDAttributeDelegates for
    // why that matters when this function runs twice per pawn.
    BindHUDAttributeDelegates();

    // Make State.Stunned actually stop the player — same idempotency rules.
    // See HandleStunTagChanged.
    BindStunTagListener();

    // Bind to inventory equipment changes so weapon slots update when gear is equipped
    // Guarded: PossessedBy + OnRep_PlayerState both call InitGASFromPlayerState
    UGothicInventoryComponent* Inventory = PS->GetInventory();

    if (Inventory && !bInventoryBound)
    {
        Inventory->OnItemEquipped.AddDynamic(this, &AGothicPlayerCharacter::OnEquipmentChanged);
        bInventoryBound = true;
    }

    // Everything below used to live INSIDE that bind guard, which made the
    // starting kit and the slot sync reachable only on the single call that
    // first bound the delegate. One lost race there — a call that binds while
    // something downstream is not ready yet — and the pawn never gets another
    // attempt, because bInventoryBound is already up. That is the whole
    // starting loadout, and with it GE_EquipmentStats, which is where a
    // player's MaxHealth above the GE_InitStats 200 comes from: a pawn that
    // misses this reads a flat 200 forever while a geared one reads ~245.
    //
    // Both halves are idempotent on their own — GrantStartingItems is one-shot
    // on its own bStartingItemsGranted, and the sync just re-reads what is
    // equipped — so they are safe to run on every pass, which is what gives a
    // retry something to repair.
    if (Inventory)
    {
        // Grant BEFORE the sync, and after the bind: equipping fires
        // OnItemEquipped, and the sync below then reads the result.
        // Authority-only in effect — GrantStartingItems refuses on a client —
        // but stated here too, because "the server hands out the kit" should be
        // legible at the call site.
        if (HasAuthority())
        {
            // A player who just travelled here from another level gets their own
            // gear back instead of the starting kit. The snapshot lives on the
            // GameInstance, the only object OpenLevel does not destroy, and is
            // one-shot — so this is a no-op on a respawn, on a fresh boot, and on
            // every pass but the first after a travel.
            //
            // It must run BEFORE GrantStartingItems, and it does its own
            // suppression: the restore raises bStartingItemsGranted, so the call
            // below turns into the early return it already has.
            //
            // Position matters as much as order. This sits after InitializeGAS
            // and after the ASC/AttributeSet have been re-read from the
            // PlayerState, because the restore re-equips through EquipItem and
            // therefore needs a live ASC to apply GE_EquipmentStats to, and it
            // writes the Selah attribute, which GE_InitStats_Player would
            // otherwise stomp if it ran afterwards.
            if (UGothicGameInstance* GothicGI = GetGameInstance<UGothicGameInstance>())
            {
                GothicGI->RestoreTravelSnapshot(PS);
            }

            // On the measurement bench, roll the kit canonically — frozen to
            // range midpoints, identical every session and every spawn, so the
            // archetype-damage scalar GA_Fire reads (the base→pre-vital drift that
            // broke cross-session baselines) is pinned. Everywhere else this is
            // false and the existing rolled path is untouched. GrantStartingItems
            // is one-shot, so a bench respawn keeps the same canonical kit.
            //
            // The bench is a dev-only surface: in Shipping the whole canonical
            // branch is compiled out and only the plain legacy grant remains.
#if !UE_BUILD_SHIPPING
            const bool bBench = IsDevBenchLevel();
            if (bBench)
            {
                UE_LOG(LogVigilCombat, Log,
                    TEXT("Bench|CanonicalLoadout|ENGAGED|pawn=%s|map=%s"),
                    *GetName(), *UGameplayStatics::GetCurrentLevelName(this));
            }
            Inventory->GrantStartingItems(bBench);
#else
            Inventory->GrantStartingItems();
#endif
        }

        // Sync weapon slots with anything already equipped (e.g. after respawn).
        // Runs on clients as well as the server — a remote player's own slots
        // are read from replicated equipment, not granted.
        static const EGothicEquipSlot WeaponEquipSlots[] = {
            EGothicEquipSlot::Sidearm,
            EGothicEquipSlot::Piece,
            EGothicEquipSlot::Rig
        };
        for (int32 i = 0; i < WeaponSlots.Num() && i < 3; ++i)
        {
            if (const FGothicItemInstance* Equipped = Inventory->GetEquippedItem(WeaponEquipSlots[i]))
            {
                if (Equipped->Definition && Equipped->Definition->IsWeapon())
                {
                    WeaponSlots[i].WeaponData = Equipped->Definition->WeaponData;
                    WeaponSlots[i].InitFromData();
                }
            }
        }
        // ── The ammo restore, and it must be AFTER the sync loop ─────────────
        // InitFromData up there refills every magazine to capacity and every
        // reserve to its starting value. That is right for a first spawn and
        // wrong for a respawn, so the banked counts are replayed over the top of
        // it — put this before the loop and the refill simply overwrites it,
        // which is the whole failure this PR exists to fix.
        //
        // Authority-only because that is where the bank is written and where the
        // authoritative slots live. The owning client's own copy of the array is
        // caught up by the RPC below; see Client_RestoreAmmo.
        if (HasAuthority())
        {
            if (PS->HasCachedAmmo())
            {
                const TArray<FGothicAmmoSlotSnapshot> Banked = PS->ConsumeCachedAmmo();
                ApplyAmmoSnapshot(Banked);

                // Send the CLAMPED result, not the raw bank: the client would
                // otherwise clamp against its own view of the slot and the two
                // could disagree while equipment replication is still catching up.
                Client_RestoreAmmo(CaptureAmmoSnapshot());
            }
        }
        else if (PendingClientAmmoRestore.Num() > 0)
        {
            // The client half of the same ordering hazard. The RPC can land
            // before this pawn has ever run its own init pass, and that pass
            // refills through InitFromData exactly like the server's does — so
            // whatever the server sent is replayed here, at the same seam.
            ApplyAmmoSnapshot(PendingClientAmmoRestore);
        }

        RefreshWeaponVisuals(ActiveWeaponIndex);

        // The numbers changed under the HUD on both machines. Local-only inside,
        // so this is a no-op on a server pass for a remote pawn.
        PushAmmoToHUD();
    }

    LogGASInitComplete();
}

void AGothicPlayerCharacter::ScheduleGASInitRetry()
{
    // Bounded. A pawn still without a PlayerState after this many passes has a
    // problem a timer cannot fix, and an unbounded retry would spin for the
    // whole match logging nothing anyone reads.
    if (GASInitRetryCount >= MaxGASInitRetries)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("VigilTimeline|t=%.3f|%s|GASInit|ABANDONED|retries=%d|localCtrl=%d"),
            GASInitTimelineNow(this), *GetName(),
            GASInitRetryCount, IsLocallyControlled() ? 1 : 0);
        return;
    }

    ++GASInitRetryCount;
    GetWorldTimerManager().SetTimer(
        GASInitRetryTimer, this,
        &AGothicPlayerCharacter::InitGASFromPlayerState,
        GASInitRetryInterval, false);
}

void AGothicPlayerCharacter::LogGASInitComplete() const
{
    // One line per pawn per init pass. The two-player verification pass needs to
    // assert "BOTH pawns were granted" from a server-bound harness, and counting
    // abilities off the ASC is the only reading of that which cannot be confused
    // with a replication delay — this runs on whichever machine did the work,
    // and says which machine that was.
    const int32 AbilityCount =
        AbilitySystemComponent ? AbilitySystemComponent->GetActivatableAbilities().Num() : -1;

    // The ability slot map, which is what the eight granted abilities are
    // registered into. The line used to print the WEAPON slot count in a field
    // called `slots` immediately after `abilities`, and reading the two together
    // — "abilities=8|slots=1" — invited exactly the wrong conclusion. Both are
    // now named for what they count.
    const int32 AbilitySlots =
        AbilitySystemComponent ? AbilitySystemComponent->GetRegisteredAbilitySlotCount() : -1;

    int32 ArmedWeaponSlots = 0;
    for (const FGothicWeaponSlot& Slot : WeaponSlots)
    {
        if (Slot.WeaponData)
        {
            ++ArmedWeaponSlots;
        }
    }

    // `asc` and `auth` are the fields the two-player read needs. A count alone
    // cannot distinguish "this pawn was never granted" from "this pawn is
    // looking at a different ASC than the one that was granted", and telling
    // those apart is the entire remote-player bug — so the line names the ASC it
    // counted and says whether the machine printing it could have granted at all.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|GASInit|pawn=%s|abilities=%d|abilitySlots=%d|weaponSlots=%d|asc=%s|auth=%d|localCtrl=%d"),
        GASInitTimelineNow(this), *GetName(), *GetName(),
        AbilityCount, AbilitySlots, ArmedWeaponSlots,
        *GetNameSafe(AbilitySystemComponent ? AbilitySystemComponent->GetOwner() : nullptr),
        HasAuthority() ? 1 : 0, IsLocallyControlled() ? 1 : 0);
}

void AGothicPlayerCharacter::BindHUDAttributeDelegates()
{
    // Remove first, always. InitGASFromPlayerState is reached from BOTH
    // PossessedBy and OnRep_PlayerState, so this runs twice for every pawn that
    // is ever spawned — the duplicated "InputComponent not yet available"
    // warning a few lines up is the same double-entry seen from another angle.
    // Without the remove, every life registered two copies of each lambda and
    // the HUD was written twice per attribute change.
    UnbindHUDAttributeDelegates();

    if (!IsLocallyControlled() || !AbilitySystemComponent)
    {
        return;
    }

    BoundHUDAttributeASC = AbilitySystemComponent;

    // Health delegate — drives the health bar fill and number.
    HealthChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
        UGothicAttributeSet::GetHealthAttribute()).AddLambda(
        [this](const FOnAttributeChangeData& Data)
        {
            if (!IsLocallyControlled()) return;

            APlayerController* PC = Cast<APlayerController>(GetController());
            if (!PC) return;

            AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD());
            if (GothicHUD && AttributeSet)
            {
                GothicHUD->UpdateHealth(Data.NewValue, AttributeSet->GetMaxHealth());
            }

            // Taking damage is a combat action — enters/refreshes combat state
            // so Steadfast fills even during purely defensive play.
            if (Data.NewValue < Data.OldValue)
            {
                if (UGothicCombatStateComponent* Combat = FindComponentByClass<UGothicCombatStateComponent>())
                {
                    Combat->NotifyCombatAction();
                }
            }
        });

    // Selah delegate
    SelahChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
        UGothicAttributeSet::GetSelahAttribute()).AddLambda(
        [this](const FOnAttributeChangeData& Data)
        {
            if (!IsLocallyControlled()) return;

            APlayerController* PC = Cast<APlayerController>(GetController());
            if (!PC) return;

            AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD());
            if (GothicHUD)
            {
                GothicHUD->UpdateSelah(Data.NewValue);
            }
        });

    // Super meter delegate
    SuperMeterChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
        UGothicAttributeSet::GetSuperMeterAttribute()).AddLambda(
        [this](const FOnAttributeChangeData& Data)
        {
            if (!IsLocallyControlled()) return;

            APlayerController* PC = Cast<APlayerController>(GetController());
            if (!PC) return;

            AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD());
            if (GothicHUD && AttributeSet)
            {
                GothicHUD->UpdateSuperMeter(Data.NewValue, AttributeSet->GetMaxSuperMeter());
            }

            // Reckoning is teachable the moment it is spendable, and not before —
            // telling a player about a super they cannot fire is telling them
            // about a locked door. Edge-latched: SuperMeter sits pinned at max
            // until it is spent, and this delegate fires on every clamped write.
            if (AttributeSet)
            {
                const float MaxSuper = AttributeSet->GetMaxSuperMeter();
                const bool bNowFull = MaxSuper > 0.f && Data.NewValue >= MaxSuper;

                if (bNowFull && !bSuperMeterWasFull && HintManager)
                {
                    HintManager->ShowHint(GothicTags::Hint_Reckoning);
                }
                bSuperMeterWasFull = bNowFull;
            }
        });

    // Steadfast delegate. Added late — the attribute has filled since the first
    // build with nothing reading it, so the player was asked to spend a resource
    // the HUD never showed them. Drives the pip row AND the conversion hint.
    SteadfastChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
        UGothicAttributeSet::GetSteadfastAttribute()).AddLambda(
        [this](const FOnAttributeChangeData& Data)
        {
            if (!IsLocallyControlled()) return;

            APlayerController* PC = Cast<APlayerController>(GetController());
            if (!PC) return;

            AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD());
            if (GothicHUD && AttributeSet)
            {
                GothicHUD->UpdateSteadfast(Data.NewValue, AttributeSet->GetMaxSteadfast());
            }

            // The hint fires when the bar CAPS, not when it starts filling: a full
            // bar is the first moment holding reload is unambiguously worth it,
            // and it is also the first moment further fill is being wasted.
            if (AttributeSet)
            {
                const float MaxSteadfast = AttributeSet->GetMaxSteadfast();
                const bool bNowFull = MaxSteadfast > 0.f && Data.NewValue >= MaxSteadfast;

                if (bNowFull && !bSteadfastWasFull && HintManager)
                {
                    HintManager->ShowHint(GothicTags::Hint_SteadfastConvert);
                }
                bSteadfastWasFull = bNowFull;
            }
        });

    UE_LOG(LogTemp, Verbose,
        TEXT("GothicPlayerCharacter[%s]: HUD attribute delegates bound to ASC %s"),
        *GetName(), *GetNameSafe(AbilitySystemComponent));
}

void AGothicPlayerCharacter::UnbindHUDAttributeDelegates()
{
    UGothicAbilitySystemComponent* ASC = BoundHUDAttributeASC.Get();
    if (!ASC)
    {
        // Either nothing was ever bound, or the ASC itself is already gone —
        // in which case its delegate lists went with it and there is nothing
        // to detach from. Clear the handles either way so a later rebind
        // cannot try to remove them from a different ASC.
        HealthChangedHandle.Reset();
        SelahChangedHandle.Reset();
        SuperMeterChangedHandle.Reset();
        SteadfastChangedHandle.Reset();
        BoundHUDAttributeASC.Reset();
        return;
    }

    if (HealthChangedHandle.IsValid())
    {
        ASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
        HealthChangedHandle.Reset();
    }

    if (SelahChangedHandle.IsValid())
    {
        ASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetSelahAttribute()).Remove(SelahChangedHandle);
        SelahChangedHandle.Reset();
    }

    if (SuperMeterChangedHandle.IsValid())
    {
        ASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetSuperMeterAttribute()).Remove(SuperMeterChangedHandle);
        SuperMeterChangedHandle.Reset();
    }

    if (SteadfastChangedHandle.IsValid())
    {
        ASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetSteadfastAttribute()).Remove(SteadfastChangedHandle);
        SteadfastChangedHandle.Reset();
    }

    BoundHUDAttributeASC.Reset();
}

// ═══════════════════════════════════════════════════════════════════════════
// Stun — State.Stunned stops the player, not just the player's abilities
// ═══════════════════════════════════════════════════════════════════════════

void AGothicPlayerCharacter::BindStunTagListener()
{
    // Remove first, always — InitGASFromPlayerState runs twice per pawn
    // (PossessedBy and OnRep_PlayerState), and a doubled registration would
    // double-count the controller's ref-counted move-input ignore.
    UnbindStunTagListener();

    if (!AbilitySystemComponent)
    {
        return;
    }

    // Registered on server AND owning client — the two halves of the handler
    // live on different machines. Copies the registration pattern from
    // AGothicEnemyBase::BeginPlay, plus the handle bookkeeping the enemy does
    // not need: its ASC dies with it, ours lives on the PlayerState and would
    // keep calling the OLD pawn's handler between its death and its GC.
    BoundStunTagASC = AbilitySystemComponent;
    StunTagChangedHandle = AbilitySystemComponent->RegisterGameplayTagEvent(
        GothicTags::State_Stunned,
        EGameplayTagEventType::NewOrRemoved)
        .AddUObject(this, &AGothicPlayerCharacter::HandleStunTagChanged);
}

void AGothicPlayerCharacter::UnbindStunTagListener()
{
    // Whatever else happens, the controller gets its move input back. It
    // survives the pawn, so an ignore left behind by a pawn that died stunned
    // would arrive on the respawned pawn as a player who can never move again.
    ClearStunMoveInputIgnore();

    UGothicAbilitySystemComponent* ASC = BoundStunTagASC.Get();
    if (ASC && StunTagChangedHandle.IsValid())
    {
        ASC->RegisterGameplayTagEvent(
            GothicTags::State_Stunned,
            EGameplayTagEventType::NewOrRemoved).Remove(StunTagChangedHandle);
    }

    StunTagChangedHandle.Reset();
    BoundStunTagASC.Reset();
}

void AGothicPlayerCharacter::HandleStunTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
    const bool bStunned = NewCount > 0;

    // Server half — movement mode. CharacterMovement replicates it, so cutting
    // it here is what stops the pawn everywhere. Mirrors
    // AGothicEnemyBase::HandleStunTagChanged, minus the AI brain pause a
    // player-controlled pawn does not have.
    if (HasAuthority())
    {
        if (UCharacterMovementComponent* Move = GetCharacterMovement())
        {
            if (bStunned)
            {
                // StopMovementImmediately alone is not enough — held input
                // would re-issue velocity on the next tick, so the mode has to
                // go too.
                Move->StopMovementImmediately();
                Move->SetMovementMode(MOVE_None);
            }
            else if (!AbilitySystemComponent ||
                     !AbilitySystemComponent->HasMatchingGameplayTag(
                         GothicTags::State_Dead))
            {
                // Restore — but never over the death path. OnDeath's
                // DisableMovement also parks the pawn in MOVE_None, and a
                // player who dies mid-stun still has the 2s stun GE ticking on
                // the PlayerState ASC; its expiry lands here and must not put
                // a corpse back on its feet.
                Move->SetMovementMode(MOVE_Walking);
            }
        }
    }

    // Input half — runs wherever the controller lives, which on a dedicated
    // server is the owning client (the tag replicates to it). Move input only:
    // abilities are already blocked by the tag, and LOOK stays free on purpose,
    // because a stunned player watching the boss wind up is the entire point of
    // a telegraphed stun. Ignoring input rather than just zeroing the mode also
    // covers the owning client, where AddMovementInput would otherwise keep
    // feeding a prediction the server has to fight.
    if (bStunned)
    {
        // Controller can legitimately be null here — the respawn window — in
        // which case there is no input to ignore and MOVE_None already holds.
        if (AController* StunController = GetController())
        {
            StunController->SetIgnoreMoveInput(true);
            StunMoveIgnoredController = StunController;
        }
    }
    else
    {
        ClearStunMoveInputIgnore();
    }

    UE_LOG(LogTemp, Verbose, TEXT("Stun[%s]: %s"),
        *GetName(), bStunned ? TEXT("halted") : TEXT("resumed"));

    // HUD tell — local player only. Runs on the owning client (where the HUD
    // lives); on a dedicated server this pawn is not locally controlled and
    // PushStunStateToHUD no-ops. Read the GE's remaining time only on the gain
    // transition so the widget can drain an indicator; clear pushes 0.
    const float ExpectedDuration = bStunned ? GetStunRemainingDuration() : 0.f;
    PushStunStateToHUD(bStunned, ExpectedDuration);

    if (IsLocallyControlled())
    {
        UE_LOG(LogVigilCombat, Log,
            TEXT("VigilTimeline|t=%.3f|%s|Stun|HUD_%s|remaining=%.2f"),
            GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetName(),
            bStunned ? TEXT("STUNNED") : TEXT("CLEARED"), ExpectedDuration);
    }
}

void AGothicPlayerCharacter::PushStunStateToHUD(bool bStunned, float ExpectedDuration)
{
    if (!IsLocallyControlled())
    {
        return;
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (GothicHUD)
    {
        // No-ops inside the HUD until the layout widget exists; the HUD-ready
        // timer re-pushes the current state once it does.
        GothicHUD->NotifyStunnedStateChanged(bStunned, ExpectedDuration);
    }
}

float AGothicPlayerCharacter::GetStunRemainingDuration() const
{
    if (!AbilitySystemComponent)
    {
        return 0.f;
    }

    // Match any active effect whose granted (owning) tags include State.Stunned —
    // the stun GEs grant it. Duration GEs replicate to the owning client, so this
    // is valid on the machine the HUD lives on. Take the longest remaining if more
    // than one stun overlaps.
    FGameplayTagContainer StunTags;
    StunTags.AddTag(GothicTags::State_Stunned);

    const FGameplayEffectQuery Query =
        FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(StunTags);

    float MaxRemaining = 0.f;
    for (float Remaining : AbilitySystemComponent->GetActiveEffectsTimeRemaining(Query))
    {
        MaxRemaining = FMath::Max(MaxRemaining, Remaining);
    }
    return MaxRemaining;
}

void AGothicPlayerCharacter::ClearStunMoveInputIgnore()
{
    // Paid back on the controller that took it, not whoever GetController()
    // returns now — SetIgnoreMoveInput is ref-counted per controller, and an
    // unpossess between add and remove would otherwise strand the count at 1.
    if (AController* StunController = StunMoveIgnoredController.Get())
    {
        StunController->SetIgnoreMoveInput(false);
    }
    StunMoveIgnoredController.Reset();
}

void AGothicPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // THE respawn crash fix. The ASC lives on the PlayerState and survives this
    // pawn; the lambdas bound to it capture `this`. Leaving them attached means
    // the first attribute change after the dead pawn is collected calls into
    // freed memory. Unbinding here is what makes the pawn's death final as far
    // as the ASC is concerned.
    UnbindHUDAttributeDelegates();

    // Same lifetime problem, same fix — and this one also pays back the
    // controller's move-input ignore, which would otherwise outlive the pawn
    // and freeze the respawned one. See UnbindStunTagListener.
    UnbindStunTagListener();

    // Downed hygiene, and the same lifetime argument as the two unbinds above:
    // the timer is owned by the world and fires into `this`, and the downed flag
    // lives on the PlayerState, which outlives this pawn. A player who
    // disconnects, travels, or is otherwise unpossessed while down would leave
    // both behind — a timer calling into a dead pawn, and a PlayerState that
    // still reads downed for whatever pawn comes next.
    //
    // The flag is only cleared on the authority (SetDowned refuses elsewhere) and
    // only if it is actually set, so a normal pawn teardown does nothing here.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ReviveWindowTimer);
    }

    if (HasAuthority())
    {
        // Identical argument for the channel: ReviveChannelTimer is world-owned and
        // fires into `this`, and the channel state it drives lives on two
        // PlayerStates that both outlive this pawn. A reviver who dies, disconnects
        // or travels mid-channel would otherwise leave a bar filling on the downed
        // player's screen forever, driven by nothing.
        CancelReviveChannel(TEXT("reviver-pawn-endplay"));

        if (AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>())
        {
            PS->SetDowned(false);
        }
    }

    Super::EndPlay(EndPlayReason);
}

void AGothicPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);


    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!EIC)
    {
        UE_LOG(LogTemp, Error, TEXT("GothicPlayerCharacter: Failed to cast to UEnhancedInputComponent"));
        return;
    }

    // Movement — direct bindings, not through GAS
    if (MoveAction)
        EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AGothicPlayerCharacter::OnMove);

    if (LookAction)
        EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AGothicPlayerCharacter::OnLook);

    if (JumpAction)
    {
        EIC->BindAction(JumpAction, ETriggerEvent::Started,   this, &ACharacter::Jump);
        EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
    }

    // Neither Fire nor Melee is bound here. Both route through
    // DA_GothicInputConfig — IA_Fire as Input.Ability.PrimaryFire -> GA_Fire,
    // IA_Melee as Input.Ability.Melee -> GA_HuntersStrike.
    //
    // Their predecessors (OnFire here, OnMelee on BP_GothicPlayerCharacter's
    // event graph) each ran *in addition* to the ability on every input: an
    // ECC_Pawn capsule trace applying a flat PistolDamage/MeleeDamage with no
    // authority check, no ammo cost, no cooldown and no vital-point resolution.
    // Because the trace hit the capsule rather than the mesh, they also dealt
    // damage on shots the ability itself counted as a miss. Both removed.

    if (SprintAction)
    {
        EIC->BindAction(SprintAction, ETriggerEvent::Started,   this, &AGothicPlayerCharacter::OnSprintStarted);
        EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AGothicPlayerCharacter::OnSprintStopped);
    }

    // Interact — a revive channel is a HOLD, so this now needs both edges, the same
    // shape as reload/sprint/ADS above. The Selah collect still happens on the press
    // alone and simply ignores the release.
    if (InteractAction)
    {
        EIC->BindAction(InteractAction, ETriggerEvent::Started,   this, &AGothicPlayerCharacter::OnInteract);
        EIC->BindAction(InteractAction, ETriggerEvent::Completed, this, &AGothicPlayerCharacter::OnInteractReleased);
    }

    // Weapon slot swap — 1/2/3 keys
    if (WeaponSlot1Action)
        EIC->BindAction(WeaponSlot1Action, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnWeaponSlot1);
    if (WeaponSlot2Action)
        EIC->BindAction(WeaponSlot2Action, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnWeaponSlot2);
    if (WeaponSlot3Action)
        EIC->BindAction(WeaponSlot3Action, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnWeaponSlot3);

    // Reload — press starts the hold, release decides tap vs. already-converted
    if (ReloadAction)
    {
        EIC->BindAction(ReloadAction, ETriggerEvent::Started,   this, &AGothicPlayerCharacter::OnReloadPressed);
        EIC->BindAction(ReloadAction, ETriggerEvent::Completed, this, &AGothicPlayerCharacter::OnReloadReleased);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: ReloadAction not assigned — reload and Steadfast conversion cannot fire. Assign IA_Reload in BP_GothicPlayerCharacter."));
    }

    // Inventory screen toggle
    if (InventoryToggleAction)
    {
        EIC->BindAction(InventoryToggleAction, ETriggerEvent::Started, this, &AGothicPlayerCharacter::ToggleInventory);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: InventoryToggleAction not assigned — inventory cannot be opened. Assign IA_InventoryToggle in BP_GothicPlayerCharacter."));
    }

    // Aim down sights — hold, so it needs both edges like sprint and reload
    if (ADSAction)
    {
        EIC->BindAction(ADSAction, ETriggerEvent::Started,   this, &AGothicPlayerCharacter::OnADSPressed);
        EIC->BindAction(ADSAction, ETriggerEvent::Completed, this, &AGothicPlayerCharacter::OnADSReleased);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: ADSAction not assigned — aiming does nothing. Assign IA_ADS in BP_GothicPlayerCharacter."));
    }

    // Quit menu toggle
    if (QuitMenuAction)
    {
        EIC->BindAction(QuitMenuAction, ETriggerEvent::Started, this, &AGothicPlayerCharacter::ToggleQuitMenu);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: QuitMenuAction not assigned — the quit menu cannot be opened and a packaged build cannot be exited. Assign IA_QuitMenu in BP_GothicPlayerCharacter."));
    }

    // Ability inputs go through InputHandler → ASC tag pipeline
    // ASC may not be ready here — bindings are set up again in InitGASFromPlayerState
    if (InputHandler && AbilitySystemComponent)
    {
        InputHandler->SetupAbilityInputBindings(EIC, AbilitySystemComponent);
    }
}

void AGothicPlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Fell through the map. This is the reliable death-floor trigger: the
    // engine's KillZ usually sits far below any real floor, so a player who
    // clips through geometry falls forever and never reaches it. Catch them at
    // a level-tunable height and respawn. Server-authoritative.
    if (HasAuthority() && GetActorLocation().Z < FallRespawnZ)
    {
        TriggerFallRespawn();
        return;
    }

    // Stillness clock for Steady Read. Stamped on every authority-and-local frame
    // the pawn is moving, so the perk's "no movement in the last 0.5s" is one
    // subtraction at the fire site rather than a timer that has to be started and
    // cancelled from the movement input handlers.
    if (IsMovingUnderOwnPower())
    {
        LastMovingWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    }

    // Ease the FOV toward the aim state. Interpolated rather than snapped: a hard
    // FOV cut reads as a glitch, and the ~0.15s pull-in is most of what makes
    // aiming feel like a weapon settling rather than a zoom toggle.
    if (IsLocallyControlled() && FirstPersonCamera)
    {
        const float TargetFOV = bIsAiming ? ADSFieldOfView : HipFieldOfView;
        const float CurrentFOV = FirstPersonCamera->FieldOfView;
        if (!FMath::IsNearlyEqual(CurrentFOV, TargetFOV, 0.05f))
        {
            FirstPersonCamera->SetFieldOfView(FMath::FInterpTo(
                CurrentFOV, TargetFOV, DeltaTime, FMath::Max(0.1f, ADSFieldOfViewInterpSpeed)));
        }
    }

    if (IsLocallyControlled())
    {
        // Ahead of the bHUDReady early-return below: the weapon is visible from the
        // first frame, and the HUD has nothing to do with it.
        UpdateFirstPersonWeaponPose(DeltaTime);

        // Recoil lives on control rotation, so its recovery has to as well — the
        // weapon-pose kick above is a separate, purely cosmetic spring.
        TickRecoilRecovery(DeltaTime);

        // Revive first: it owns the prompt slot ahead of the Selah collect, and
        // UpdateSelahInteractPrompt reads ShownRevivePromptPawn to yield. A body on
        // the floor beats a meditation you can take any time, and the Selah prompt's
        // range (1200uu, the encounter's own MeditationRange) covers most of an
        // arena — without the ordering it would starve the revive prompt outright.
        UpdateRevivePrompt();
        UpdateSelahInteractPrompt();

        UpdateReviveChannelHUD();
    }

    if (!IsLocallyControlled() || !AbilitySystemComponent || !bHUDReady) return;

    APlayerController* PC = Cast<APlayerController>(GetController());
    AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (!GothicHUD) return;

    UGothicHUDWidget* HUDWidget = GothicHUD->GetHUDWidget();
    if (!HUDWidget) return;

    // Poll cooldown for each ability slot — totals pulled from actual GE durations
    static const EGothicAbilitySlot SlotsToTrack[] = {
        EGothicAbilitySlot::LightAttack,
        EGothicAbilitySlot::Ability1,
        EGothicAbilitySlot::Ability2,
        EGothicAbilitySlot::Ability3,
    };

    for (int32 i = 0; i < UE_ARRAY_COUNT(SlotsToTrack); ++i)
    {
        const float Remaining = AbilitySystemComponent->GetCooldownRemainingForSlot(SlotsToTrack[i]);
        const float Total     = AbilitySystemComponent->GetCooldownTotalForSlot(SlotsToTrack[i]);
        GothicHUD->UpdateAbilityCooldown(SlotsToTrack[i], Remaining, Total);
    }
}

void AGothicPlayerCharacter::OnMove(const FInputActionValue& Value)
{
    // EnhancedInput keeps firing Move even under SetInputModeUIOnly, so opening
    // an interact panel or the inventory wouldn't otherwise stop the player. The
    // mouse cursor is only ever shown while a blocking UI is up in this project,
    // so it's a reliable "in a menu" signal — gate movement on it here, once,
    // instead of in every interactable.
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (PC->bShowMouseCursor)
        {
            return;
        }
    }

    // Self-heal a stranded lock. bSelahMomentLock must never outlive its release
    // timer: TriggerSelahMoment always arms SelahMomentLockHandle as the fallback,
    // and EndSelahMomentLock clears them together. If the flag is set but the timer
    // is gone, the release path was lost (a widget completion event that never
    // arrived after its widget was torn down mid-cycle, a collect whose finalize
    // engaged the lock while a second overlapping volume's fight was still live)
    // and the player would be frozen for the rest of the run — the reported
    // softlock. Releasing here bounds any strand to a single input event.
    if (bSelahMomentLock && !GetWorldTimerManager().IsTimerActive(SelahMomentLockHandle))
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("VigilTimeline|t=%.3f|%s|Selah|LOCK_SELFHEAL|release-timer-inactive"),
            GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetName());
        EndSelahMomentLock();
    }

    // The Selah moment holds you in place. Same reasoning as the cursor gate above:
    // refuse the input here, once, rather than in every system that could move.
    if (bSelahMomentLock)
    {
        return;
    }

    // Downed players do not walk. DisableMovement in EnterDownedState is the
    // authoritative half; this is the owning client's half, and it needs to exist
    // separately because IsDowned() reads the REPLICATED flag on the PlayerState
    // and is therefore true on the client too, whereas the movement mode set on
    // the server is not what the client's own input path consults.
    if (IsDowned())
    {
        return;
    }

    const FVector2D MoveVec = Value.Get<FVector2D>();
    if (Controller)
    {
        const FRotator Rotation    = Controller->GetControlRotation();
        const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);

        const FVector ForwardDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        const FVector RightDir   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

        AddMovementInput(ForwardDir, MoveVec.Y);
        AddMovementInput(RightDir,   MoveVec.X);
    }

    // Placed AFTER the gates above on purpose: a Move event swallowed by the
    // cursor gate or the Selah lock is not the player moving, and counting it
    // would suppress the opener for a player who never actually walked. The
    // deadzone check keeps a stick at rest from counting as movement.
    if (HintManager && !MoveVec.IsNearlyZero())
    {
        HintManager->NotifyMoveInput();
    }
}

void AGothicPlayerCharacter::OnLook(const FInputActionValue& Value)
{
    const FVector2D LookVec = Value.Get<FVector2D>();
    AddControllerYawInput(LookVec.X);
    AddControllerPitchInput(LookVec.Y);

    if (HintManager && !LookVec.IsNearlyZero())
    {
        HintManager->NotifyLookInput();
    }

    // Recover only what the player has NOT already pulled back themselves. Look
    // input opposing the outstanding kick is the player compensating manually;
    // burning it off here stops the automatic recovery from correcting a second
    // time and dragging their aim past the target.
    if (!FMath::IsNearlyZero(PendingRecoilPitch) && LookVec.Y != 0.f)
    {
        const float Opposing = FMath::Sign(PendingRecoilPitch) * LookVec.Y;
        if (Opposing < 0.f)
        {
            const float Compensated = FMath::Min(FMath::Abs(LookVec.Y), FMath::Abs(PendingRecoilPitch));
            PendingRecoilPitch -= FMath::Sign(PendingRecoilPitch) * Compensated;
        }
    }
}

void AGothicPlayerCharacter::OnSprintStarted()
{
    SetSprinting(true);
}

void AGothicPlayerCharacter::OnSprintStopped()
{
    SetSprinting(false);
}

void AGothicPlayerCharacter::SetSprinting(bool bNewSprinting)
{
    if (bIsSprinting == bNewSprinting)
    {
        return;
    }

    bIsSprinting = bNewSprinting;
    SyncSprintTag();

    // The opportunity cost, taught on the first sprint and never again. Fired on
    // the START edge only — this function is the single funnel for every sprint
    // path, so the stop edge would double it. Answering it is impossible by
    // definition (the hint IS the consequence), so it always runs its full
    // duration; no NotifyHintActionPerformed pairs with it.
    if (bIsSprinting && HintManager)
    {
        HintManager->ShowHint(GothicTags::Hint_Sprint);
        HintManager->ShowHint(GothicTags::Hint_SprintLowersGun);
    }

    // A sprint abandons any Steadfast hold in progress. The hold is a gun action
    // and the sprint just took the gun away, so leaving its timer running would
    // pay out a conversion mid-sprint through a path the block never sees.
    if (bIsSprinting && bSprintBlocksGunActions)
    {
        EndSteadfastHold();
        bSteadfastHoldThresholdReached = false;

        // The gun just went down mid-hold — the reload window is abandoned, and the
        // release that eventually comes is blocked by AreGunActionsBlocked, so it
        // would never close the window. Close it here.
        SetReloadingHold(false);
    }

    RefreshMovementSpeed();
}

void AGothicPlayerCharacter::SyncSprintTag()
{
    if (!AbilitySystemComponent)
    {
        return; // pre-possession; InitGASFromPlayerState re-syncs once the ASC arrives
    }

    AbilitySystemComponent->SetLooseGameplayTagCount(
        GothicTags::State_Sprinting, bIsSprinting ? 1 : 0);
}

void AGothicPlayerCharacter::CancelSprintForAbility()
{
    // Only the gun is blocked by a sprint; everything else breaks it instead.
    // Guarded on the same switch as the block so one checkbox restores the whole
    // of the old behaviour.
    if (bSprintBlocksGunActions)
    {
        SetSprinting(false);
    }
}

void AGothicPlayerCharacter::OnInteract()
{
    // A downed player has no interactions. Their movement is off and their
    // abilities are cancelled; letting them collect Selah or start a revive from
    // the floor would be the one thing that still worked.
    if (IsDowned())
    {
        return;
    }

    // Revive takes priority over the collect, for the same reason the prompt does.
    if (AGothicPlayerCharacter* Target = FindReviveTargetInRange())
    {
        ServerStartReviveChannel(Target);
        return;
    }

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;

    // Nearest pending encounter that covers us — NOT "whichever completed last".
    // With overlapping volumes the single-slot version handed us a prompt anchored
    // to a different encounter entirely, and collecting ran against that one.
    AGothicEncounterVolume* Enc = GS ? GS->GetPromptEncounterFor(GetActorLocation()) : nullptr;
    if (!Enc)
    {
        UE_LOG(LogTemp, Warning, TEXT("Selah: interact pressed but no meditation prompt covers this spot (%d pending)"),
            GS ? (GS->HasPendingPrompt() ? 1 : 0) : -1);
        return;
    }

    UE_LOG(LogTemp, Verbose, TEXT("Selah: interact accepted at %.0fuu from %s"),
        FVector::Dist(GetActorLocation(), Enc->GetActorLocation()), *Enc->GetName());
    ServerCollectEncounterSelah(Enc);
}

void AGothicPlayerCharacter::ServerCollectEncounterSelah_Implementation(AGothicEncounterVolume* Enc)
{
    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;

    // Trust nothing from the client: the encounter must still be offering a prompt.
    if (!GS || !Enc || !GS->IsPromptPending(Enc))
    {
        return;
    }

    // Server-side range recheck (a little slack) so a client can't collect from afar.
    if (FVector::Dist(GetActorLocation(), Enc->GetActorLocation()) > Enc->GetMeditationRange() * 1.25f)
    {
        return;
    }

    Enc->CompleteCollection();
}

void AGothicPlayerCharacter::UpdateSelahInteractPrompt()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    AGothicHUD* HUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (!HUD)
    {
        return;
    }

    // Yield the whole slot to a revive prompt. UpdateRevivePrompt ran first this
    // tick and has already claimed it if a body is in range.
    if (ShownRevivePromptPawn.IsValid())
    {
        if (ShownSelahPromptCorpse.IsValid())
        {
            HUD->ClearInteractPrompt(ShownSelahPromptCorpse.Get());
            ShownSelahPromptCorpse = nullptr;
        }
        return;
    }

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
    AGothicEncounterVolume* Enc = GS ? GS->GetPromptEncounterFor(GetActorLocation()) : nullptr;

    if (Enc)
    {
        // Yield to any other interactable that already owns the prompt slot.
        //
        // The HUD keeps ONE prompt with one owner, and this runs every tick over a
        // radius the size of the whole arena. Asserting unconditionally meant
        // "Collect Selah" overwrote the Contract Board's prompt every single frame, and
        // the board's own ClearInteractPrompt was rejected because it wasn't the
        // owner — every interactable inside a completed encounter went dead.
        AActor* CurrentOwner = HUD->GetInteractPromptOwner();
        if (!CurrentOwner || CurrentOwner == Enc)
        {
            HUD->SetInteractPrompt(Enc, FText::FromString(TEXT("Collect Selah")));
            ShownSelahPromptCorpse = Enc;
        }
    }
    else if (ShownSelahPromptCorpse.IsValid())
    {
        HUD->ClearInteractPrompt(ShownSelahPromptCorpse.Get());
        ShownSelahPromptCorpse = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Channelled revive (PR-3)
//
// The whole feature is four pieces: a proximity search for a body, a two-edge
// input that opens and closes a server-side channel, a 0.1s authoritative timer
// that fills a replicated bar and tests four cancellation conditions, and a
// completion that calls the ReviveFromDowned that already existed.
//
// What is deliberately NOT here: the revive WINDOW is not paused while a channel
// runs. That is the simpler of the two honest readings and the one this PR takes
// — one clock, started when the player went down, and it is the player's whole
// budget. A channel that starts at 28s into a 30s window loses to the bleed-out,
// and the loss is handled rather than special-cased: the window fires, the target
// stops being downed, and the next channel tick sees that and breaks cleanly.
// Pausing it would mean a body could be held alive indefinitely by a reviver who
// never finishes, which is a strictly worse failure than losing a late pickup.
// ---------------------------------------------------------------------------

AGothicPlayerCharacter* AGothicPlayerCharacter::FindReviveTargetInRange() const
{
    const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    if (!GS)
    {
        return nullptr;
    }

    // Nearest, not first. Two bodies side by side is the exact case where "first in
    // PlayerArray" would silently pick the one you are not looking at.
    AGothicPlayerCharacter* Best = nullptr;
    float BestDistSq = FMath::Square(FMath::Max(50.f, ReviveChannelRange));

    for (const APlayerState* PS : GS->PlayerArray)
    {
        const AGothicPlayerState* GothicPS = Cast<const AGothicPlayerState>(PS);
        if (!GothicPS || GothicPS == GetPlayerState() || !GothicPS->IsDowned())
        {
            continue;
        }

        AGothicPlayerCharacter* Candidate = Cast<AGothicPlayerCharacter>(GothicPS->GetPawn());
        if (!IsValid(Candidate) || Candidate == this)
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation());
        if (DistSq <= BestDistSq)
        {
            BestDistSq = DistSq;
            Best = Candidate;
        }
    }

    return Best;
}

void AGothicPlayerCharacter::OnInteractReleased()
{
    // Unconditional: the server is the only thing that knows whether a channel is
    // actually open, and it no-ops when there is not one. Gating this on a local
    // guess is how a client ends up holding a channel it thinks it released.
    ServerEndReviveChannel();
}

void AGothicPlayerCharacter::ServerStartReviveChannel_Implementation(AGothicPlayerCharacter* Target)
{
    StartReviveChannel(Target);
}

void AGothicPlayerCharacter::ServerEndReviveChannel_Implementation()
{
    CancelReviveChannel(TEXT("released"));
}

void AGothicPlayerCharacter::StartReviveChannel(AGothicPlayerCharacter* Target)
{
    if (!HasAuthority())
    {
        return;
    }

    // Trust nothing the client sent — every one of these is re-derived here.
    if (!IsValid(Target) || Target == this || !Target->IsDowned())
    {
        return;
    }

    if (!IsAlive() || IsDowned())
    {
        return;
    }

    AGothicPlayerState* TargetPS = Target->GetPlayerState<AGothicPlayerState>();
    AGothicPlayerState* SelfPS   = GetPlayerState<AGothicPlayerState>();
    if (!TargetPS || !SelfPS)
    {
        return;
    }

    // Somebody is already on this body. PR-3 does not stack revivers — see the
    // header. Refusing here rather than silently taking the slot means the first
    // reviver's channel is never quietly reset by a second player walking up.
    if (TargetPS->IsReviveChannelActive())
    {
        return;
    }

    if (FVector::Dist(GetActorLocation(), Target->GetActorLocation()) > ReviveChannelRange)
    {
        return;
    }

    // Re-entrancy: a press with a channel already running restarts it on the same
    // body rather than stacking a second timer.
    CancelReviveChannel(TEXT("restarted"));

    ReviveChannelTargetPawn  = Target;
    ReviveChannelElapsed     = 0.f;
    ReviveChannelAnchor      = GetActorLocation();
    ReviveChannelLastHealth  = AttributeSet ? AttributeSet->GetHealth() : 0.f;

    SelfPS->SetReviveChannelTarget(TargetPS);
    TargetPS->BeginReviveChannel(SelfPS);

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            ReviveChannelTimer, this, &AGothicPlayerCharacter::TickReviveChannel,
            ReviveChannelTickInterval, true);
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("VigilTimeline|t=%.3f|%s|ReviveChannel|START|target=%s|duration=%.1fs"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetNameSafe(this),
        *GetNameSafe(Target), FMath::Max(0.2f, ReviveChannelSeconds));
}

void AGothicPlayerCharacter::CancelReviveChannel(const FString& Reason)
{
    if (!HasAuthority() || !ReviveChannelTargetPawn.IsValid())
    {
        return;
    }

    AGothicPlayerCharacter* Target = ReviveChannelTargetPawn.Get();
    ReviveChannelTargetPawn.Reset();

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ReviveChannelTimer);
    }

    if (AGothicPlayerState* TargetPS = Target ? Target->GetPlayerState<AGothicPlayerState>() : nullptr)
    {
        TargetPS->EndReviveChannel(/*bCompleted*/ false);
    }

    // Belt and braces. EndReviveChannel above clears the back-pointer through the
    // reviver it recorded, but a target whose PlayerState has already gone (a
    // disconnect mid-channel) would leave ours pointing at nothing.
    if (AGothicPlayerState* SelfPS = GetPlayerState<AGothicPlayerState>())
    {
        SelfPS->SetReviveChannelTarget(nullptr);
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("VigilTimeline|t=%.3f|%s|ReviveChannel|CANCEL|reason=%s|progress=%.2f"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetNameSafe(this),
        *Reason, ReviveChannelElapsed / FMath::Max(0.2f, ReviveChannelSeconds));

    ReviveChannelElapsed = 0.f;
}

void AGothicPlayerCharacter::TickReviveChannel()
{
    if (!HasAuthority())
    {
        return;
    }

    AGothicPlayerCharacter* Target = ReviveChannelTargetPawn.Get();

    // The target stopped being a valid revive: revived by someone else, bled out
    // (the un-paused window doing its job), or destroyed.
    if (!IsValid(Target) || !Target->IsDowned())
    {
        CancelReviveChannel(TEXT("target-no-longer-downed"));
        return;
    }

    // The reviver went down or died mid-channel.
    if (!IsAlive() || IsDowned())
    {
        CancelReviveChannel(TEXT("reviver-down"));
        return;
    }

    // Took a hit. See ReviveChannelLastHealth for why this is a poll. A HEAL is
    // explicitly not an interrupt, which is why this compares against the previous
    // tick rather than against the health the channel opened at.
    const float Health = AttributeSet ? AttributeSet->GetHealth() : ReviveChannelLastHealth;
    if (Health < ReviveChannelLastHealth - KINDA_SMALL_NUMBER)
    {
        CancelReviveChannel(TEXT("damaged"));
        return;
    }
    ReviveChannelLastHealth = Health;

    // Moved off the body.
    if (FVector::Dist(GetActorLocation(), ReviveChannelAnchor) > ReviveChannelMoveTolerance)
    {
        CancelReviveChannel(TEXT("moved"));
        return;
    }

    // Separate from the displacement test above: this one catches the BODY moving
    // away (ragdoll settle, a physics shove) rather than the reviver.
    if (FVector::Dist(GetActorLocation(), Target->GetActorLocation()) > ReviveChannelRange)
    {
        CancelReviveChannel(TEXT("out-of-range"));
        return;
    }

    ReviveChannelElapsed += ReviveChannelTickInterval;

    const float Duration = FMath::Max(0.2f, ReviveChannelSeconds);
    const float Progress = FMath::Clamp(ReviveChannelElapsed / Duration, 0.f, 1.f);

    if (AGothicPlayerState* TargetPS = Target->GetPlayerState<AGothicPlayerState>())
    {
        TargetPS->SetReviveChannelProgress(Progress);
    }

    if (Progress >= 1.f)
    {
        CompleteReviveChannel();
    }
}

void AGothicPlayerCharacter::CompleteReviveChannel()
{
    if (!HasAuthority())
    {
        return;
    }

    AGothicPlayerCharacter* Target = ReviveChannelTargetPawn.Get();
    ReviveChannelTargetPawn.Reset();
    ReviveChannelElapsed = 0.f;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ReviveChannelTimer);
    }

    // ORDER MATTERS. The channel is stamped COMPLETED before ReviveFromDowned runs,
    // because ReviveFromDowned calls SetDowned(false), and SetDowned's own cleanup
    // closes any still-open channel as an INTERRUPT. Stamping first makes that
    // cleanup a no-op and leaves the correct result replicated for both bars.
    if (AGothicPlayerState* TargetPS = Target ? Target->GetPlayerState<AGothicPlayerState>() : nullptr)
    {
        TargetPS->EndReviveChannel(/*bCompleted*/ true);
    }

    if (AGothicPlayerState* SelfPS = GetPlayerState<AGothicPlayerState>())
    {
        SelfPS->SetReviveChannelTarget(nullptr);
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("VigilTimeline|t=%.3f|%s|ReviveChannel|COMPLETE|target=%s"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetNameSafe(this),
        *GetNameSafe(Target));

    if (IsValid(Target))
    {
        Target->ReviveFromDowned();
    }
}

void AGothicPlayerCharacter::UpdateRevivePrompt()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    AGothicHUD* HUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (!HUD)
    {
        return;
    }

    AGothicPlayerCharacter* Target = IsDowned() ? nullptr : FindReviveTargetInRange();

    if (Target)
    {
        // Yield to a genuinely different interactable (a door, the Contract Board),
        // but NOT to the Selah prompt, which this pawn raised itself and which
        // covers most of an arena. Without that exception a body inside a completed
        // encounter could never be picked up.
        AActor* CurrentOwner = HUD->GetInteractPromptOwner();
        const bool bCanClaim =
            !CurrentOwner || CurrentOwner == Target || CurrentOwner == ShownSelahPromptCorpse.Get();

        if (bCanClaim)
        {
            const AGothicPlayerState* TargetPS = Target->GetPlayerState<AGothicPlayerState>();
            const FString Name = TargetPS ? TargetPS->GetPlayerName() : TEXT("teammate");

            HUD->SetInteractPrompt(Target, FText::FromString(FString::Printf(TEXT("Revive %s"), *Name)));
            ShownRevivePromptPawn = Target;
            return;
        }
    }

    if (ShownRevivePromptPawn.IsValid())
    {
        HUD->ClearInteractPrompt(ShownRevivePromptPawn.Get());
        ShownRevivePromptPawn = nullptr;
    }
}

void AGothicPlayerCharacter::UpdateReviveChannelHUD()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    AGothicHUD* HUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (!HUD)
    {
        return;
    }

    AGothicPlayerState* MyPS = GetPlayerState<AGothicPlayerState>();

    // Which channel concerns THIS viewer? Exactly two answers, and both are read
    // off replicated PlayerState data that every client has: one where we are the
    // body, one where we are the hands. Anybody else's revive is not our bar.
    AGothicPlayerState* Subject = nullptr;
    if (MyPS)
    {
        if (MyPS->IsReviveChannelActive())
        {
            Subject = MyPS;
        }
        else if (AGothicPlayerState* TargetPS = MyPS->GetReviveChannelTarget())
        {
            if (TargetPS->IsReviveChannelActive())
            {
                Subject = TargetPS;
            }
        }
    }

    if (Subject)
    {
        ShownReviveChannelSubject = Subject;
        HUD->ShowReviveChannel(Subject, Subject->GetReviveChannelReviver(),
                               Subject->GetReviveChannelProgress());
        return;
    }

    if (HUD->IsReviveChannelShown())
    {
        // The result is read off the subject we cached rather than off MyPS's
        // back-pointer, which the authority has already cleared by now — that is
        // the whole reason the cache exists. Result 2 is completed; anything else
        // (including a subject that vanished entirely) reads as interrupted.
        const AGothicPlayerState* Last = ShownReviveChannelSubject.Get();
        HUD->HideReviveChannel(Last && Last->GetReviveChannelResult() == 2);
        ShownReviveChannelSubject = nullptr;
    }
}

void AGothicPlayerCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    // Build the classic FP assembly on the LIVE instance, now that the Blueprint's serialized
    // component hierarchy has been applied. Runtime enforcement is what makes the assembly
    // deterministic — the BP's serialized hierarchy can override a constructor-only attach
    // (the banked trap); doing it before BeginPlay guarantees the eye and arms are seated
    // before BeginPlay's RefreshWeaponVisuals / arms-pose work reads the camera.
    EnforceFirstPersonCameraMount();

    // Hide the two RETIRED legacy guns ("UWeaponMesh" ghost + native "WeaponMesh") now that
    // the BP hierarchy (and the pointer redirect) has serialized, so only the hand-born
    // FPWeaponMesh renders. See NeutralizeDuplicateWeaponMesh.
    NeutralizeDuplicateWeaponMesh();

    // Earliest deterministic pass at the FP-mesh visibility gate. On a remote proxy
    // IsLocallyControlled() is already reliably false here, so the owner-only full-body FP
    // mesh is hidden before it can render even one frame; the owning pawn's later
    // PossessedBy/OnRep_Controller pass flips it visible once locality resolves true.
    UpdateFirstPersonVisibility();

    // NO tick prerequisite. #90 forced the arms to tick after the TP body they CopyPose'd
    // from; the classic assembly does not CopyPose (AnimClass = None), so that source
    // dependency no longer exists and pinning the tick order would be dead weight.
}

void AGothicPlayerCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // Editor-only design-time preview: put PreviewWeaponMesh in the hand so the BP editor
    // viewport shows a gun at design time. Gated on a NON-GAME world (IsGameWorld() is false
    // for the editor/preview worlds OnConstruction runs in when a BP is edited, true in
    // PIE/standalone/dedicated) — this is the "no runtime weapon is active" gate: at real
    // spawn the equip flow (RefreshWeaponVisuals) owns FPWeaponMesh's mesh and an in-game
    // empty slot clears it to null, never back to this preview. NO hardcoded asset path;
    // PreviewWeaponMesh is assigned on BP_GothicPlayerCharacter or left null (renders nothing).
    const UWorld* World = GetWorld();
    if (PreviewWeaponMesh && FPWeaponMesh && (World == nullptr || !World->IsGameWorld()))
    {
        FPWeaponMesh->SetStaticMesh(PreviewWeaponMesh);
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|FPGun|PREVIEW|mesh=%s"),
            GASInitTimelineNow(this), *GetName(), *PreviewWeaponMesh->GetName());
    }
}

void AGothicPlayerCharacter::EnforceFirstPersonCameraMount()
{
    if (!FirstPersonCamera)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("VigilTimeline|t=%.3f|%s|FPCamera|MOUNT|result=refused|reason=no-camera-component"),
            GASInitTimelineNow(this), *GetName());
        return;
    }

    USkeletalMeshComponent* Arms = FirstPersonArmsMesh;

    // Step 1 — seat the camera on the CAPSULE at the historical eye seat. Camera FIRST, so
    // any stale warp-era camera-under-arms parentage is broken here, before step 2 re-seats
    // the arms under the camera — that ordering is what keeps the arms->camera attach from
    // ever cycling. The AttachToComponent bool result is checked; a refusal never reads as
    // success (the census pattern).
    const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const bool bCameraSeated = FirstPersonCamera->AttachToComponent(
        GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    FirstPersonCamera->SetRelativeLocation(FVector(20.f, 0.f, 170.f - CapsuleHalfHeight));

    if (bCameraSeated)
    {
        UE_LOG(LogVigilCombat, Log,
            TEXT("VigilTimeline|t=%.3f|%s|FPCamera|MOUNT|step=camera-capsule|result=success|parent=%s|z=%.1f"),
            GASInitTimelineNow(this), *GetName(),
            *GetNameSafe(FirstPersonCamera->GetAttachParent()), 170.f - CapsuleHalfHeight);
    }
    else
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("VigilTimeline|t=%.3f|%s|FPCamera|MOUNT|step=camera-capsule|result=refused|parent=%s|z=%.1f"),
            GASInitTimelineNow(this), *GetName(),
            *GetNameSafe(FirstPersonCamera->GetAttachParent()), 170.f - CapsuleHalfHeight);
    }

    // Step 2 — attach the arms to the camera at the ArmsOffset/ArmsRotation seat. As a camera
    // child the arms inherit view yaw AND pitch rigidly. Result checked; on a null arms
    // component the step is skipped with one Warning rather than silently doing nothing.
    if (Arms)
    {
        const bool bArmsMounted = Arms->AttachToComponent(
            FirstPersonCamera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        Arms->SetRelativeLocationAndRotation(ArmsOffset, ArmsRotation);

        if (bArmsMounted)
        {
            UE_LOG(LogVigilCombat, Log,
                TEXT("VigilTimeline|t=%.3f|%s|FPCamera|MOUNT|step=arms-camera|result=success|parent=%s|offset=%s|rot=%s"),
                GASInitTimelineNow(this), *GetName(),
                *GetNameSafe(Arms->GetAttachParent()), *ArmsOffset.ToString(), *ArmsRotation.ToString());
        }
        else
        {
            UE_LOG(LogVigilCombat, Warning,
                TEXT("VigilTimeline|t=%.3f|%s|FPCamera|MOUNT|step=arms-camera|result=refused|parent=%s|offset=%s|rot=%s"),
                GASInitTimelineNow(this), *GetName(),
                *GetNameSafe(Arms->GetAttachParent()), *ArmsOffset.ToString(), *ArmsRotation.ToString());
        }
    }
    else
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("VigilTimeline|t=%.3f|%s|FPCamera|MOUNT|step=arms-camera|result=skipped|reason=no-arms-component"),
            GASInitTimelineNow(this), *GetName());
    }

    // Authoring guard — NOT a clamp. A non-positive ArmsOffset.X seats the arms AT or BEHIND
    // the camera's near plane, where a camera child renders NOTHING: the exact defect that
    // shipped when a stale BP CDO override of (-20,0,-165) parked the arms 20uu behind the eye
    // (it predates every rebuild). Silently sanitizing X here would HIDE that authoring mistake,
    // so this only names the hazard in the log; the C++ default is a sane forward seat and a BP
    // CDO override still wins — the post-merge editor pass owns making the BP value sane.
    if (ArmsOffset.X <= 0.f)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("VigilTimeline|t=%.3f|%s|FPArms|OFFSCREEN|ArmsOffset.X=%.1f|hazard=at-or-behind-near-plane|fix=set-BP-ArmsOffset-X-positive"),
            GASInitTimelineNow(this), *GetName(), ArmsOffset.X);
    }

    // Step 3 — push the first-person render FOV. Re-assert bEnableFirstPersonFieldOfView in
    // case a BP serialized it off.
    FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
    FirstPersonCamera->FirstPersonFieldOfView = FirstPersonFOV;
}

// DEAD in the classic assembly. The camera is parented to the CAPSULE at the historical eye
// seat by EnforceFirstPersonCameraMount (PostInitializeComponents), so this legacy TP-body-bone
// anchor is NO LONGER CALLED (the AnchorCameraToBone() call was removed from BeginPlay). Kept
// declared, and CameraAttachBoneName defaulted to NAME_None, so the function is a hard no-op
// even if something re-invokes it and so any existing BP CDO reference to CameraAttachBoneName
// still resolves. Do not re-wire this into BeginPlay — it would re-parent the camera off the
// capsule eye seat onto the animated TP spine.
void AGothicPlayerCharacter::AnchorCameraToBone()
{
    if (!FirstPersonCamera || CameraAttachBoneName.IsNone())
    {
        return; // left on the mesh component — the original behaviour
    }

    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp || !MeshComp->DoesSocketExist(CameraAttachBoneName))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicPlayerCharacter: CameraAttachBoneName '%s' is not a bone or socket on "
                 "%s — camera left on the mesh component."),
            *CameraAttachBoneName.ToString(), *GetNameSafe(MeshComp ? MeshComp->GetSkeletalMeshAsset() : nullptr));
        return;
    }

    // KeepWorldTransform, so the eye does not move when the anchor changes. The
    // camera keeps the exact position the Blueprint placed it at and only starts
    // following a different parent — which makes this a clean A/B: anything that
    // looks different afterwards is the anchoring, not a shifted viewpoint.
    FirstPersonCamera->AttachToComponent(
        MeshComp, FAttachmentTransformRules::KeepWorldTransform, CameraAttachBoneName);

    if (!CameraBoneOffsetAdjust.IsNearlyZero())
    {
        FirstPersonCamera->AddLocalOffset(CameraBoneOffsetAdjust);
    }
}

void AGothicPlayerCharacter::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AGothicPlayerCharacter, bIsAiming);
}

void AGothicPlayerCharacter::OnADSPressed()  { SetAiming(true); }
void AGothicPlayerCharacter::OnADSReleased() { SetAiming(false); }

void AGothicPlayerCharacter::SetAiming(bool bNewAiming)
{
    if (bIsAiming == bNewAiming)
    {
        return;
    }

    // Sprinting and aiming are mutually exclusive — holding both would otherwise
    // leave you at sprint speed with a sniper's FOV. Routed through SetSprinting
    // rather than writing the flag, so this end path drops State.Sprinting too;
    // aiming out of a sprint is the most common way the gun comes back.
    if (bNewAiming && bIsSprinting)
    {
        SetSprinting(false);
    }

    bIsAiming = bNewAiming;
    RefreshMovementSpeed();

    // The crosshair has had this hook since it was written, with nothing calling it.
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD()))
        {
            GothicHUD->NotifyAimDownSights(bIsAiming);
        }
    }

    if (!HasAuthority())
    {
        ServerSetAiming(bNewAiming);
    }
}

void AGothicPlayerCharacter::ServerSetAiming_Implementation(bool bNewAiming)
{
    // Authority copy only. Re-running the local half here would fight the owning
    // client's prediction on a listen server.
    bIsAiming = bNewAiming;
    RefreshMovementSpeed();
}

void AGothicPlayerCharacter::RefreshMovementSpeed()
{
    const float Base = bIsSprinting ? SprintSpeed : WalkSpeed;

    // MovementSpeed is an additive cm/s bonus from gear. Recomputed rather than
    // accumulated, so repeated equips cannot stack the bonus onto itself.
    float Bonus = 0.f;
    if (AbilitySystemComponent)
    {
        Bonus = AbilitySystemComponent->GetNumericAttribute(
            UGothicAttributeSet::GetMovementSpeedAttribute());
    }

    // Aiming multiplies the FINAL speed, gear bonus included, so a heavily kitted
    // player still gives up the same proportion for aiming.
    const float AimScale = bIsAiming ? FMath::Clamp(ADSMoveSpeedMultiplier, 0.1f, 1.f) : 1.f;

    GetCharacterMovement()->MaxWalkSpeed = FMath::Max(1.f, (Base + Bonus) * AimScale);
}

void AGothicPlayerCharacter::TriggerSelahMoment()
{
    UWorld* World = GetWorld();

    // Never engage a lock we cannot schedule a release for. The fallback timer IS
    // the guarantee the player unfreezes; without a world to arm it on, engaging
    // the lock would strand the player. Bail before touching any lock state.
    if (!World)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("%s|Selah|LOCK_SKIP|no-world"), *GetName());
        return;
    }

    // A fresh moment starts with no widget yet — OnSelahMoment below creates it and
    // is expected to hand it back via RegisterSelahMomentWidget. Reset the tracking
    // BEFORE arming anything so a second overlapping payout cannot inherit the first
    // moment's stale widget pointer.
    SelahMomentWidget.Reset();
    bSelahMomentWidgetRegistered = false;
    SelahMomentLockStartTime = World->GetTimeSeconds();

    // Arm the release poll FIRST, then engage the lock — so the flag and its release
    // driver are set as a matched pair and the flag can never exist without a
    // pending release. This is a REPEATING poll, not a single-shot fallback: each
    // tick both watches the registered widget for teardown (release the instant the
    // visible beat is gone) and enforces SelahMomentLockSeconds as the hard ceiling.
    // Re-entrant-safe: a second finalize simply re-arms the same handle rather than
    // stacking, and the reset above re-baselines the ceiling.
    World->GetTimerManager().SetTimer(
        SelahMomentLockHandle, this, &AGothicPlayerCharacter::PollSelahMomentLock,
        0.25f, /*bLoop=*/true, /*FirstDelay=*/0.25f);

    // Hold the player still for the reveal. Movement and fire are refused while
    // this is set (see OnMove and UGA_Fire::CanActivateAbility); menus are not.
    bSelahMomentLock = true;

    // Mirror the lock onto the authority ASC so the pack surge decorator can read it
    // on the server. TriggerSelahMoment is driven from AGothicEncounterVolume::
    // FinalizeCollection, which is authority-gated, so this runs server-side; the
    // ASC outlives the pawn, so EndSelahMomentLock and the fresh-pawn cleanup both
    // clear it. (State.Dead idiom: absolute SetLooseGameplayTagCount, ASC null-guarded.)
    if (HasAuthority() && AbilitySystemComponent)
    {
        AbilitySystemComponent->SetLooseGameplayTagCount(GothicTags::State_Selah, 1);
    }

    // Kill any momentum already in flight, or a player who was sprinting when the
    // last enemy died keeps sliding through the whole moment.
    if (UCharacterMovementComponent* Move = GetCharacterMovement())
    {
        Move->StopMovementImmediately();
    }

    UE_LOG(LogVigilCombat, Log,
        TEXT("VigilTimeline|t=%.3f|%s|Selah|LOCK_ENGAGE|ceiling=%.1fs"),
        World->GetTimeSeconds(), *GetName(), FMath::Max(0.1f, SelahMomentLockSeconds));

    // Blueprint handles the visual and audio — and is expected to call
    // RegisterSelahMomentWidget with the name-cycle widget it creates here, so the
    // poll can release the lock the moment that widget is torn down.
    OnSelahMoment();
}

void AGothicPlayerCharacter::RegisterSelahMomentWidget(UUserWidget* Widget)
{
    // Only meaningful while a lock is live; a stray call outside the beat is ignored
    // so it cannot arm a phantom teardown-release against the next moment.
    if (!bSelahMomentLock)
    {
        return;
    }

    SelahMomentWidget = Widget;
    bSelahMomentWidgetRegistered = (Widget != nullptr);

    UE_LOG(LogVigilCombat, Log,
        TEXT("VigilTimeline|t=%.3f|%s|Selah|WIDGET_REGISTER|valid=%d"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetName(),
        bSelahMomentWidgetRegistered ? 1 : 0);
}

void AGothicPlayerCharacter::PollSelahMomentLock()
{
    // A poll that fires with no lock live is a stale timer — clear it and go.
    if (!bSelahMomentLock)
    {
        GetWorldTimerManager().ClearTimer(SelahMomentLockHandle);
        return;
    }

    // Teardown IS a release path: once a widget has been registered for this moment,
    // its going invalid (destroyed on level travel, respawn, an interrupted collect,
    // or a second overlapping payout tearing it down) means the visible justification
    // for the lock is gone — release now rather than waiting out the ceiling. Note we
    // only trust this after a registration; an un-wired Blueprint never registers, so
    // bSelahMomentWidgetRegistered stays false and we fall through to the ceiling.
    if (bSelahMomentWidgetRegistered && !SelahMomentWidget.IsValid())
    {
        UE_LOG(LogVigilCombat, Log,
            TEXT("VigilTimeline|t=%.3f|%s|Selah|LOCK_RELEASE|widget-gone"),
            GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetName());
        EndSelahMomentLock();
        return;
    }

    // Hard ceiling — the last-resort backstop for a moment whose widget was never
    // registered and whose OnSelahMomentComplete never arrived.
    const float Now     = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    const float Ceiling = FMath::Max(0.1f, SelahMomentLockSeconds);
    if (Now - SelahMomentLockStartTime >= Ceiling)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("VigilTimeline|t=%.3f|%s|Selah|LOCK_RELEASE|ceiling elapsed=%.2f"),
            Now, *GetName(), Now - SelahMomentLockStartTime);
        EndSelahMomentLock();
    }
}

void AGothicPlayerCharacter::EndSelahMomentLock()
{
    const bool bWasLocked = bSelahMomentLock;

    bSelahMomentLock = false;

    // Close the server-side window opened in TriggerSelahMoment. Reached via the
    // fallback timer, the widget's OnSelahMomentComplete, the OnMove self-heal, and
    // the fresh-pawn / respawn cleanup — so it must be idempotent and clear ALL of
    // its state unconditionally (flag, ASC tag, timer). The ASC clear is absolute
    // (State.Dead idiom) and null-guarded because the ASC outlives the pawn.
    if (HasAuthority() && AbilitySystemComponent)
    {
        AbilitySystemComponent->SetLooseGameplayTagCount(GothicTags::State_Selah, 0);
    }

    GetWorldTimerManager().ClearTimer(SelahMomentLockHandle);

    // Drop the widget tracking so a torn-down widget from this moment can never fire
    // a phantom teardown-release against the next one.
    SelahMomentWidget.Reset();
    bSelahMomentWidgetRegistered = false;

    // The canonical "lock actually released at t=X" marker (grep: Selah|LOCK_RELEASE).
    // Entry paths that carry a reason — the poll's widget-gone / ceiling lines and
    // OnMove's LOCK_SELFHEAL — log that reason just before landing here; a bare
    // LOCK_RELEASE with no preceding reason line is the normal widget-completion path
    // (Blueprint's OnSelahMomentComplete calling in directly).
    if (bWasLocked)
    {
        UE_LOG(LogVigilCombat, Log,
            TEXT("VigilTimeline|t=%.3f|%s|Selah|LOCK_RELEASE"),
            GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetName());
    }
}

bool AGothicPlayerCharacter::HasRoundChambered() const
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return false;
    }
    // HasAmmo covers the ammo-less case — a Heavy Melee Rig is always ready
    return WeaponSlots[ActiveWeaponIndex].HasAmmo();
}

void AGothicPlayerCharacter::ConsumeRound()
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return;
    }

    FGothicWeaponSlot& Slot = WeaponSlots[ActiveWeaponIndex];

    // Ammo-less weapons have nothing to spend
    if (Slot.WeaponData && !Slot.WeaponData->bUsesAmmo)
    {
        return;
    }

    if (Slot.CurrentMagazine > 0)
    {
        Slot.CurrentMagazine--;


        PushAmmoToHUD();

        // Auto-reload on the shot that empties the magazine, not on the next fire
        // attempt — the player should never have to pull a trigger that does nothing.
        //
        // This sits INSIDE the decrement branch on purpose. It is the only place
        // in the class where a magazine reaches zero by being spent; every other
        // path to CurrentMagazine = 0 (InitFromData, and the slot-clearing branch
        // of OnEquipmentChanged) assigns the field directly and never reaches
        // here, so a weapon swap or an unequip cannot masquerade as an empty gun.
        if (Slot.CurrentMagazine == 0)
        {
            // The magazine emptying is the teachable moment for reload, whether or
            // not this weapon auto-reloads: an auto-reloading gun still leaves the
            // player wondering what just happened, and a manual one leaves them
            // holding a dead trigger. Raised BEFORE TryAutoReload, because that
            // call can re-enter Blueprint and must be the last thing here.
            if (HintManager)
            {
                HintManager->ShowHint(GothicTags::Hint_Reload);
            }

            if (Slot.WeaponData && Slot.WeaponData->bAutoReloadWhenEmpty)
            {
                // Nothing below may touch Slot: the reload fires a Blueprint event
                // that could resize WeaponSlots out from under this reference.
                TryAutoReload();
            }
        }
    }
}

void AGothicPlayerCharacter::TryAutoReload()
{
    // ReloadActiveWeapon fires OnReloadPerformed, a Blueprint event free to do
    // anything at all — including firing again, which lands back in ConsumeRound.
    // The guard makes that a no-op rather than a recursion.
    if (bAutoReloadInProgress)
    {
        return;
    }

    bAutoReloadInProgress = true;

    // Deliberately a single attempt, never a loop. An empty reserve makes
    // ReloadActiveWeapon return false, and that is the end of it — the player is
    // dry and must reload or convert Steadfast themselves. Spending a defensive
    // resource without being asked is a design decision, not part of this.
    const bool bReloaded = ReloadActiveWeapon();

    bAutoReloadInProgress = false;

    if (bReloaded)
    {
        OnAutoReloadPerformed();
    }
}

int32 AGothicPlayerCharacter::GetActiveSteadfastRefillCost() const
{
    const UGothicWeaponData* WeaponData = GetActiveWeaponData();
    const int32 BaseCost = WeaponData ? WeaponData->GetSteadfastRefillCost() : 0;

    // Charitable Toll — "costs 1 fewer charge (minimum 1)". Applied here rather
    // than only in the conversion so the HUD quotes the price the player will
    // actually pay; a weapon that costs nothing (no ammo) still costs nothing.
    if (BaseCost > 0 && HasWeaponPerk(GothicTags::Perk_Weapon_VerbB_CharitableToll))
    {
        return FMath::Max(1, BaseCost - CharitableTollChargeDiscount);
    }

    return BaseCost;
}

void AGothicPlayerCharacter::PushAmmoToHUD() const
{
    if (!IsLocallyControlled())
    {
        return;
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (!GothicHUD || !WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return;
    }

    const FGothicWeaponSlot& Slot = WeaponSlots[ActiveWeaponIndex];
    if (!Slot.WeaponData)
    {
        GothicHUD->UpdateAmmo(0, 0, 0, 0);
        return;
    }

    GothicHUD->UpdateAmmo(
        Slot.CurrentMagazine,
        // Effective for the same reason the reserve max is — Extended Magazine
        // has to move the denominator or the counter reads past its own cap.
        Slot.GetEffectiveMagazineCapacity(),
        Slot.CurrentReserve,
        // Effective, not authored — the HUD's max has to move with Deep Reserves
        // or the bar reads over 100% the moment the perk pays out.
        Slot.GetEffectiveMaxReserve());
}

TArray<FGothicAmmoSlotSnapshot> AGothicPlayerCharacter::CaptureAmmoSnapshot() const
{
    TArray<FGothicAmmoSlotSnapshot> Snapshot;

    for (int32 i = 0; i < WeaponSlots.Num(); ++i)
    {
        const FGothicWeaponSlot& Slot = WeaponSlots[i];

        // Ammo-less weapons hold 0/0 by construction (InitFromData zeroes them),
        // so banking them would only add rows the restore has to skip anyway.
        if (!Slot.WeaponData || !Slot.WeaponData->bUsesAmmo)
        {
            continue;
        }

        FGothicAmmoSlotSnapshot& Entry = Snapshot.AddDefaulted_GetRef();
        Entry.SlotIndex = i;
        Entry.Magazine  = Slot.CurrentMagazine;
        Entry.Reserve   = Slot.CurrentReserve;
    }

    return Snapshot;
}

void AGothicPlayerCharacter::ApplyAmmoSnapshot(const TArray<FGothicAmmoSlotSnapshot>& Snapshot)
{
    for (const FGothicAmmoSlotSnapshot& Entry : Snapshot)
    {
        if (!WeaponSlots.IsValidIndex(Entry.SlotIndex))
        {
            continue;
        }

        FGothicWeaponSlot& Slot = WeaponSlots[Entry.SlotIndex];

        // The slot's CURRENT weapon decides whether ammo is even a concept here
        // and what its ceilings are. A player who banked a perked gun and
        // respawned holding something else must not inherit that gun's numbers.
        if (!Slot.WeaponData || !Slot.WeaponData->bUsesAmmo)
        {
            continue;
        }

        Slot.CurrentMagazine = FMath::Clamp(Entry.Magazine, 0, Slot.GetEffectiveMagazineCapacity());
        Slot.CurrentReserve  = FMath::Clamp(Entry.Reserve,  0, Slot.GetEffectiveMaxReserve());
    }
}

void AGothicPlayerCharacter::Client_RestoreAmmo_Implementation(const TArray<FGothicAmmoSlotSnapshot>& Snapshot)
{
    PendingClientAmmoRestore = Snapshot;
    ApplyAmmoSnapshot(PendingClientAmmoRestore);
    PushAmmoToHUD();
}

// ═══════════════════════════════════════════════════════════════════════════
// Reload — tap to load from reserve, hold to convert Steadfast into reserve
// ═══════════════════════════════════════════════════════════════════════════

bool AGothicPlayerCharacter::ReloadActiveWeapon()
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return false;
    }

    FGothicWeaponSlot& Slot = WeaponSlots[ActiveWeaponIndex];
    if (!Slot.WeaponData || !Slot.WeaponData->bUsesAmmo)
    {
        return false;
    }

    const int32 Space = Slot.GetEffectiveMagazineCapacity() - Slot.CurrentMagazine;
    if (Space <= 0 || Slot.CurrentReserve <= 0)
    {
        return false;
    }

    const int32 Loaded = FMath::Min(Space, Slot.CurrentReserve);
    Slot.CurrentMagazine += Loaded;
    Slot.CurrentReserve  -= Loaded;


    PushAmmoToHUD();
    OnReloadPerformed();
    return true;
}

bool AGothicPlayerCharacter::ConvertSteadfastToReserve()
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return false;
    }

    FGothicWeaponSlot& Slot = WeaponSlots[ActiveWeaponIndex];
    if (!Slot.WeaponData || !Slot.WeaponData->bUsesAmmo)
    {
        return false;
    }

    // Effective ceiling — Deep Reserves buys headroom for Steadfast to fill.
    const int32 ReserveSpace = Slot.GetEffectiveMaxReserve() - Slot.CurrentReserve;
    if (ReserveSpace <= 0)
    {
        return false;
    }

    UGothicSteadfastComponent* Steadfast = FindComponentByClass<UGothicSteadfastComponent>();
    if (!Steadfast)
    {
        UE_LOG(LogTemp, Warning, TEXT("ConvertSteadfastToReserve: no UGothicSteadfastComponent on %s"), *GetName());
        return false;
    }

    // Charges are a design-side unit; Steadfast itself is a float attribute.
    // Convert through the bar so the cost stays correct if MaxSteadfast is retuned.
    // Effective, not authored — Charitable Toll's discount is already folded in
    // here, which is also what the HUD quotes.
    const int32 ChargeCost = GetActiveSteadfastRefillCost();
    if (ChargeCost <= 0)
    {
        return false;
    }

    // A zero ceiling means the ASC isn't initialized yet. Without this guard the
    // per-charge cost computes to zero and every conversion is free.
    const float MaxSteadfast = Steadfast->GetMaxSteadfast();
    if (MaxSteadfast <= 0.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("ConvertSteadfastToReserve: MaxSteadfast is 0 — ASC not ready, refusing a free conversion"));
        return false;
    }

    const float PerCharge = MaxSteadfast / FMath::Max(1, SteadfastChargesPerFullBar);
    float Cost = ChargeCost * PerCharge;

    float Yield = static_cast<float>(Slot.WeaponData->SteadfastRefillAmount);

    // Well-Tended — "restores 50% more reserve ammo". Yield only; the price is
    // untouched, which is the whole shape of the perk.
    if (HasWeaponPerk(GothicTags::Perk_Weapon_VerbB_WellTended))
    {
        Yield *= WellTendedYieldScale;
    }

    // Frugal Hand / Overcharge — "one ammo tier lower at half cost" and "one tier
    // higher at 1.5x cost".
    //
    // A "tier" is read here as one charge-step of the weapon's own price: the
    // yield moves by (Steps +/- 1) / Steps where Steps is the charge cost, so a
    // 3-charge Rig trades a third of its refill in either direction and a
    // 1-charge Sidearm trades all of it. The cost multipliers are the doc's flat
    // numbers.
    //
    // INTERPRETATION FLAGGED, not invented quietly: the doc names an "ammo tier"
    // without defining one, and SteadfastChargesPerFullBar is the only tiering
    // the system actually has. A 1-charge weapon cannot step DOWN a tier without
    // yielding nothing, so Frugal Hand floors its yield at one step — it is then
    // purely a half-price refill on a Sidearm, which is still the perk's promise.
    // The two are mutually exclusive by bucket; the else-if only guards against a
    // hand-authored instance carrying both.
    const int32 YieldSteps = FMath::Max(1, ChargeCost);
    if (HasWeaponPerk(GothicTags::Perk_Weapon_VerbB_FrugalHand))
    {
        Yield *= static_cast<float>(FMath::Max(1, YieldSteps - 1)) / static_cast<float>(YieldSteps);
        Cost  *= FrugalHandCostScale;
    }
    else if (HasWeaponPerk(GothicTags::Perk_Weapon_VerbB_Overcharge))
    {
        Yield *= static_cast<float>(YieldSteps + 1) / static_cast<float>(YieldSteps);
        Cost  *= OverchargeCostScale;
    }

    const int32 Requested = FMath::Min(FMath::FloorToInt(Yield), ReserveSpace);

    // Nothing to grant — bail BEFORE the conversion. TryConvertSteadfast debits
    // the cost and then returns whatever it was handed, so calling it with a zero
    // yield charges the player for nothing. Reachable the moment a perk scales a
    // small refill amount down.
    if (Requested <= 0)
    {
        return false;
    }

    const float Granted = Steadfast->TryConvertSteadfast(Cost, static_cast<float>(Requested));
    if (Granted <= 0.f)
    {
        return false;
    }

    const int32 Rounds = FMath::Min(FMath::FloorToInt(Granted), ReserveSpace);
    Slot.CurrentReserve += Rounds;


    PushAmmoToHUD();
    OnSteadfastConverted(Rounds);
    return true;
}

void AGothicPlayerCharacter::OnReloadPressed()
{
    // Inventory open — the key belongs to the UI
    if (ActiveInventoryWidget)
    {
        return;
    }

    // Sprinting — the gun is down. Dead input, deliberately: no queued reload
    // waiting for the sprint to end, and no auto-unsprint. Reload is a gun action.
    if (AreGunActionsBlocked())
    {
        return;
    }

    bSteadfastConversionFired = false;
    bSteadfastHoldThresholdReached = false;

    // The press-to-release hold IS the reload vulnerability window — reload itself is
    // instantaneous (ReloadActiveWeapon only moves counts), so there is no separate
    // reload duration to gate on. Opened here, past the guards so a blocked press
    // never opens it, and closed on release plus every interrupt path below.
    SetReloadingHold(true);

    GetWorldTimerManager().SetTimer(
        SteadfastHoldTimerHandle,
        this,
        &AGothicPlayerCharacter::HandleSteadfastHoldTick,
        SteadfastHoldThreshold,
        false);
}

void AGothicPlayerCharacter::OnReloadReleased()
{
    // A physical key release always closes the vulnerability window, even the
    // release that lands while sprinting (which returns early below). Cleared ahead
    // of that guard for exactly that case.
    SetReloadingHold(false);

    // Checked on release as well as press, not only on press: a player who starts
    // the sprint DURING the hold would otherwise release into a tap-reload, since
    // the release handler reloads whenever the threshold was not reached. The
    // sprint already cleared the hold state in SetSprinting.
    if (AreGunActionsBlocked())
    {
        return;
    }

    const bool bWasHeld = bSteadfastHoldThresholdReached;

    EndSteadfastHold();
    bSteadfastHoldThresholdReached = false;

    // Released before the threshold — this was a tap, so do a normal reload.
    // Past the threshold the payoff was already given mid-hold (or refused because
    // there was nothing to convert); either way releasing must not reload.
    if (!bWasHeld)
    {
        ReloadActiveWeapon();

        // A manual reload answers the reload hint — cut it short rather than
        // making the player read instructions they have already followed.
        if (HintManager)
        {
            HintManager->NotifyHintActionPerformed(GothicTags::Hint_Reload);
        }
    }
    else if (HintManager)
    {
        // A completed hold answers the Steadfast hint the same way.
        HintManager->NotifyHintActionPerformed(GothicTags::Hint_SteadfastConvert);
    }
}

void AGothicPlayerCharacter::HandleSteadfastHoldTick()
{
    // Conversion is granted here, mid-hold — never deferred to release.
    if (!ConvertSteadfastToReserve())
    {
        // Nothing left to convert, or nowhere to put it. Stop rather than spin.
        //
        // Deliberately leave bSteadfastHoldThresholdReached false: the flag's only
        // job is to tell the release handler "the payoff was already given, don't
        // also tap-reload". No conversion happened, so the release must still
        // reload. Setting it unconditionally here made every press longer than
        // SteadfastHoldThreshold a no-op — and because StartingReserveAmmo equals
        // MaxReserveAmmo on every weapon, reserve begins full and this branch is
        // exactly what a fresh player hits.
        EndSteadfastHold();
        return;
    }

    bSteadfastHoldThresholdReached = true;
    bSteadfastConversionFired = true;

    // Still held — queue the next conversion.
    GetWorldTimerManager().SetTimer(
        SteadfastHoldTimerHandle,
        this,
        &AGothicPlayerCharacter::HandleSteadfastHoldTick,
        SteadfastHoldRepeatInterval,
        false);
}

void AGothicPlayerCharacter::EndSteadfastHold()
{
    GetWorldTimerManager().ClearTimer(SteadfastHoldTimerHandle);

    if (bSteadfastConversionFired)
    {
        bSteadfastConversionFired = false;
        OnSteadfastConversionEnded();
    }
}

// The reload vulnerability window (State.Reloading) has to sit on the SERVER ASC —
// the pack surge decorator reads it on the authority — but reload input is
// owning-client-only (bound in SetupPlayerInputComponent). So the owning client
// hands the state to the server over an RPC, the same client-authoritative route
// ammo already takes. On a listen-server host HasAuthority() is already true and
// the tag is applied without a round trip. No local copy is kept: the tag has no
// client-side consumer (unlike State.Sprinting, whose GA_Fire gate is client-side).
void AGothicPlayerCharacter::SetReloadingHold(bool bActive)
{
    if (HasAuthority())
    {
        ApplyReloadingTag(bActive);
    }
    else
    {
        ServerSetReloadingHold(bActive);
    }
}

void AGothicPlayerCharacter::ServerSetReloadingHold_Implementation(bool bActive)
{
    ApplyReloadingTag(bActive);
}

void AGothicPlayerCharacter::ApplyReloadingTag(bool bActive)
{
    // SetLooseGameplayTagCount to an absolute 1/0 rather than Add/Remove: the ASC
    // lives on the PlayerState and outlives the pawn, so an absolute write can never
    // accumulate a stuck count across repeated presses or a death (same idiom as the
    // State.Dead clear in InitGASFromPlayerState).
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->SetLooseGameplayTagCount(
            GothicTags::State_Reloading, bActive ? 1 : 0);
    }
}

const UGothicWeaponData* AGothicPlayerCharacter::GetActiveWeaponData() const
{
    if (WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return WeaponSlots[ActiveWeaponIndex].WeaponData;
    }
    return nullptr;
}

bool AGothicPlayerCharacter::ResolveMuzzleLocation(
    bool bPreferFirstPerson, FVector& OutLocation, const TCHAR*& OutSource) const
{
    // Authoritative component first, mirror second. Locally-viewed pawn: the owner FP
    // gun is the one the reticle lines up against, so it wins. Server copy of a remote
    // pawn: the FP mesh is hidden (and points nowhere meaningful), so the third-person
    // mirror — the gun that pawn actually presents to the world — is authoritative.
    UStaticMeshComponent* Primary   = bPreferFirstPerson ? FPWeaponMesh : ThirdPersonWeaponMesh;
    UStaticMeshComponent* Secondary = bPreferFirstPerson ? ThirdPersonWeaponMesh : FPWeaponMesh;

    // A weapon component with an assigned mesh (an empty slot clears FPWeaponMesh's mesh
    // to null) is required for either a socket lookup or the location fallback — a bare
    // component with no mesh has neither a valid "Muzzle" socket nor a meaningful origin.
    auto SocketLabel = [this](const UStaticMeshComponent* Comp) -> const TCHAR*
    {
        return (Comp == FPWeaponMesh) ? TEXT("fp-socket") : TEXT("tp-socket");
    };

    // Links 1 & 2 — named socket on the authoritative mesh, then the mirror. Hidden
    // static-mesh components still resolve socket transforms, so this is valid on the
    // server-for-remote path. Guard on DoesSocketExist because GetSocketLocation silently
    // returns the component origin for a missing socket, which would masquerade as a hit.
    if (Primary && Primary->GetStaticMesh() && Primary->DoesSocketExist(MuzzleSocketName))
    {
        OutLocation = Primary->GetSocketLocation(MuzzleSocketName);
        OutSource   = SocketLabel(Primary);
        return true;
    }
    if (Secondary && Secondary->GetStaticMesh() && Secondary->DoesSocketExist(MuzzleSocketName))
    {
        OutLocation = Secondary->GetSocketLocation(MuzzleSocketName);
        OutSource   = SocketLabel(Secondary);
        return true;
    }

    // Link 3 — no muzzle socket authored on either mesh: synthesize a muzzle a fixed
    // distance forward of whichever weapon component carries a mesh.
    UStaticMeshComponent* WeaponComp =
        (Primary && Primary->GetStaticMesh())       ? Primary   :
        (Secondary && Secondary->GetStaticMesh())   ? Secondary : nullptr;
    if (WeaponComp)
    {
        OutLocation = WeaponComp->GetComponentLocation()
                    + WeaponComp->GetForwardVector() * MuzzleForwardOffset;
        OutSource   = TEXT("component-offset");
        return true;
    }

    // No weapon mesh at all — caller falls back to the camera origin.
    OutSource = TEXT("camera-fallback");
    return false;
}

int32 AGothicPlayerCharacter::GetActiveGearPower() const
{
    if (WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return WeaponSlots[ActiveWeaponIndex].GearPower;
    }
    return 0;
}

void AGothicPlayerCharacter::ApplyLocalHitStop()
{
    // Shooter-LOCAL only. The confirmed-hit multicast reaches every client; this
    // must fire the hitch only on the pawn whose shot it was. A remote proxy has no
    // business dilating for someone else's hit, and a dedicated-server copy has no
    // view to feel it.
    if (!IsLocallyControlled())
    {
        return;
    }

    // "Heavy" is a weapon-data property, never a hardcoded name — a light sidearm
    // opts out simply by leaving bHeavyWeapon false.
    const UGothicWeaponData* WeaponData = GetActiveWeaponData();
    if (!WeaponData || !WeaponData->bHeavyWeapon || WeaponData->HitStopDuration <= 0.f)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // The multiplayer-safe part: CustomTimeDilation is PER-ACTOR. It slows only this
    // pawn's tick (and, being camera-parented, its own view), so only the shooter
    // feels the micro-pause. Global Set-Global-Time-Dilation would lag the whole
    // server/co-op session and is deliberately NOT used here.
    CustomTimeDilation = FMath::Clamp(WeaponData->HitStopTimeDilation, 0.01f, 1.f);

    // The timer manager ticks on the WORLD clock, which per-actor CustomTimeDilation
    // does not scale, so the restore lands after HitStopDuration of real time no
    // matter how hard this pawn is dilated. Non-looping; a second hit inside the
    // window just re-arms it. Bound to this pawn, so it auto-clears on destruction.
    World->GetTimerManager().SetTimer(
        HitStopTimerHandle, this, &AGothicPlayerCharacter::EndLocalHitStop,
        WeaponData->HitStopDuration, /*bLoop=*/ false);

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|HitStop|dilation=%.2f|dur=%.3f"),
        World->GetTimeSeconds(), *GetName(),
        CustomTimeDilation, WeaponData->HitStopDuration);
}

void AGothicPlayerCharacter::EndLocalHitStop()
{
    CustomTimeDilation = 1.f;
}

float AGothicPlayerCharacter::GetActiveWeaponTierMultiplier() const
{
    // WEAPON_ARCHETYPES.md, DECIDED 2026-08-04: a weapon scales by its OWN
    // instance's Gear Power, never by what the wearer is wearing. Since
    // GetGearPower() is GearTier * 100 with no per-drop variance, the multiplier
    // IS the tier -- x1 at Tier 1, x5 at Tier 5.
    const int32 SlotGearPower = GetActiveGearPower();

    // Zero means the slot was never filled through the inventory -- the
    // Blueprint default loadout writes WeaponData directly and leaves GearPower
    // at 0, which is the state the starting Revolver is in on every fresh
    // character. Treated as the Tier-1 baseline, the same floor the doc gives
    // Salvage weapons, so the starting kit fires at its authored book value
    // instead of being multiplied to nothing.
    if (SlotGearPower <= 0)
    {
        return 1.f;
    }

    return static_cast<float>(SlotGearPower) / WeaponTierBaselineGearPower;
}

const TArray<FGameplayTag>& AGothicPlayerCharacter::GetActiveWeaponPerks() const
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        static const TArray<FGameplayTag> Empty;
        return Empty;
    }

    return WeaponSlots[ActiveWeaponIndex].Perks;
}

bool AGothicPlayerCharacter::HasWeaponPerk(FGameplayTag Perk) const
{
    return GetActiveWeaponPerks().Contains(Perk);
}

bool AGothicPlayerCharacter::IsMovingUnderOwnPower() const
{
    const UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (!Movement)
    {
        return false;
    }

    // Horizontal only. A falling character is moving fast in Z and is not doing
    // the thing Moving Target rewards, and a lift ride should not read as strafing.
    return Movement->Velocity.Size2D() >= PerkMovementSpeedThreshold;
}

float AGothicPlayerCharacter::GetStationaryDuration() const
{
    const UWorld* World = GetWorld();
    if (!World || IsMovingUnderOwnPower())
    {
        return 0.f;
    }

    // Never moved since possession — treat the whole session as still rather
    // than measuring back to world time zero, which would credit a pawn that
    // spawned mid-match with a stillness it did not earn.
    if (LastMovingWorldTime < 0.f)
    {
        return World->GetTimeSeconds();
    }

    return World->GetTimeSeconds() - LastMovingWorldTime;
}

int32 AGothicPlayerCharacter::AddRoundsToMagazine(int32 Rounds)
{
    if (Rounds <= 0 || !WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return 0;
    }

    FGothicWeaponSlot& Slot = WeaponSlots[ActiveWeaponIndex];
    if (!Slot.WeaponData || !Slot.WeaponData->bUsesAmmo)
    {
        return 0;
    }

    // Straight into the magazine, deliberately NOT out of the reserve — Marksman's
    // Due creates the round, it does not move one. Clamped to the effective cap so
    // it composes with Extended Magazine rather than fighting it.
    const int32 Space = Slot.GetEffectiveMagazineCapacity() - Slot.CurrentMagazine;
    const int32 Loaded = FMath::Clamp(Rounds, 0, FMath::Max(0, Space));
    if (Loaded <= 0)
    {
        return 0;
    }

    Slot.CurrentMagazine += Loaded;
    PushAmmoToHUD();
    return Loaded;
}

int32 AGothicPlayerCharacter::GetAggregateGearPower() const
{
    const AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    const UGothicInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
    return Inventory ? Inventory->GetAggregateGearPower() : 0;
}

float AGothicPlayerCharacter::GetArchetypeDamageBonusPct(EGothicWeaponArchetype Archetype) const
{
    const AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    const UGothicInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
    return Inventory ? Inventory->GetArchetypeDamageBonus(GetArchetypeDamageStat(Archetype)) : 0.f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Dev measurement bench (L_DEV_FeelBox) — see the header block on DevBenchMaps
// ═══════════════════════════════════════════════════════════════════════════

bool AGothicPlayerCharacter::IsDevBenchLevel() const
{
    // Reflected (BlueprintPure), so the symbol stays in every config — UHT will
    // not honour an #if around the declaration — but the bench itself is dev-only:
    // in Shipping this always answers false, which routes every caller (the
    // canonical loadout, any stray Blueprint) onto the plain non-bench path.
#if !UE_BUILD_SHIPPING
    // GetCurrentLevelName strips the PIE "UEDPIE_0_" prefix, so the compare holds
    // in PIE, standalone and cooked — identical to the hint zone gate. Empty list
    // (or a mismatch) means NOT a bench, which is what keeps every shipping map
    // and its rolled loadout on the untouched path.
    const FString CurrentMap = UGameplayStatics::GetCurrentLevelName(this);
    return DevBenchMaps.ContainsByPredicate(
        [&CurrentMap](const FString& Allowed) { return Allowed.Equals(CurrentMap, ESearchCase::IgnoreCase); });
#else
    return false;
#endif
}

#if !UE_BUILD_SHIPPING
void AGothicPlayerCharacter::DumpBenchLoadout() const
{
    // Read-only measurement dump. The console command already proved the bench
    // gate before calling, but re-state the map on the header line so a captured
    // log is self-describing.
    const FString CurrentMap = UGameplayStatics::GetCurrentLevelName(this);

    const AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    const UGothicInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
    if (!Inventory)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Bench|DumpLoadout|NO-INVENTORY|pawn=%s|map=%s — PlayerState/inventory not resolved yet."),
            *GetName(), *CurrentMap);
        return;
    }

    // The active weapon and the two scalars GA_Fire multiplies onto its base
    // damage. ArchetypePct is the exact base→pre-vital drift the bench pins:
    // pre-vital = base x weaponTier x (1 + ArchetypePct/100).
    const UGothicWeaponData* ActiveWeapon =
        WeaponSlots.IsValidIndex(ActiveWeaponIndex) ? WeaponSlots[ActiveWeaponIndex].WeaponData : nullptr;
    const float WeaponTierMult = GetActiveWeaponTierMultiplier();
    const float ArchetypePct = ActiveWeapon
        ? GetArchetypeDamageBonusPct(ActiveWeapon->Archetype) : 0.f;

    UE_LOG(LogVigilCombat, Log,
        TEXT("Bench|DumpLoadout|pawn=%s|map=%s|activeWeapon=%s|weaponTierMult=x%.3f|archetypePct=%.3f|gearScore=%d"),
        *GetName(), *CurrentMap,
        ActiveWeapon ? *ActiveWeapon->WeaponName.ToString() : TEXT("none"),
        WeaponTierMult, ArchetypePct, Inventory->GetGearScore());

    // One line per equipped slot: identity plus the primary and every secondary
    // roll, so a diff of two sessions' dumps is exactly the loadout comparison the
    // bench exists to make trivial. Enum values logged numerically — this is a
    // developer dump, not player-facing copy.
    for (const FGothicEquippedSlot& Entry : Inventory->GetEquippedSlots())
    {
        const FGothicItemInstance& Item = Entry.Item;
        FString Secondaries;
        for (const FGothicStatRoll& Roll : Item.SecondaryStats)
        {
            Secondaries += FString::Printf(TEXT("[stat=%d val=%.3f]"),
                static_cast<int32>(Roll.StatType), Roll.Value);
        }

        UE_LOG(LogVigilCombat, Log,
            TEXT("Bench|DumpLoadout|slot=%d|item=%s|tier=%d|primaryStat=%d|primaryVal=%.3f|secondaries=%s"),
            static_cast<int32>(Entry.Slot),
            Item.Definition ? *Item.Definition->ItemID.ToString() : TEXT("null"),
            Item.Definition ? Item.Definition->GearTier : -1,
            Item.Definition ? static_cast<int32>(Item.Definition->PrimaryStatType) : -1,
            Item.PrimaryStatValue,
            Secondaries.IsEmpty() ? TEXT("(none)") : *Secondaries);
    }
}

bool AGothicPlayerCharacter::GrantBenchItem(const FString& ItemDefName)
{
    AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    UGothicInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
    if (!Inventory)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Bench|Grant|NO-INVENTORY|item=%s — PlayerState/inventory not resolved."),
            *ItemDefName);
        return false;
    }

    // Real named definitions live under /Game/Data/Loot as DA_ItemDef_<Name>. Load
    // by full object path (Package.Object) so LoadObject resolves the asset itself
    // rather than the package. A miss is the common operator error — a mistyped
    // name — so it logs the resolved path to make the fix obvious, and returns
    // rather than asserting.
    const FString ObjectPath = FString::Printf(
        TEXT("/Game/Data/Loot/DA_ItemDef_%s.DA_ItemDef_%s"), *ItemDefName, *ItemDefName);
    UGothicItemDefinition* Def = LoadObject<UGothicItemDefinition>(nullptr, *ObjectPath);
    if (!Def)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Bench|Grant|NOT-FOUND|item=%s|path=%s — check the name (Gothic.Bench.ListItems)."),
            *ItemDefName, *ObjectPath);
        return false;
    }

    // Canonical roll so the granted item is byte-identical every session, exactly
    // like the pinned starting kit. AddItem/EquipItem are authority-only and log
    // their own refusal on a client — the bench is single-player, so the local
    // pawn is authority.
    FGothicItemInstance Instance = Def->RollInstance(/*bCanonical=*/true);
    if (!Inventory->AddItem(Instance))
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Bench|Grant|ADD-FAILED|item=%s — inventory full or client."), *ItemDefName);
        return false;
    }

    // Equip straight into the definition's own slot. This fires OnItemEquipped ->
    // OnEquipmentChanged, which is what actually arms the weapon slot / applies the
    // armor stats — the same path a hand-equip takes.
    Inventory->EquipItem(Instance.InstanceID);

    // For a weapon, put it in the active hand so the press ends "ready to fire".
    // Armor returns index -1 here and simply stays equipped.
    bool bSwappedToHand = false;
    if (Def->IsWeapon())
    {
        const int32 WeaponIndex = EquipSlotToWeaponIndex(Def->EquipSlot);
        if (WeaponSlots.IsValidIndex(WeaponIndex))
        {
            SwapWeapon(WeaponIndex);
            bSwappedToHand = true;
        }
    }

    UE_LOG(LogVigilCombat, Log,
        TEXT("Bench|Grant|OK|item=%s|slot=%d|weapon=%d|inHand=%d|gearScore=%d"),
        *ItemDefName, static_cast<int32>(Def->EquipSlot),
        Def->IsWeapon() ? 1 : 0, bSwappedToHand ? 1 : 0, Inventory->GetGearScore());
    return true;
}

void AGothicPlayerCharacter::BenchLookAt(const FVector& WorldPoint)
{
    AController* Ctrl = GetController();
    if (!Ctrl)
    {
        UE_LOG(LogVigilCombat, Warning, TEXT("Bench|LookAt|NO-CONTROLLER|pawn=%s"), *GetName());
        return;
    }

    // From the CAMERA's world location, deliberately — the camera sits ~170uu up
    // the capsule, and building the aim from the actor origin instead is the exact
    // pitch error this command exists to eliminate. Fall back to the eye viewpoint
    // only if the component is somehow absent, never to the actor location.
    FVector EyeLoc;
    if (FirstPersonCamera)
    {
        EyeLoc = FirstPersonCamera->GetComponentLocation();
    }
    else
    {
        FRotator ViewRot;
        GetActorEyesViewPoint(EyeLoc, ViewRot);
    }

    const FRotator LookRot = (WorldPoint - EyeLoc).Rotation();
    Ctrl->SetControlRotation(LookRot);

    UE_LOG(LogVigilCombat, Log,
        TEXT("Bench|LookAt|pawn=%s|from=%s|to=%s|pitch=%.2f|yaw=%.2f"),
        *GetName(), *EyeLoc.ToCompactString(), *WorldPoint.ToCompactString(),
        LookRot.Pitch, LookRot.Yaw);
}

void AGothicPlayerCharacter::BenchSetControlRotation(float Pitch, float Yaw)
{
    AController* Ctrl = GetController();
    if (!Ctrl)
    {
        UE_LOG(LogVigilCombat, Warning, TEXT("Bench|SetRot|NO-CONTROLLER|pawn=%s"), *GetName());
        return;
    }

    const FRotator Rot(Pitch, Yaw, 0.f);
    Ctrl->SetControlRotation(Rot);

    UE_LOG(LogVigilCombat, Log,
        TEXT("Bench|SetRot|pawn=%s|pitch=%.2f|yaw=%.2f"), *GetName(), Rot.Pitch, Rot.Yaw);
}
#endif // !UE_BUILD_SHIPPING

const UGA_TheLovedAndTheLost* AGothicPlayerCharacter::FindLovedAndLost() const
{
    if (!AbilitySystemComponent)
    {
        return nullptr;
    }

    // The passive is granted once and runs InstancedPerActor, so its single
    // primary instance holds the live ramp state.
    for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
    {
        if (const UGA_TheLovedAndTheLost* Ramp =
                Cast<UGA_TheLovedAndTheLost>(Spec.GetPrimaryInstance()))
        {
            return Ramp;
        }
    }
    return nullptr;
}

float AGothicPlayerCharacter::GetLovedAndLostRamp() const
{
    const UGA_TheLovedAndTheLost* Ramp = FindLovedAndLost();
    return Ramp ? Ramp->GetRampAlpha() : 0.f;
}

bool AGothicPlayerCharacter::IsLovedAndLostActive() const
{
    const UGA_TheLovedAndTheLost* Ramp = FindLovedAndLost();
    return Ramp && Ramp->IsRampActive();
}

bool AGothicPlayerCharacter::IsReadActive() const
{
    return AbilitySystemComponent &&
        AbilitySystemComponent->HasMatchingGameplayTag(GothicTags::State_Read);
}

void AGothicPlayerCharacter::SetReadMark(UAbilitySystemComponent* NewTargetASC,
                                         const FActiveGameplayEffectHandle& NewMarkHandle)
{
    // Single prey: lift the mark off whoever we last read before recording the new
    // one. Skipped when it is the SAME enemy (a re-read refreshes the mark rather
    // than clearing it), and authority-gated because GE_ReadMark lives on the
    // target's authority — a client here would only try to remove an effect it does
    // not own. The previous enemy may already be dead: RemoveActiveGameplayEffect on
    // a stale handle is a safe no-op, and the weak ASC reads back null once it is GC'd.
    if (HasAuthority())
    {
        if (UAbilitySystemComponent* PrevASC = ReadMarkedTargetASC.Get())
        {
            if (PrevASC != NewTargetASC && ReadMarkHandle.IsValid())
            {
                PrevASC->RemoveActiveGameplayEffect(ReadMarkHandle);

                UE_LOG(LogVigilCombat, Verbose,
                    TEXT("VigilTimeline|t=%.3f|%s|ReadMark|singlePrey lifted prior mark from %s"),
                    GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
                    *GetNameSafe(this), *GetNameSafe(PrevASC->GetAvatarActor()));
            }
        }
    }

    ReadMarkedTargetASC = NewTargetASC;
    ReadMarkHandle      = NewMarkHandle;
}

bool AGothicPlayerCharacter::IsReckoningActive() const
{
    return AbilitySystemComponent &&
        AbilitySystemComponent->HasMatchingGameplayTag(
            GothicTags::State_Reckoning);
}

bool AGothicPlayerCharacter::IsNotAtAllGranted() const
{
    if (!AbilitySystemComponent)
    {
        return false;
    }

    // Check the granted spec's ability class rather than a live instance — the
    // passive counts as "granted" the moment it's on the ASC, whether or not its
    // instance has spun up yet.
    for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
    {
        if (Spec.Ability && Spec.Ability->IsA<UGA_NotAtAll>())
        {
            return true;
        }
    }
    return false;
}

void AGothicPlayerCharacter::OnDeath_Implementation(AActor* Killer)
{
    // Re-entry guard ahead of Super, which early-outs on State.Dead but returns
    // void, so the rest of this function would still run on a second call. Two
    // damage instances landing in the same frame is enough to trigger it.
    // AGothicEnemyBase guards its own override the same way and for the same
    // reason.
    if (AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(
            GothicTags::State_Dead))
    {
        return;
    }

    // Death is a sprint end path like any other, and the most dangerous one to
    // miss: the ASC lives on the PlayerState and outlives the pawn, so a
    // State.Sprinting left set here would ride into the respawn on a pawn whose
    // bIsSprinting is false — and the player would come back unable to shoot,
    // with no input that could clear it.
    SetSprinting(false);

    // The Read's caster window (State.Read) is a duration GE on this same
    // PlayerState ASC, so like State.Sprinting it would ride the remainder of its
    // duration into the next life and leave the HUD proc icon falsely lit. It
    // grants no damage — the payoff is on the target's State.Read.Marked — so this
    // is cosmetic, but it is the same outlives-the-pawn hazard and gets the same
    // clear. RemoveActiveEffectsWithGrantedTags, not a loose-tag clear: State.Read
    // is GRANTED by GE_ReadState, and removing the effect is what drops the tag.
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->RemoveActiveEffectsWithGrantedTags(
            FGameplayTagContainer(GothicTags::State_Read));
    }

    // Death is a reload-window end path like it is a sprint end path: a player who
    // died mid-hold would otherwise carry State.Reloading on the PlayerState ASC
    // into the respawn, where no reload input could ever clear it. On the authority
    // already, so write the tag directly.
    ApplyReloadingTag(false);

    // ── THE DOWNED FORK ──────────────────────────────────────────────────────
    // Everything below this block is irreversible for a player we mean to keep:
    // Super applies State.Dead (in the ActivationBlockedTags of every ability),
    // cancels everything, drops the capsule's collision and disables movement,
    // and the tail of this function asks the game mode to destroy the pawn and
    // respawn a new one. So the fork goes HERE, ahead of all of it.
    //
    // Three gates, and each one is a case where downing would be wrong:
    //   - authority: the state is server-owned; a client reaching this predicts
    //     nothing useful and would only desync the flag.
    //   - a player controller: an AI-possessed player pawn (test dummy, future
    //     companion) has nobody to revive it and no revive UI, same reasoning as
    //     the RequestRespawn gate below.
    //   - the expiring window: OnReviveWindowExpired calls straight back into
    //     this function, and without the latch it would find the party still
    //     alive and put the player down for another 30 seconds, forever.
    if (HasAuthority() && !bReviveWindowExpiring && Cast<APlayerController>(GetController()))
    {
        // Already down and taking damage anyway — environmental, an AoE that
        // does not care about targeting, a shot already in flight when they went
        // down. Re-pin the health and leave, WITHOUT re-arming the timer: a
        // downed player who keeps getting clipped must not get an endless window.
        if (IsDowned())
        {
            if (AttributeSet)
            {
                AttributeSet->SetHealth(1.f);
            }
            return;
        }

        if (HasLivingPartyMember())
        {
            EnterDownedState(Killer);
            return;
        }
    }

    // Tag as dead, cancel abilities, drop collision, stop moving.
    Super::OnDeath_Implementation(Killer);

    if (!HasAuthority())
    {
        return;
    }

    // Reckoning progress is the one thing death does not take. Bank it now,
    // because the next pawn's GAS init re-applies GE_InitStats_Player over the
    // top of the attribute set — which survives on the PlayerState and would
    // otherwise come back overwritten.
    if (AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>())
    {
        if (AttributeSet)
        {
            PS->CacheSuperMeterOnDeath(AttributeSet->GetSuperMeter());
        }

        // Ammo is the same story one layer out: WeaponSlots die with the pawn,
        // and the replacement pawn's init runs InitFromData over every slot,
        // which refills the magazine to capacity. Dying was therefore a free
        // reload — the one thing a death should never hand back.
        //
        // Deliberately HERE and not in the downed fork above. Everything before
        // this point is reversible: a downed player who gets picked up keeps the
        // pawn they were already holding, so their ammo is never captured and
        // never rewritten. A downed player whose window expires comes back
        // through this same function with bReviveWindowExpiring set, falls past
        // the fork, and banks at the moment their death actually becomes real.
        PS->CacheAmmo(CaptureAmmoSnapshot());
    }

    // This is the line the whole death path was missing. Everything above leaves
    // the player tagged dead, uncollidable and unable to move — and then the base
    // class returned, with nothing anywhere calling the game mode. That is the
    // soft-lock: a dead player who is never respawned and can never act.
    //
    // Player-controlled only. An AI-possessed player pawn (a test dummy, a future
    // companion) has no respawn to ask for, and the game mode's path assumes a
    // PlayerStart and a checkpoint that belong to a person.
    if (Cast<APlayerController>(GetController()))
    {
        if (AGothicGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AGothicGameMode>() : nullptr)
        {
            GM->RequestRespawn(GetController());
        }
    }

    // The wipe trigger for the commonest case by far: the LAST player still up
    // dies, so everyone left on the floor loses the only person who could have
    // reached them. Deliberately last — the respawn above is this player's own,
    // and the census below is about everybody else's.
    NotifyPartyStateChanged();
}

// ---------------------------------------------------------------------------
// Downed fork
// ---------------------------------------------------------------------------

bool AGothicPlayerCharacter::HasLivingPartyMember() const
{
    // The whole body of this function moved to AGothicGameState in PR-5 — see the
    // header. It is the SAME walk the party-wipe census runs, which is the point:
    // "somebody is left to pick me up" and "the party has not wiped" are now
    // provably the same predicate rather than two implementations of it.
    const UWorld* World = GetWorld();
    const AGothicGameState* GS = World ? World->GetGameState<AGothicGameState>() : nullptr;
    if (!GS)
    {
        return false;
    }

    return GS->HasFightablePartyMember(GetPlayerState());
}

void AGothicPlayerCharacter::NotifyPartyStateChanged()
{
    if (!HasAuthority())
    {
        return;
    }

    if (AGothicGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AGothicGameMode>() : nullptr)
    {
        GM->EvaluatePartyState();
    }
}

void AGothicPlayerCharacter::CollapseReviveWindow()
{
    if (!HasAuthority() || !IsDowned())
    {
        return;
    }

    // Clear the timer before running its callback by hand: OnReviveWindowExpired
    // destroys this pawn (through OnDeath → RequestRespawn), and a timer left
    // armed against it would be firing into the corpse.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ReviveWindowTimer);
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("VigilTimeline|t=%.3f|%s|Downed|COLLAPSED|party wipe took the window"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetNameSafe(this));

    OnReviveWindowExpired();
}

void AGothicPlayerCharacter::EnterDownedState(AActor* Killer)
{
    AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    if (!PS)
    {
        return;
    }

    // Clear the latch from any PREVIOUS life's expiry before anything else — a
    // player who bled out last life and respawned into this one must be able to
    // go down again.
    bReviveWindowExpiring = false;

    // If WE were mid-revive on somebody else, that ends here. TickReviveChannel
    // would catch it within one interval anyway, but a player going down should not
    // spend a tenth of a second still visibly reviving from the floor.
    CancelReviveChannel(TEXT("reviver-went-down"));

    DownedKiller = Killer;

    // Alive, but only just. IsAlive() is health > 0, and the whole downed design
    // rests on a downed player passing it — that is what keeps the damage
    // pipeline, the HUD and every IsAlive caller in the project working without
    // learning a third state. Not-in-the-fight is carried by IsDowned() instead.
    if (AttributeSet)
    {
        AttributeSet->SetHealth(1.f);
    }

    // The replicated primitive. This is what the enemy AI reads (through
    // IsFightableActor), what the reviving player's client reads, and what mirrors
    // State.Downed onto the ASC.
    PS->SetDowned(true);

    // Out of the fight means out of the fight: anything mid-cast ends here. Note
    // this also ends the passives, which is precisely the debt ReviveFromDowned
    // has to pay back — see there.
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->CancelAllAbilities();
    }

    // Movement off, momentum killed. Capsule COLLISION IS DELIBERATELY LEFT ON,
    // unlike the death path: a downed body has to stay a physical, traceable
    // thing for PR-3's revive to aim at, and it should not sink through the floor
    // while it waits.
    if (UCharacterMovementComponent* Move = GetCharacterMovement())
    {
        Move->StopMovementImmediately();
        Move->DisableMovement();
    }

    // The clock. Expiry is not a second death path — it is the ONLY death path,
    // reached through the same OnDeath_Implementation everything else uses.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            ReviveWindowTimer, this, &AGothicPlayerCharacter::OnReviveWindowExpired,
            FMath::Max(1.f, ReviveWindowSeconds), false);
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("VigilTimeline|t=%.3f|%s|Downed|ENTER|killer=%s|window=%.1fs"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetNameSafe(this),
        *GetNameSafe(Killer), FMath::Max(1.f, ReviveWindowSeconds));

    OnDownedStateChanged(true);

    // Alive → InPeril. This cannot itself produce a wipe — EnterDownedState is
    // only ever reached when HasLivingPartyMember() said somebody is still up —
    // but it is the transition that puts the party readout into peril, and the
    // Blueprint layer hangs off that.
    NotifyPartyStateChanged();
}

void AGothicPlayerCharacter::OnReviveWindowExpired()
{
    if (!HasAuthority())
    {
        return;
    }

    // Somebody revived us between the timer firing and this running, or the state
    // was cleared out from under us by Gothic.SetDowned. Either way there is
    // nothing to bleed out.
    if (!IsDowned())
    {
        return;
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("VigilTimeline|t=%.3f|%s|Downed|EXPIRED|falling through to death"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetNameSafe(this));

    // TAG HYGIENE. The ASC and the PlayerState both outlive this pawn, so a
    // State.Downed left set here rides into the next life on a pawn that is
    // upright and fighting — the exact shape of the State.Dead and
    // State.Sprinting bugs this file already carries fixes for. Cleared BEFORE
    // the death call, not after, because the death call destroys this pawn.
    if (AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>())
    {
        PS->SetDowned(false);
    }
    OnDownedStateChanged(false);

    // Health back off 1 so the death that follows is a real zero-health death
    // rather than a killed-while-alive special case.
    if (AttributeSet)
    {
        AttributeSet->SetHealth(0.f);
    }

    // The latch. OnDeath_Implementation is about to re-ask "is anyone else up?",
    // and the answer is very likely still yes — without this the player goes down
    // again and the window never actually expires.
    bReviveWindowExpiring = true;

    // Through the interface, not OnDeath_Implementation directly: this is the
    // same entry point GothicAttributeSet uses when health crosses zero, so a
    // Blueprint override of OnDeath keeps working on the bleed-out path.
    IGothicCombatInterface::Execute_OnDeath(this, DownedKiller.Get());
}

void AGothicPlayerCharacter::ReviveFromDowned()
{
    if (!HasAuthority() || !IsDowned())
    {
        return;
    }

    AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    if (!PS)
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ReviveWindowTimer);
    }

    DownedKiller.Reset();
    bReviveWindowExpiring = false;

    // Clear the state first: SetDowned(false) drops State.Downed off the ASC, and
    // the ability re-activation below goes through TryActivateAbility, which a
    // blocking tag would refuse — the same ordering InitGASFromPlayerState needs
    // for State.Dead, and for the same reason.
    PS->SetDowned(false);

    // Movement back. NavWalking/Flying are not used by the player, so Walking is
    // the correct restore; the death path never gets here to disagree.
    if (UCharacterMovementComponent* Move = GetCharacterMovement())
    {
        Move->SetMovementMode(MOVE_Walking);
    }

    // A revived player is not a respawned one — GE_InitStats_Player does not run,
    // so this is the only thing that lifts them off the 1 HP they were pinned at.
    // Deliberately a FRACTION rather than a full heal: standing back up at full
    // health would make going down a free reset. PR-3 owns the tuning; the value
    // is here so the state is playable before that ability lands.
    if (AttributeSet && AttributeSet->GetMaxHealth() > 0.f)
    {
        AttributeSet->SetHealth(AttributeSet->GetMaxHealth() * ReviveHealthFraction);
    }

    // ── THE PASSIVES ─────────────────────────────────────────────────────────
    // This is the half an in-place revive gets wrong by default, and it is the
    // first-death passive-loss bug in a new costume.
    //
    // On a RESPAWN the passives come back for free: the pawn is replaced, so its
    // AbilitiesGrantedIntoASC latch starts null, InitGASFromPlayerState re-runs
    // the grant, and UGothicAbilitySet::GiveToAbilitySystem finds each spec
    // already present on the surviving PlayerState ASC and RE-ACTIVATES the ones
    // marked bActivateOnGranted (The Loved and The Lost, Not At All) instead of
    // skipping them.
    //
    // A revive has no new pawn. The latch on THIS pawn still points at THIS ASC,
    // so nothing re-runs, and EnterDownedState's CancelAllAbilities already ended
    // the passive instances — the player would stand back up permanently missing
    // their passives, with the specs sitting right there on the ASC looking fine.
    //
    // So run the same grant loop directly, deliberately bypassing the latch. It
    // is idempotent by construction (existing specs are re-pointed and
    // re-activated, never duplicated), which is exactly why it is safe to call on
    // an ASC that has already been granted into.
    if (AbilitySystemComponent)
    {
        for (const TObjectPtr<UGothicAbilitySet>& AbilitySet : StartupAbilitySets)
        {
            if (AbilitySet)
            {
                AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, this);
            }
        }
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("VigilTimeline|t=%.3f|%s|Downed|REVIVED|health=%.1f"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, *GetNameSafe(this),
        AttributeSet ? AttributeSet->GetHealth() : -1.f);

    OnDownedStateChanged(false);

    // InPeril → Alive, once the last body is off the floor. Covers BOTH revive
    // routes at once, because the channelled revive and Gothic.Revive both end
    // here — there is no second place to remember.
    NotifyPartyStateChanged();
}

void AGothicPlayerCharacter::TriggerFallRespawn()
{
    // Authority only. A fall through the map is NOT a death — routing it through
    // the game mode's RequestRespawn destroyed the pawn and left the controller
    // with no view target for the full 10s respawn delay, which reads as the
    // game freezing. Instead, just teleport the same pawn back onto solid ground
    // instantly (last checkpoint, else a PlayerStart) and kill its velocity. No
    // penalty, no delay, no dependency on the game mode being GothicGameMode.
    if (!HasAuthority())
    {
        return;
    }

    FVector SafeLocation(0.f, 0.f, 500.f);

    if (const AGothicGameState* GS =
            GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr)
    {
        if (!GS->CheckpointLocation.IsZero())
        {
            SafeLocation = GS->CheckpointLocation;
        }
        else if (const AActor* Start =
                     UGameplayStatics::GetActorOfClass(this, APlayerStart::StaticClass()))
        {
            SafeLocation = Start->GetActorLocation();
        }
    }

    SafeLocation.Z += 100.f;  // clear the floor so we don't spawn embedded

    SetActorLocation(SafeLocation, false, nullptr, ETeleportType::TeleportPhysics);
    if (UCharacterMovementComponent* Move = GetCharacterMovement())
    {
        Move->StopMovementImmediately();
    }
}

void AGothicPlayerCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
    // Dropped below the world's KillZ — recover in place rather than the engine
    // default (destroy the pawn, strand the controller).
    if (HasAuthority())
    {
        TriggerFallRespawn();
        return;
    }

    Super::FellOutOfWorld(DmgType);
}

void AGothicPlayerCharacter::ApplyRecoilKick()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC)
    {
        return;
    }

    const UGothicWeaponData* WeaponData = GetActiveWeaponData();
    float Pitch = WeaponData ? WeaponData->RecoilPitch : -0.5f;
    float YawSpread = WeaponData ? WeaponData->RecoilYawSpread : 0.f;

    // Dead Hand — "recoil pitch -30%". Scaling rather than subtracting: RecoilPitch
    // is negative (up), so a subtraction would push the kick harder, and the sign
    // survives a multiply on its own.
    if (HasWeaponPerk(GothicTags::Perk_Weapon_FineTune_DeadHand))
    {
        Pitch *= (1.f - DeadHandRecoilPitchReduction);
    }

    // True Bore — "yaw spread -50%". Narrows the random cone; a weapon authored
    // with no spread at all (Heavy Melee) stays at zero, which is why the perk is
    // curated off it rather than guarded here.
    if (HasWeaponPerk(GothicTags::Perk_Weapon_FineTune_TrueBore))
    {
        YawSpread *= (1.f - TrueBoreYawSpreadReduction);
    }

    PC->AddPitchInput(Pitch);

    // Bank the kick so TickRecoilRecovery can walk it back out. Same units, same
    // call on the way back, so any scaling AddPitchInput applies is symmetric.
    PendingRecoilPitch += Pitch;
    LastRecoilKickTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

    if (YawSpread > 0.f)
    {
        // Yaw spread is deliberately NOT banked. Spread is where the shot went, not
        // a displacement the gun should undo — recovering it would drag the player's
        // aim sideways after every shot.
        PC->AddYawInput(FMath::FRandRange(-YawSpread, YawSpread));
    }
}

void AGothicPlayerCharacter::TickRecoilRecovery(float DeltaTime)
{
    // Recovery speed is a per-weapon feel tunable; the pawn's own RecoilRecoveryRate
    // is the fallback for the no-weapon / melee case. Read fresh each tick so a mid-
    // recovery swap adopts the new gun's settle rate.
    const UGothicWeaponData* WeaponData = GetActiveWeaponData();
    const float RecoveryRate = WeaponData ? WeaponData->RecoilRecoveryRate : RecoilRecoveryRate;

    if (FMath::IsNearlyZero(PendingRecoilPitch, 0.0001f) || RecoveryRate <= 0.f)
    {
        PendingRecoilPitch = 0.f;
        return;
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC)
    {
        PendingRecoilPitch = 0.f;
        return;
    }

    // Hold off until the player stops firing. Every shot restamps the time, so a
    // burst climbs cleanly instead of fighting its own recovery between rounds.
    const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    if (Now - LastRecoilKickTime < RecoilRecoveryDelay)
    {
        return;
    }

    // Exponential rather than linear: the gun drops fast off the top of the kick
    // and eases into the resting position, which is what reads as the weapon
    // settling rather than the camera being driven.
    const float Recovered = PendingRecoilPitch *
        FMath::Clamp(RecoveryRate * DeltaTime, 0.f, 1.f);

    PC->AddPitchInput(-Recovered);
    PendingRecoilPitch -= Recovered;
}

void AGothicPlayerCharacter::SwapWeapon(int32 NewIndex)
{
    if (NewIndex == ActiveWeaponIndex)
    {
        return;
    }

    if (!WeaponSlots.IsValidIndex(NewIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("SwapWeapon: Invalid index %d (have %d slots)"),
            NewIndex, WeaponSlots.Num());
        return;
    }

    // Refuse BEFORE committing the index. The old order set ActiveWeaponIndex,
    // then discovered the slot was empty, then blanked the weapon mesh and
    // returned — leaving the player pointed at an empty slot with nothing in
    // their hands and no way back except pressing another slot key.
    //
    // That was survivable while every slot was filled at spawn. It is not now:
    // the progression starts SIDEARM-ONLY, so slots 2 and 3 are legitimately
    // empty until the Piece is found in Palewood and the Rig is gifted in
    // Hearth, and pressing 2 on the way there disarmed the player mid-fight.
    const UGothicWeaponData* NewWeapon = WeaponSlots[NewIndex].WeaponData;
    if (!NewWeapon)
    {
        // Verbose, not Warning: with an intentionally empty slot this is a player
        // pressing a key that does nothing yet, not a misconfiguration.
        UE_LOG(LogTemp, Verbose, TEXT("SwapWeapon: Slot %d has no WeaponData — keeping slot %d"),
            NewIndex, ActiveWeaponIndex);
        return;
    }

    ActiveWeaponIndex = NewIndex;

    // A completed swap answers the swap hint.
    if (HintManager)
    {
        HintManager->NotifyHintActionPerformed(GothicTags::Hint_WeaponSwap);
    }

    // A swap abandons any in-progress conversion — the cost was tied to the old weapon's tier
    EndSteadfastHold();

    // ...and abandons the reload vulnerability window with it: the reload key may
    // still be held, but the hold that opened the window is over.
    SetReloadingHold(false);

    // ...and abandons the Oversurge streak with it. The streak is a property of
    // sustained fire from one weapon; letting it carry across a swap would mean
    // building it up on the cheap repeater and spending it on a heavy hitter.
    ResetConsecutiveHits();

    // Update mesh and crosshair
    RefreshWeaponVisuals(NewIndex);
    PushAmmoToHUD();


    // NOT WIRED — Quick Hands, "swap-to speed +25%" (WEAPON_PERK_TABLES.md,
    // Fine-Tune bucket). There is nothing here for it to modify: SwapWeapon is
    // instantaneous. The index changes, the mesh and HUD update, and the weapon is
    // fireable on the same frame — the only swap "duration" that exists is
    // whatever animation OnWeaponSwapped plays in Blueprint, which is cosmetic and
    // gates nothing.
    //
    // Deliberately left as a comment rather than given a timing system to speed
    // up. When swap-to time becomes real (an equip montage, or a lockout tag
    // between the index change and the first legal shot), this is its call site:
    //   Duration *= (1.f - QuickHandsSwapSpeedBonus) when
    //   HasWeaponPerk(GothicTags::Perk_Weapon_FineTune_QuickHands).
    // Note the perk must read the NEW slot's perks, i.e. after ActiveWeaponIndex
    // moves, which is already the case at this point.

    // Blueprint hook for swap animation / audio
    OnWeaponSwapped(NewIndex, NewWeapon);
}

// The slot keys are blocked while sprinting — "any gun actions" covers reaching
// for a different gun. Gated HERE and not inside SwapWeapon, so the inventory's
// equip path (OnEquipmentChanged) still swaps whatever the player equips: the
// block is on the combat input, not on the act of changing weapons.
void AGothicPlayerCharacter::OnWeaponSlot1() { if (AreGunActionsBlocked()) { return; } SwapWeapon(0); }
void AGothicPlayerCharacter::OnWeaponSlot2() { if (AreGunActionsBlocked()) { return; } SwapWeapon(1); }
void AGothicPlayerCharacter::OnWeaponSlot3() { if (AreGunActionsBlocked()) { return; } SwapWeapon(2); }

// ═══════════════════════════════════════════════════════════════════════════
// Inventory UI
// ═══════════════════════════════════════════════════════════════════════════

void AGothicPlayerCharacter::ToggleQuitMenu()
{
    if (!IsLocallyControlled())
    {
        return;
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC)
    {
        return;
    }

    // Escape backs out of the topmost screen. With the inventory up, that IS the
    // inventory — opening the quit menu on top of it would leave two screens
    // stacked, both wanting the cursor, and ToggleInventory's input-mode reset
    // would fight the menu's on the way back down.
    if (ActiveInventoryWidget)
    {
        ToggleInventory();
        return;
    }

    if (AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD()))
    {
        GothicHUD->ToggleQuitMenu();
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicPlayerCharacter: no AGothicHUD — quit menu cannot open. Is the "
                 "GameMode's HUDClass set to BP_GothicHUD?"));
    }
}

void AGothicPlayerCharacter::ToggleInventory()
{
    if (!IsLocallyControlled())
    {
        return;
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC)
    {
        return;
    }

    // Opening the inventory answers both the inventory hint and the equip hint —
    // the equip hint's only instruction is "open the inventory and equip it", and
    // a player who is already looking at the screen does not need to be told to.
    if (HintManager)
    {
        HintManager->NotifyHintActionPerformed(GothicTags::Hint_Inventory);
    }

    AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD());

    // Already open — tear it down. NativeDestruct unbinds the inventory delegates.
    if (ActiveInventoryWidget)
    {
        ActiveInventoryWidget->RemoveFromParent();
        ActiveInventoryWidget = nullptr;

        PC->SetInputMode(FInputModeGameOnly());
        PC->SetShowMouseCursor(false);

        if (GothicHUD)
        {
            GothicHUD->SetCrosshairVisible(true);
        }

        return;
    }

    if (!InventoryWidgetClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: InventoryWidgetClass not assigned — set it to WBP_Inventory in BP_GothicPlayerCharacter."));
        return;
    }

    // Inventory lives on the PlayerState alongside the ASC so it survives respawns
    AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    UGothicInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
    if (!Inventory)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: No inventory component on PlayerState — cannot open inventory."));
        return;
    }

    ActiveInventoryWidget = CreateWidget<UGothicInventoryWidget>(PC, InventoryWidgetClass);
    if (!ActiveInventoryWidget)
    {
        UE_LOG(LogTemp, Error, TEXT("GothicPlayerCharacter: CreateWidget failed for %s"), *InventoryWidgetClass->GetName());
        return;
    }

    // Z-order 10 — above the HUD layout (0) and crosshair (1)
    ActiveInventoryWidget->AddToViewport(10);

    // After AddToViewport so the Blueprint's Construct has run before the
    // OnInventoryRefreshed / OnEquipmentRefreshed events fire and populate the grid.
    ActiveInventoryWidget->InitializeFromInventory(Inventory);

    FInputModeGameAndUI InputMode;
    InputMode.SetWidgetToFocus(ActiveInventoryWidget->TakeWidget());
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    PC->SetInputMode(InputMode);
    PC->SetShowMouseCursor(true);

    if (GothicHUD)
    {
        GothicHUD->SetCrosshairVisible(false);
    }

}

int32 AGothicPlayerCharacter::EquipSlotToWeaponIndex(EGothicEquipSlot Slot)
{
    switch (Slot)
    {
        case EGothicEquipSlot::Sidearm: return 0;
        case EGothicEquipSlot::Piece:   return 1;
        case EGothicEquipSlot::Rig:     return 2;
        default:                        return -1;
    }
}

bool AGothicPlayerCharacter::SwapToWeaponForEquipSlot(EGothicEquipSlot EquipSlot)
{
    const int32 WeaponIndex = EquipSlotToWeaponIndex(EquipSlot);
    if (WeaponSlots.IsValidIndex(WeaponIndex))
    {
        SwapWeapon(WeaponIndex);
        return true;
    }
    return false;
}

void AGothicPlayerCharacter::OnEquipmentChanged(EGothicEquipSlot Slot, const FGothicItemInstance& Item)
{
    // Any equip can change the MovementSpeed secondary, and attribute changes do
    // not reach CharacterMovement on their own — armour included, so this runs
    // before the weapon-slot early-out below.
    RefreshMovementSpeed();

    const int32 WeaponIndex = EquipSlotToWeaponIndex(Slot);
    if (WeaponIndex < 0)
    {
        // Not a weapon slot — armor equip, nothing further here
        return;
    }

    if (!WeaponSlots.IsValidIndex(WeaponIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("OnEquipmentChanged: WeaponSlots index %d out of range"), WeaponIndex);
        return;
    }

    if (Item.Definition && Item.Definition->IsWeapon())
    {
        WeaponSlots[WeaponIndex].WeaponData = Item.Definition->WeaponData;

        // Carry the item's Gear Power across so the slot knows its tier. This is
        // now load-bearing: GetActiveWeaponTierMultiplier() divides it by the
        // Tier-1 baseline and GA_Fire multiplies the weapon's authored damage by
        // the result, so a Tier-3 Revolver of this archetype hits for x3.
        //
        // It does NOT make two copies of the same tier differ.
        // FGothicItemInstance::GearPower comes from
        // UGothicItemDefinition::GetGearPower(), which is `GearTier * 100` — a
        // property of the definition, identical for every copy rolled from it.
        // Per-copy variation lives in the rolled secondaries, not here.
        WeaponSlots[WeaponIndex].GearPower = Item.GearPower;

        // Instance retention — the perk seam. Unlike GearPower above, THIS is
        // what makes two copies of the same archetype differ: the perks were
        // rolled per drop and would otherwise be discarded at this boundary,
        // leaving the fire path with only the shared asset to read.
        //
        // Set before InitFromData, which asks the slot for its effective reserve
        // and so must already be able to see Deep Reserves.
        WeaponSlots[WeaponIndex].EquippedInstanceID = Item.InstanceID;
        WeaponSlots[WeaponIndex].Perks = Item.WeaponPerks;

        WeaponSlots[WeaponIndex].InitFromData();
    }
    else
    {
        // Unequipped or non-weapon item — clear the slot
        WeaponSlots[WeaponIndex].WeaponData = nullptr;
        WeaponSlots[WeaponIndex].CurrentMagazine = 0;
        WeaponSlots[WeaponIndex].CurrentReserve = 0;
        WeaponSlots[WeaponIndex].GearPower = 0;
        WeaponSlots[WeaponIndex].EquippedInstanceID.Invalidate();
        WeaponSlots[WeaponIndex].Perks.Reset();
    }

    // Equipping into the active slot swaps the weapon out from under any in-progress
    // hold, so abandon it — the conversion cost was tied to the old weapon's tier.
    if (WeaponIndex == ActiveWeaponIndex)
    {
        EndSteadfastHold();
        // The active weapon changed under the hold — close the reload window too.
        SetReloadingHold(false);
        RefreshWeaponVisuals(WeaponIndex);
        PushAmmoToHUD();
    }
}

void AGothicPlayerCharacter::ApplyWeaponAttachment(const UGothicWeaponData* WeaponData)
{
    if (!FPWeaponMesh)
    {
        return;
    }

    // The gun is BORN on the hand (constructor SetupAttachment onto FirstPersonArmsMesh @
    // FPWeaponGripSocket), so in the normal path this call only RE-ASSERTS that seat and
    // stamps the per-weapon grip transform on a swap — it is idempotent, not a re-parent
    // from scratch. The arms are a camera child seated at ArmsOffset, so the whole
    // arms+gun assembly tracks the reticle rigidly by construction. Gate is mesh-only: a
    // SkeletalMesh asset is assigned AND the grip socket exists (no anim class needed — the
    // arms move as one rigid camera child whether single-node, ref-pose, or ABP-driven).
    const bool bArmsValid =
        FirstPersonArmsMesh != nullptr
        && FirstPersonArmsMesh->GetSkeletalMeshAsset() != nullptr;

    // Only the local player has a meaningful first-person mount. A simulated proxy shows the
    // gun through ThirdPersonWeaponMesh; FPWeaponMesh is OnlyOwnerSee so it renders nothing on
    // a proxy's machine regardless of where it hangs — the body-hand fallback below is purely
    // to keep it out of the way.
    if (IsLocallyControlled() && FirstPersonCamera != nullptr)
    {
        if (bArmsValid && FirstPersonArmsMesh->DoesSocketExist(FPWeaponGripSocket))
        {
            // Re-assert the born-on-hand seat. WeaponData->MeshOffset/MeshRotation were
            // authored to seat the mesh inside a hand grip — exactly this socket's frame — so
            // the same values that align the third-person hand mount apply here.
            FPWeaponMesh->AttachToComponent(
                FirstPersonArmsMesh,
                FAttachmentTransformRules::SnapToTargetNotIncludingScale,
                FPWeaponGripSocket);

            if (WeaponData)
            {
                FPWeaponMesh->SetRelativeLocation(WeaponData->MeshOffset);
                FPWeaponMesh->SetRelativeRotation(WeaponData->MeshRotation);
            }

            WeaponMountState = EFPWeaponMount::ArmsSocket;
            UE_LOG(LogVigilCombat, Log,
                TEXT("VigilTimeline|t=%.3f|%s|FPGun|MOUNT|comp=FPWeaponMesh|parent=%s|socket=%s|mount=ArmsSocket"),
                GASInitTimelineNow(this), *GetName(),
                *GetNameSafe(FPWeaponMesh->GetAttachParent()), *FPWeaponGripSocket.ToString());
            return;
        }

        if (bArmsValid)
        {
            // Arms mesh assigned but the grip socket is missing — the born seat resolved to the
            // mesh root, stacking the gun at the arms origin. Fall back to the camera mount so
            // the player still sees a usable weapon, and say so once.
            UE_LOG(LogVigilCombat, Warning,
                TEXT("VigilTimeline|t=%.3f|%s|FPWeaponMount|socket-missing|socket=%s|mesh=%s|fallback=camera"),
                GASInitTimelineNow(this), *GetName(), *FPWeaponGripSocket.ToString(),
                FirstPersonArmsMesh->GetSkeletalMeshAsset()
                    ? *FirstPersonArmsMesh->GetSkeletalMeshAsset()->GetName() : TEXT("None"));
        }

        if (bArmsValid || bAttachWeaponToCamera)
        {
            // Camera fallback. RETAINED (author's call, per brief item 3) for two cases the
            // born-on-hand seat cannot serve on its own: (a) no arms mesh assigned yet — the
            // pre-editor-pass window, where the gun would otherwise sit invisibly at the empty
            // arms component's origin — and (b) the socket-missing case above. In both the gun
            // re-parents to the camera at CameraWeaponOffset so it stays visible and usable; the
            // camera-space pose write in UpdateFirstPersonWeaponPose engages only in THIS state.
            FPWeaponMesh->AttachToComponent(
                FirstPersonCamera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

            FPWeaponMesh->SetRelativeLocation(CameraWeaponOffset);
            FPWeaponMesh->SetRelativeRotation(CameraWeaponRotation);

            WeaponMountState = EFPWeaponMount::Camera;
            UE_LOG(LogVigilCombat, Log,
                TEXT("VigilTimeline|t=%.3f|%s|FPGun|MOUNT|comp=FPWeaponMesh|parent=%s|socket=none|mount=Camera"),
                GASInitTimelineNow(this), *GetName(),
                *GetNameSafe(FPWeaponMesh->GetAttachParent()));
            return;
        }
        // else: local, no arms mesh, and bAttachWeaponToCamera explicitly turned off — the
        // third-person-view toggle. Fall through to the body hand mount below.
    }

    if (GetMesh())
    {
        FPWeaponMesh->AttachToComponent(
            GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("HandGrip_R"));

        if (WeaponData)
        {
            FPWeaponMesh->SetRelativeLocation(WeaponData->MeshOffset);
            FPWeaponMesh->SetRelativeRotation(WeaponData->MeshRotation);
        }

        WeaponMountState = EFPWeaponMount::BodyHand;
        UE_LOG(LogVigilCombat, Log,
            TEXT("VigilTimeline|t=%.3f|%s|FPGun|MOUNT|comp=FPWeaponMesh|parent=%s|socket=HandGrip_R|mount=BodyHand"),
            GASInitTimelineNow(this), *GetName(), *GetNameSafe(FPWeaponMesh->GetAttachParent()));
    }
}

void AGothicPlayerCharacter::NeutralizeDuplicateWeaponMesh()
{
    // Retire BOTH legacy first-person guns now that FPWeaponMesh is the real one. The two
    // dead components are the BP ghost "UWeaponMesh" (the WeaponMeshComponent pointer's
    // redirect target, camera-parented and undeletable) and the native constructor subobject
    // "WeaponMesh" — each would otherwise render a second owner-only gun. Neither is deletable
    // (banked trap / BP-serialized redirect), so both are hidden in place. Found by name;
    // FPWeaponMesh and ThirdPersonWeaponMesh are skipped by pointer so the two LIVE guns are
    // never touched (name skip alone would be enough — FPWeaponMesh/ThirdPersonWeaponMesh do
    // not match the retired names — but the pointer skip makes the intent explicit).
    TArray<UStaticMeshComponent*> MeshComps;
    GetComponents<UStaticMeshComponent>(MeshComps);
    TArray<FString> Neutralized;
    for (UStaticMeshComponent* Comp : MeshComps)
    {
        if (!Comp || Comp == FPWeaponMesh || Comp == ThirdPersonWeaponMesh)
        {
            continue;
        }

        const FString CompName = Comp->GetName();
        if (CompName == TEXT("WeaponMesh") || CompName == TEXT("UWeaponMesh"))
        {
            Comp->SetHiddenInGame(true);
            Comp->SetVisibility(false);
            Neutralized.Add(CompName);
        }
    }

    UE_LOG(LogVigilCombat, Log,
        TEXT("VigilTimeline|t=%.3f|%s|FPGun|DEDUPE|neutralized=[%s]|kept=%s"),
        GASInitTimelineNow(this), *GetName(),
        *FString::Join(Neutralized, TEXT(",")), *GetNameSafe(FPWeaponMesh));
}

void AGothicPlayerCharacter::AddWeaponFireKick()
{
    // Per-weapon feel: FireKickScale sizes the visible jolt, FireKickClimb sets how
    // far sustained fire may stack it. Both fall back to the pawn's baseline (scale 1,
    // MaxStackedFireKick) when no weapon data is active. FireKickOffset/Rotation stay
    // the shared base SHAPE of a kick; the scalar sizes it per gun.
    const UGothicWeaponData* WeaponData = GetActiveWeaponData();
    const float KickScale = WeaponData ? WeaponData->FireKickScale : 1.f;
    const float Ceiling = FMath::Max(1.f, WeaponData ? WeaponData->FireKickClimb : MaxStackedFireKick);

    const FVector KickLoc = FireKickOffset * KickScale;
    const FRotator KickRot = FireKickRotation * KickScale;

    // Stack, then clamp. Repeating a single identical hop reads as a looping
    // animation rather than a gun fighting back, but unbounded stacking walks the
    // weapon out of frame on anything with a fast fire rate.
    CurrentFireKickLocation += KickLoc;
    CurrentFireKickRotation += KickRot;

    const FVector MaxLoc = KickLoc * Ceiling;
    CurrentFireKickLocation.X = FMath::Clamp(CurrentFireKickLocation.X,
        FMath::Min(0.f, MaxLoc.X), FMath::Max(0.f, MaxLoc.X));
    CurrentFireKickLocation.Y = FMath::Clamp(CurrentFireKickLocation.Y,
        FMath::Min(0.f, MaxLoc.Y), FMath::Max(0.f, MaxLoc.Y));
    CurrentFireKickLocation.Z = FMath::Clamp(CurrentFireKickLocation.Z,
        FMath::Min(0.f, MaxLoc.Z), FMath::Max(0.f, MaxLoc.Z));

    const FRotator MaxRot = KickRot * Ceiling;
    CurrentFireKickRotation.Pitch = FMath::Clamp(CurrentFireKickRotation.Pitch,
        FMath::Min(0.f, MaxRot.Pitch), FMath::Max(0.f, MaxRot.Pitch));
    CurrentFireKickRotation.Yaw = FMath::Clamp(CurrentFireKickRotation.Yaw,
        FMath::Min(0.f, MaxRot.Yaw), FMath::Max(0.f, MaxRot.Yaw));
    CurrentFireKickRotation.Roll = FMath::Clamp(CurrentFireKickRotation.Roll,
        FMath::Min(0.f, MaxRot.Roll), FMath::Max(0.f, MaxRot.Roll));
}

void AGothicPlayerCharacter::UpdateFirstPersonWeaponPose(float DeltaTime)
{
    if (!FPWeaponMesh || !bAttachWeaponToCamera || !FirstPersonCamera)
    {
        return;
    }

    // Blended, not switched — the weapon should swing down as the sprint starts
    // rather than appear in the lowered pose on the first frame.
    SprintPoseAlpha = FMath::FInterpTo(
        SprintPoseAlpha, IsSprinting() ? 1.f : 0.f, DeltaTime, FMath::Max(0.5f, SprintPoseBlendSpeed));

    CurrentFireKickLocation = FMath::VInterpTo(
        CurrentFireKickLocation, FVector::ZeroVector, DeltaTime, FMath::Max(0.5f, FireKickRecoverySpeed));
    CurrentFireKickRotation = FMath::RInterpTo(
        CurrentFireKickRotation, FRotator::ZeroRotator, DeltaTime, FMath::Max(0.5f, FireKickRecoverySpeed));

    const FVector TargetLocation =
        CameraWeaponOffset + (SprintWeaponOffset * SprintPoseAlpha) + CurrentFireKickLocation;

    // Compose rotations as quaternions with the offsets OUTERMOST, so they act in
    // camera space. Adding Euler components instead would apply them in the weapon's
    // own frame — and this mesh is yawed -90, so its local pitch axis runs straight
    // down the barrel. "Muzzle rises" would have come out as the gun rolling on its
    // side. The same trap cost a session's worth of tuning on the spine pin.
    const FQuat BaseQ   = CameraWeaponRotation.Quaternion();
    const FQuat SprintQ = FRotator(SprintWeaponRotation.Pitch * SprintPoseAlpha,
                                   SprintWeaponRotation.Yaw   * SprintPoseAlpha,
                                   SprintWeaponRotation.Roll  * SprintPoseAlpha).Quaternion();
    const FQuat KickQ   = CurrentFireKickRotation.Quaternion();

    // Drive the weapon component directly ONLY when it is actually camera-mounted. In
    // the animated-hand mount (WeaponMountState == ArmsSocket) the gun is a CHILD of
    // FirstPersonArmsMesh at the grip socket; a camera-space write here would stomp that
    // grip transform every frame and tear the gun off the hand. Gate on the ACTUAL mount,
    // not bAttachWeaponToCamera — that flag is still true in the hand mount. The arms
    // write below carries the whole arms+gun assembly through sprint/kick in that mode.
    if (WeaponMountState == EFPWeaponMount::Camera)
    {
        FPWeaponMesh->SetRelativeLocation(TargetLocation);
        FPWeaponMesh->SetRelativeRotation((KickQ * SprintQ * BaseQ).Rotator());
    }

    // The arms sway/kick write. In the classic assembly the arms ARE camera-parented, so
    // this gate is TRUE and the write is live — exactly the assembly it was authored for.
    // ArmsOffset is the camera-relative resting seat and the sprint/kick deltas are added on
    // top, all in CAMERA space (the arms' own frame), so the whole arms+gun assembly swings
    // through sprint and settles from a shot. The rotation composes as quaternions with
    // ArmsBaseQ innermost and the KickQ/SprintQ deltas OUTERMOST, so they act in camera space
    // rather than the -90-yawed mesh's own frame (the same trap the weapon write above
    // documents). Gated on the ACTUAL parent, not a config flag: if a future mesh were
    // hand-mounted off the camera this would correctly skip. When the gun rides the arms grip
    // socket (WeaponMountState == ArmsSocket) it is a CHILD of these arms, so this single write
    // carries the gun too; when the gun is camera-mounted (Camera) the write above moves it in
    // lockstep with the same deltas.
    if (FirstPersonArmsMesh && FirstPersonArmsMesh->GetAttachParent() == FirstPersonCamera)
    {
        const FVector ArmsDelta = (SprintWeaponOffset * SprintPoseAlpha) + CurrentFireKickLocation;
        FirstPersonArmsMesh->SetRelativeLocation(ArmsOffset + ArmsDelta);
        const FQuat ArmsBaseQ = ArmsRotation.Quaternion();
        FirstPersonArmsMesh->SetRelativeRotation((KickQ * SprintQ * ArmsBaseQ).Rotator());
    }
}

void AGothicPlayerCharacter::RefreshWeaponVisuals(int32 SlotIndex)
{
    if (!FPWeaponMesh || !WeaponSlots.IsValidIndex(SlotIndex))
    {
        // This is where a missing gun goes quiet: an early return here leaves the last
        // mesh (or none) on screen with no other symptom. Name which guard tripped.
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|WeaponVisuals|SKIPPED|slot=%d|reason=%s"),
            GASInitTimelineNow(this), *GetName(), SlotIndex,
            !FPWeaponMesh ? TEXT("null-FPWeaponMesh") : TEXT("invalid-slot-index"));
        return;
    }

    const UGothicWeaponData* WeaponData = WeaponSlots[SlotIndex].WeaponData;
    if (WeaponData)
    {
        FPWeaponMesh->SetStaticMesh(WeaponData->WeaponMesh);
        FPWeaponMesh->SetRelativeScale3D(WeaponData->MeshScale);

        // Mirror the same mesh onto the third-person hand mount so OTHER players see the
        // gun in Manny's hand. Owner-hidden by the ctor's SetOwnerNoSee; grip alignment is
        // the whitebox ThirdPersonWeaponOffset/Rotation (a later content-pass tune, not a
        // per-weapon value yet). Scale tracks the FP weapon's so the two never diverge.
        if (ThirdPersonWeaponMesh)
        {
            ThirdPersonWeaponMesh->SetStaticMesh(WeaponData->WeaponMesh);
            ThirdPersonWeaponMesh->SetRelativeScale3D(WeaponData->MeshScale);
            ThirdPersonWeaponMesh->SetRelativeLocationAndRotation(
                ThirdPersonWeaponOffset, ThirdPersonWeaponRotation);
        }

        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|WeaponVisuals|APPLIED|slot=%d|mesh=%s"),
            GASInitTimelineNow(this), *GetName(), SlotIndex,
            WeaponData->WeaponMesh ? *WeaponData->WeaponMesh->GetName() : TEXT("None"));

        // Location and rotation come from here, not from WeaponData directly — which
        // of the two offsets is correct depends on what the weapon is parented to.
        ApplyWeaponAttachment(WeaponData);

        if (IsLocallyControlled())
        {
            APlayerController* PC = Cast<APlayerController>(GetController());
            AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
            if (GothicHUD)
            {
                GothicHUD->SetCrosshairType(WeaponData->CrosshairType);
            }
        }
    }
    else
    {
        FPWeaponMesh->SetStaticMesh(nullptr);
        if (ThirdPersonWeaponMesh)
        {
            ThirdPersonWeaponMesh->SetStaticMesh(nullptr);
        }

        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|WeaponVisuals|CLEARED|slot=%d|reason=null-WeaponData"),
            GASInitTimelineNow(this), *GetName(), SlotIndex);
    }
}
// ---------------------------------------------------------------------------
// Gothic.Revive <PlayerIndex> — debug console command
//
// The counterpart to Gothic.SetDowned, and here for the same reason that one
// exists: ReviveFromDowned is BlueprintCallable and server-only, and until PR-3
// ships the channelled revive ability there is no surface in the running game
// that can call it. Without this the in-place revive — the passive re-grant
// above all — would ship unverified.
//
// Drives the real function, not a shortcut: every guard ReviveFromDowned has
// (authority, actually-downed) still applies, so what a verification run proves
// here is what the revive ability will get.
//
// PlayerIndex indexes the game state's PlayerArray exactly as Gothic.SetDowned
// does — 0 is the host on a listen server, 1 the first joining player — and the
// command prints the resolved name so the caller can confirm who came back.
// ---------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
static void GothicReviveConsoleCommand(const TArray<FString>& Args, UWorld* World)
{
    if (!World)
    {
        return;
    }

    if (Args.Num() < 1)
    {
        UE_LOG(LogVigilCombat, Warning, TEXT("Gothic.Revive: usage is Gothic.Revive <PlayerIndex>."));
        return;
    }

    const AGameStateBase* GS = World->GetGameState();
    if (!GS)
    {
        UE_LOG(LogVigilCombat, Warning, TEXT("Gothic.Revive: no game state in this world."));
        return;
    }

    const int32 PlayerIndex = FCString::Atoi(*Args[0]);
    if (!GS->PlayerArray.IsValidIndex(PlayerIndex))
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.Revive: no player at index %d — this world has %d."),
            PlayerIndex, GS->PlayerArray.Num());
        return;
    }

    const APlayerState* PS = GS->PlayerArray[PlayerIndex];
    AGothicPlayerCharacter* Char = PS ? Cast<AGothicPlayerCharacter>(PS->GetPawn()) : nullptr;
    if (!Char)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.Revive: player %d has no AGothicPlayerCharacter pawn."), PlayerIndex);
        return;
    }

    // Typed into a client's console this reaches the client's copy, where
    // ReviveFromDowned correctly refuses. Say so rather than appearing to work.
    if (!Char->HasAuthority())
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.Revive: %s is not authoritative in this world — run this on the server."),
            *GetNameSafe(Char));
        return;
    }

    if (!Char->IsDowned())
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.Revive: player %d ('%s') is not downed — nothing to revive."),
            PlayerIndex, *PS->GetPlayerName());
        return;
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("Gothic.Revive: reviving %s (index %d, player '%s')."),
        *GetNameSafe(Char), PlayerIndex, *PS->GetPlayerName());

    Char->ReviveFromDowned();
}

static FAutoConsoleCommandWithWorldAndArgs GGothicReviveCmd(
    TEXT("Gothic.Revive"),
    TEXT("Gothic.Revive <PlayerIndex> — revive a downed player in place on the authority. Debug only."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GothicReviveConsoleCommand));

// ---------------------------------------------------------------------------
// Gothic.StartReviveChannel / Gothic.EndReviveChannel — debug console commands
//
// The harness cannot hold a key down, and the channel's whole point is that it is
// a hold. These open and close the SERVER-SIDE channel directly, which is the
// exact thing ServerStartReviveChannel_Implementation does when a real press
// arrives — so what runs here is the shipping path, not a shortcut past it. Every
// guard in StartReviveChannel still applies, and a channel opened this way is
// interrupted by damage, by movement and by the body bleeding out exactly as a
// held one is. Only the input edge is faked.
//
// Indices are into the game state's PlayerArray, as with Gothic.Revive and
// Gothic.SetDowned. TargetIndex is optional: omitted, the reviver's own
// FindReviveTargetInRange picks the nearest downed body, which is also what a real
// press would have used.
// ---------------------------------------------------------------------------

/** Shared resolution for both commands. Logs and returns null on every failure. */
static AGothicPlayerCharacter* GothicResolveReviverPawn(
    const UWorld* World, const TArray<FString>& Args, int32 ArgIndex, const TCHAR* CommandName)
{
    const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
    if (!GS)
    {
        UE_LOG(LogVigilCombat, Warning, TEXT("%s: no game state in this world."), CommandName);
        return nullptr;
    }

    const int32 Index = FCString::Atoi(*Args[ArgIndex]);
    if (!GS->PlayerArray.IsValidIndex(Index))
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("%s: no player at index %d — this world has %d."),
            CommandName, Index, GS->PlayerArray.Num());
        return nullptr;
    }

    const APlayerState* PS = GS->PlayerArray[Index];
    AGothicPlayerCharacter* Char = PS ? Cast<AGothicPlayerCharacter>(PS->GetPawn()) : nullptr;
    if (!Char)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("%s: player %d has no AGothicPlayerCharacter pawn."), CommandName, Index);
        return nullptr;
    }

    // Typed into a client's console this reaches the client's copy, where the
    // channel functions correctly refuse. Say so rather than appearing to work.
    if (!Char->HasAuthority())
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("%s: %s is not authoritative in this world — run this on the server."),
            CommandName, *GetNameSafe(Char));
        return nullptr;
    }

    return Char;
}

static void GothicStartReviveChannelConsoleCommand(const TArray<FString>& Args, UWorld* World)
{
    if (!World)
    {
        return;
    }

    if (Args.Num() < 1)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.StartReviveChannel: usage is Gothic.StartReviveChannel <ReviverIndex> [TargetIndex]."));
        return;
    }

    AGothicPlayerCharacter* Reviver =
        GothicResolveReviverPawn(World, Args, 0, TEXT("Gothic.StartReviveChannel"));
    if (!Reviver)
    {
        return;
    }

    AGothicPlayerCharacter* Target = nullptr;
    if (Args.Num() >= 2)
    {
        Target = GothicResolveReviverPawn(World, Args, 1, TEXT("Gothic.StartReviveChannel"));
        if (!Target)
        {
            return;
        }
    }
    else
    {
        Target = Reviver->FindReviveTargetInRange();
        if (!Target)
        {
            UE_LOG(LogVigilCombat, Warning,
                TEXT("Gothic.StartReviveChannel: no downed player within %.0fuu of %s — "
                     "pass an explicit TargetIndex, or move the reviver closer."),
                Reviver->ReviveChannelRange, *GetNameSafe(Reviver));
            return;
        }
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("Gothic.StartReviveChannel: %s channelling on %s over %.1fs."),
        *GetNameSafe(Reviver), *GetNameSafe(Target), Reviver->ReviveChannelSeconds);

    Reviver->StartReviveChannel(Target);

    // StartReviveChannel refuses silently on a bad state (target not downed, out of
    // range, slot taken). Report the outcome so a failed setup does not read as a
    // failed feature.
    if (!Reviver->IsChannelingRevive())
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.StartReviveChannel: refused — check the target is downed, within %.0fuu, "
                 "and not already being revived."),
            Reviver->ReviveChannelRange);
    }
}

static void GothicEndReviveChannelConsoleCommand(const TArray<FString>& Args, UWorld* World)
{
    if (!World)
    {
        return;
    }

    if (Args.Num() < 1)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.EndReviveChannel: usage is Gothic.EndReviveChannel <ReviverIndex>."));
        return;
    }

    AGothicPlayerCharacter* Reviver =
        GothicResolveReviverPawn(World, Args, 0, TEXT("Gothic.EndReviveChannel"));
    if (!Reviver)
    {
        return;
    }

    if (!Reviver->IsChannelingRevive())
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.EndReviveChannel: %s is not channelling — nothing to break."),
            *GetNameSafe(Reviver));
        return;
    }

    // The same route the key release takes, so this proves the release path and
    // not a private one.
    Reviver->CancelReviveChannel(TEXT("console-released"));
}

static FAutoConsoleCommandWithWorldAndArgs GGothicStartReviveChannelCmd(
    TEXT("Gothic.StartReviveChannel"),
    TEXT("Gothic.StartReviveChannel <ReviverIndex> [TargetIndex] — open a server-side revive channel "
         "without holding a key. Debug only."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GothicStartReviveChannelConsoleCommand));

static FAutoConsoleCommandWithWorldAndArgs GGothicEndReviveChannelCmd(
    TEXT("Gothic.EndReviveChannel"),
    TEXT("Gothic.EndReviveChannel <ReviverIndex> — break an in-progress revive channel, as a key "
         "release would. Debug only."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GothicEndReviveChannelConsoleCommand));
#endif
