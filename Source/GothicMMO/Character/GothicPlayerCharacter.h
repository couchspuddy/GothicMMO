// GothicPlayerCharacter.h
// The player's physical character.
// ASC and AttributeSet live on AGothicPlayerState for persistence across respawns.
//
// Input architecture — Lyra style:
//   - Non-ability input (move, look, jump, fire, melee) bound directly in SetupPlayerInputComponent
//   - Ability input routed through UGothicInputHandlerComponent -> ASC tag pipeline
//   - Abilities granted via UGothicAbilitySet data assets, not hardcoded arrays
//
// Blueprint child: BP_GothicPlayerCharacter
//   - Assign DefaultMappingContext
//   - Assign MoveAction, LookAction, JumpAction, FireAction
//   - Assign StartupAbilitySets (DA_HunterAbilitySet etc.)
//   - Assign InputHandler->InputConfig (DA_GothicInputConfig)

#pragma once

#include "CoreMinimal.h"
#include "Character/GothicCharacterBase.h"
#include "Weapons/GothicWeaponData.h"
#include "Items/GothicItemTypes.h"
#include "GothicPlayerCharacter.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UInputMappingContext;
class UInputAction;
class UGothicInputHandlerComponent;
class UGothicAbilitySet;
class UGothicInventoryWidget;
class UGA_TheLovedAndTheLost;
struct FInputActionValue;

UCLASS()
class GOTHICMMO_API AGothicPlayerCharacter : public AGothicCharacterBase
{
    GENERATED_BODY()

public:
    AGothicPlayerCharacter();

    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void OnRep_PlayerState() override;
    virtual void Tick(float DeltaTime) override;

    /**
     * Unbinds the HUD attribute delegates.
     *
     * This is not housekeeping — it is the fix for a reproducible respawn crash.
     * See UnbindHUDAttributeDelegates.
     */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // -----------------------------------------------------------------
    // Aim down sights
    //
    // Three effects, because any one alone reads as a gimmick: the camera pulls
    // in, the weapon tightens, and you give up speed for it. The trade is the
    // point — aiming should be the accurate option and the slow one.
    // -----------------------------------------------------------------

    /** True while the aim input is held. Replicated because GA_Fire's trace runs
     *  on the server, and spread cannot be decided from a state only the shooter
     *  knows. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gothic|Aim")
    bool bIsAiming = false;

    UFUNCTION(BlueprintPure, Category = "Gothic|Aim")
    bool IsAiming() const { return bIsAiming; }

    /** True while the sprint input is held. Read by the anim instance, which
     *  releases the upper-body pin during a sprint. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Movement")
    bool IsSprinting() const { return bIsSprinting; }

    /** The first-person camera. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Camera")
    UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

    // -------------------------------------------------------------------------
    // First-person weapon presentation
    //
    // The weapon is parented to the CAMERA for the local player, not to the hand.
    // This is the thing that actually locks the muzzle to the crosshair, and it
    // does so by construction rather than by correction: a child of the camera
    // cannot move relative to the camera, at any pitch, during any animation.
    //
    // Everything before this tried to achieve the same result from the other end —
    // rotate the torso so the hand happens to land where the camera is looking. That
    // can only ever approximate, because the weapon hangs off a bone chain the camera
    // knows nothing about, and every fix was correct at exactly one look angle.
    //
    // Note this is what the Blueprint already had. BeginPlay was re-attaching it to
    // HandGrip_R on every launch, which is why it never behaved.
    // -------------------------------------------------------------------------

    /**
     * Parent the weapon to the camera for the locally-controlled player.
     *
     * Turn this off to go back to hand-socket attachment, where the weapon inherits
     * the animation — correct for a third-person view, unusable for a first-person one.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson")
    bool bAttachWeaponToCamera = true;

    /**
     * Where the weapon sits relative to the camera: +X forward, +Y right, +Z up.
     *
     * This REPLACES WeaponData's MeshOffset while camera-attached, because that value
     * was authored to align the mesh inside a hand and means nothing in camera space.
     * Expect to tune this once per weapon silhouette.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson")
    FVector CameraWeaponOffset = FVector(30.f, 12.f, -12.f);

    /** Weapon orientation relative to the camera. Replaces WeaponData's MeshRotation
     *  while camera-attached, for the same reason. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson")
    FRotator CameraWeaponRotation = FRotator::ZeroRotator;

    /**
     * Hide the character mesh from its own owner.
     *
     * The constructor already asks for this, but a serialized Blueprint value silently
     * overrides a constructor call, which is why a shoulder was still in frame. Enforced
     * at BeginPlay so the Blueprint cannot lose it. Other players still see the full body.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson")
    bool bHideBodyInFirstPerson = true;

    // -------------------------------------------------------------------------
    // Weapon pose: sprint and fire kick
    //
    // These replace what the body animation used to do. Once the weapon became a
    // child of the camera it stopped inheriting the skeleton, so a sprint montage
    // or a fire montage moves the character's arms and nothing the player can see.
    // Moving the weapon in CAMERA space is the equivalent, and it has the advantage
    // of being frame-rate independent, tunable without a re-import, and incapable
    // of drifting off the crosshair.
    //
    // All offsets below are in camera space and are applied ON TOP of
    // CameraWeaponOffset / CameraWeaponRotation, never in the weapon's own frame.
    // -------------------------------------------------------------------------

    /** Where the weapon moves while sprinting, relative to its resting pose.
     *  Negative X pulls it back toward the player, negative Z drops it. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson")
    FVector SprintWeaponOffset = FVector(-8.f, 4.f, -10.f);

    /** How the weapon tilts while sprinting. Negative pitch points the muzzle down. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson")
    FRotator SprintWeaponRotation = FRotator(-30.f, -15.f, 0.f);

    /** How quickly the weapon moves between resting and sprinting poses. Lower is
     *  heavier; too high and the transition snaps. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson",
              meta = (ClampMin = "0.5"))
    float SprintPoseBlendSpeed = 9.f;

    /** Positional kick per shot. Negative X drives the weapon back toward the eye. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson")
    FVector FireKickOffset = FVector(-3.f, 0.f, 0.5f);

    /** Rotational kick per shot. Positive pitch lifts the muzzle. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson")
    FRotator FireKickRotation = FRotator(6.f, 0.f, 0.f);

    /** How fast the weapon settles back after a shot. This is the whole feel of the
     *  gun — high is snappy and mechanical, low is heavy and slow to recover. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson",
              meta = (ClampMin = "0.5"))
    float FireKickRecoverySpeed = 11.f;

    /**
     * Ceiling on accumulated kick, as a multiple of a single shot.
     *
     * Kicks stack so that sustained fire climbs rather than repeating one identical
     * hop, but without a ceiling a fast weapon walks the gun clean out of frame.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons|FirstPerson",
              meta = (ClampMin = "1.0"))
    float MaxStackedFireKick = 2.5f;

    /** Add one shot's worth of visual kick. Called from GA_Fire on the local client;
     *  cosmetic only, and deliberately separate from ApplyRecoilKick, which moves the
     *  player's actual aim. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Weapons|FirstPerson")
    void AddWeaponFireKick();

    /** FOV while aiming. The hip value is captured from the camera at BeginPlay,
     *  so whatever the Blueprint sets stays the resting FOV. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Aim")
    float ADSFieldOfView = 55.f;

    /** How fast FOV moves between hip and aimed. Higher snaps; lower drifts. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Aim")
    float ADSFieldOfViewInterpSpeed = 12.f;

    /** MaxWalkSpeed multiplier while aiming. Applied inside RefreshMovementSpeed
     *  so gear bonuses and sprint cannot overwrite it. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Aim",
              meta = (ClampMin = "0.1", ClampMax = "1.0"))
    float ADSMoveSpeedMultiplier = 0.4f;

    /**
     * Death by damage. Everything the base class does (State.Dead, cancel
     * abilities, drop collision, stop moving) plus the half that was missing:
     * telling the game mode to respawn us.
     *
     * Overridden here rather than added to AGothicCharacterBase so enemies —
     * which share that base and route through it on every kill — cannot possibly
     * be affected.
     */
    virtual void OnDeath_Implementation(AActor* Killer) override;

    /** Safety net for falling under the map / below KillZ — routes into the
     *  normal checkpoint respawn instead of the default (which just destroys the
     *  pawn and strands the controller). */
    virtual void FellOutOfWorld(const class UDamageType& DmgType) override;

    /**
     * Z height below which the player is treated as having fallen out of the
     * playable space and is respawned. This is the reliable trigger — the
     * engine's KillZ often sits far lower than any level's floor, so a player
     * who falls through geometry never reaches it. Checked every Tick on
     * authority. Set per-level below the lowest legitimate floor.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Respawn")
    float FallRespawnZ = -2000.f;

    /** Shared respawn-on-fall path used by both FellOutOfWorld and the Tick
     *  height check. Authority-only; no-op if already handled or no game mode. */
    void TriggerFallRespawn();

    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    void TriggerSelahMoment();

    /**
     * True while the Selah moment holds the player still.
     *
     * The moment is the one beat in the game that is not combat: the names of the
     * Accursed you just killed are read out, and the player is meant to be
     * standing in it rather than strafing through it. Movement and weapon fire are
     * refused; the inventory and the quit menu are NOT, because taking the pause
     * to read your gear is exactly what the beat is for.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Selah")
    bool IsSelahMomentLocked() const { return bSelahMomentLock; }

    /**
     * Releases the lock early. Call from the name-cycle widget's
     * OnSelahMomentComplete so the lock ends exactly when the last name fades,
     * rather than on the fallback timer.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    void EndSelahMomentLock();

    /**
     * Fallback duration for the lock, in seconds.
     *
     * A timer rather than purely waiting on the widget, because the widget lives
     * on the client and can be destroyed mid-cycle (level travel, a respawn, an
     * interrupted collection). If its completion event never arrives, this is what
     * stops the player being frozen for the rest of the run. Keep it comfortably
     * longer than the name cycle — the widget normally releases the lock first.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float SelahMomentLockSeconds = 8.f;


    /** True if the active weapon has at least one round in its magazine. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    bool HasRoundChambered() const;

    /** Decrements the active weapon's magazine by one round. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Weapons")
    void ConsumeRound();

    /** Applies recoil camera kick — cosmetic, runs on the local client. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Weapons")
    void ApplyRecoilKick();

    /** Returns the active weapon's data asset for stat lookups. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    const UGothicWeaponData* GetActiveWeaponData() const;

    /**
     * Gear Power of the item in the active weapon slot, or 0 when the slot was
     * filled from the Blueprint default loadout rather than a rolled drop.
     *
     * Has no callers anywhere in Source/ — in particular NOT
     * UGA_Fire::PerformFireTrace, which the old comment named and which never
     * consults it. It exists for weapon damage scaling that has not been
     * implemented. Do not assume equipping a higher-tier weapon changes damage
     * through this path.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    int32 GetActiveGearPower() const;

    /** Overall Gear Power (average across equipped gear) — the damage floor and
     *  activity gate. Delegates to the inventory on the PlayerState. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    int32 GetAggregateGearPower() const;

    /** Armor's summed damage bonus (percent) for a given weapon archetype.
     *  GA_Fire passes the active weapon's archetype so only matching lines count. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    float GetArchetypeDamageBonusPct(EGothicWeaponArchetype Archetype) const;

    // -------------------------------------------------------------------------
    // Passive state — for the HUD's passive display. Resolve the granted passive
    // ability instances off the ASC so UMG can bind without reaching into GAS.
    // -------------------------------------------------------------------------

    /** The Loved and The Lost ramp progress 0..1, or 0 if the passive isn't granted. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Passives")
    float GetLovedAndLostRamp() const;

    /** True while The Loved and The Lost is ramping (Hunter in combat). */
    UFUNCTION(BlueprintPure, Category = "Gothic|Passives")
    bool IsLovedAndLostActive() const;

    /** True while The Read's vital-damage window is up (State.Read) — proc icon. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Passives")
    bool IsReadActive() const;

    /** True while The Reckoning's guaranteed-vital state is up (State.Reckoning) — proc icon. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Passives")
    bool IsReckoningActive() const;

    /** True when the Not At All passive is granted on this character — steady indicator. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Passives")
    bool IsNotAtAllGranted() const;

    /**
     * Steadfast charges the active weapon costs to refill — 1/2/3 by slot tier.
     * Zero when the weapon carries no ammo. Read rather than hardcoded, so
     * retuning an archetype is a data-asset edit.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    int32 GetActiveSteadfastRefillCost() const;

    // -------------------------------------------------------------------------
    // Reload
    //
    // Tap  — pull rounds from reserve into the magazine.
    // Hold — convert Steadfast into reserve ammo. The conversion fires the moment
    //        the hold threshold is reached and repeats on an interval while held.
    //        It is deliberately NOT granted on release: the player must see the
    //        ammo arrive while still holding, or the hold has no readable payoff.
    // -------------------------------------------------------------------------

    /**
     * Tap reload — refills the active weapon's magazine from its reserve.
     * Returns false if there is no reserve, the magazine is already full, or the
     * weapon carries no ammo.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Weapons")
    bool ReloadActiveWeapon();

    /**
     * Hold reload — spends Steadfast to add reserve ammo for the active weapon.
     * Cost scales with the weapon's slot tier (Sidearm 1 / Piece 2 / Rig 3
     * charges). Returns false when Steadfast is short, the reserve is already
     * full, or the weapon carries no ammo.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Weapons")
    bool ConvertSteadfastToReserve();

    /** True while a hold is actively converting Steadfast — drives looping hold VFX/audio. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    bool IsConvertingSteadfast() const { return bSteadfastConversionFired; }

    /** Seconds the reload key must be held before Steadfast conversion begins. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons")
    float SteadfastHoldThreshold = 0.5f;

    /** Seconds between repeat conversions while the key stays held. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons")
    float SteadfastHoldRepeatInterval = 0.6f;

    /**
     * How many refill "charges" a full Steadfast bar represents.
     * Cost per conversion is RefillCost * (MaxSteadfast / this). At the default of
     * 3, a Rig refill (3 charges) empties the bar exactly — which is the design's
     * stated tension, and keeps the cost correct if MaxSteadfast is ever retuned.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons", meta = (ClampMin = 1))
    int32 SteadfastChargesPerFullBar = 3;

    /**
     * Fired after any successful reload — tap, or the automatic one that follows
     * the shot emptying the magazine. Play the reload montage and audio here.
     *
     * Its signature is deliberately unchanged now that it can fire without input:
     * every Blueprint already wired to it keeps working, and the shared feedback
     * (a reload is a reload) stays in one place. Feedback that must ONLY happen
     * on an automatic reload goes in OnAutoReloadPerformed, which fires after
     * this one.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Weapons")
    void OnReloadPerformed();

    /**
     * Fired after OnReloadPerformed when the reload was automatic — the magazine
     * ran dry mid-fire and the weapon's bAutoReloadWhenEmpty topped it back up.
     * Never fires for a player-initiated reload.
     *
     * Use it for the extra beat that only makes sense unprompted: a dry click, a
     * distinct cycling cue, a HUD flash telling the player what just happened to
     * their reserve. Leave it unimplemented and an automatic reload is
     * indistinguishable from a manual one.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Weapons")
    void OnAutoReloadPerformed();

    /**
     * Fired on each Steadfast conversion while the key is held, including the
     * first one at the threshold. RoundsGranted is what actually landed in the
     * reserve after clamping.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Weapons")
    void OnSteadfastConverted(int32 RoundsGranted);

    /**
     * Fired when the hold ends — on release, or when conversion stops because
     * Steadfast ran out or the reserve filled. Use it to stop any looping
     * conversion VFX or audio started by OnSteadfastConverted.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Weapons")
    void OnSteadfastConversionEnded();

    /** Pushes the active weapon's ammo state to the HUD. Safe to call at any time. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Weapons")
    void PushAmmoToHUD() const;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons")
    TArray<FGothicWeaponSlot> WeaponSlots;

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Weapons")
    int32 ActiveWeaponIndex = 0;

    /**
     * Swap to a weapon slot by index. Updates mesh, crosshair, and ammo state.
     * Safe to call with the current index — early-outs if already active.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Weapons")
    void SwapWeapon(int32 NewIndex);

    /** Called after SwapWeapon completes — Blueprint can play swap animations, sounds. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Weapons")
    void OnWeaponSwapped(int32 NewIndex, const UGothicWeaponData* NewWeaponData);

    /**
     * Opens the inventory screen, or closes it if already open.
     * Bound to InventoryToggleAction; also callable from Blueprint (e.g. a UI close button).
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Inventory")
    void ToggleInventory();

    /** True while the inventory screen is on the viewport. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Inventory")
    bool IsInventoryOpen() const { return ActiveInventoryWidget != nullptr; }

    /**
     * Opens the quit menu, or closes it if already open. Bound to QuitMenuAction.
     *
     * The menu itself lives on the HUD (AGothicHUD::ToggleQuitMenu) — this only
     * forwards the keypress, the same way ToggleInventory owns its widget here.
     * It existed fully built and fully unreachable: nothing called
     * ToggleQuitMenu, so a packaged build could not be exited except by killing
     * the process.
     *
     * Escape closes the INVENTORY first if that is open, and only opens the quit
     * menu when nothing else is up. One escape key that backs out of the topmost
     * thing is what players expect; two screens fighting over it is not.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|UI")
    void ToggleQuitMenu();

    /**
     * Consecutive weapon hits with no miss in between. Drives the electrical
     * Rig's Oversurge.
     *
     * Kept on the character rather than the weapon asset because the asset is a
     * shared data object -- a streak stored there would be global to every actor
     * holding that weapon. Reset by a miss and by a weapon swap, so hits banked
     * with one gun cannot be cashed with another.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Combat")
    int32 GetConsecutiveHits() const { return ConsecutiveWeaponHits; }

    UFUNCTION(BlueprintCallable, Category = "Gothic|Combat")
    void RegisterWeaponHit() { ++ConsecutiveWeaponHits; }

    UFUNCTION(BlueprintCallable, Category = "Gothic|Combat")
    void ResetConsecutiveHits() { ConsecutiveWeaponHits = 0; }

protected:
    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    // -------------------------------------------------------------------------
    // Weapon Mesh — attached to camera, swapped on weapon change
    // -------------------------------------------------------------------------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Weapons")
    TObjectPtr<UStaticMeshComponent> WeaponMeshComponent;

    /**
     * Re-parent the weapon and apply the relative transform that matches whichever
     * parent it landed on.
     *
     * Attachment and transform must be set together — a hand-socket offset applied to
     * a camera-parented weapon puts it somewhere arbitrary — so both BeginPlay and
     * RefreshWeaponVisuals route through here rather than each setting their own.
     */
    void ApplyWeaponAttachment(const UGothicWeaponData* WeaponData);

    /** Drive the camera-mounted weapon's sprint pose and fire-kick recovery. Ticked
     *  on the local client only; does nothing when the weapon is hand-socketed. */
    void UpdateFirstPersonWeaponPose(float DeltaTime);

private:
    /** 0..1 blend toward the sprint pose. */
    float SprintPoseAlpha = 0.f;

    /** Live fire kick, decaying toward zero every tick. */
    FVector CurrentFireKickLocation = FVector::ZeroVector;
    FRotator CurrentFireKickRotation = FRotator::ZeroRotator;

protected:

    // -------------------------------------------------------------------------
    // Input Mapping Context
    // -------------------------------------------------------------------------
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    // -------------------------------------------------------------------------
    // Non-ability input actions — assign in BP_GothicPlayerCharacter
    // -------------------------------------------------------------------------
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> FireAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> SprintAction;

    /** Assign IA_ADS in BP_GothicPlayerCharacter. Hold to aim. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> ADSAction;

    // -----------------------------------------------------------------
    // First-person camera anchoring
    // -----------------------------------------------------------------

    /**
     * Bone the first-person camera rides. NAME_None keeps it on the mesh
     * component, which is the original behaviour.
     *
     * WHY: the weapon is attached to a hand socket, so it inherits every bit of
     * body animation. The camera was attached to the mesh COMPONENT, which does
     * not move with animation at all. That difference is the weapon sway — the
     * gun bobs with the pelvis while the view stays perfectly still, and the eye
     * reads the relative motion.
     *
     * Anchoring the camera to a bone in the same chain as the weapon makes both
     * move together, so the gun sits still in frame and the bob shows up as head
     * movement instead. spine_05 rather than head: the aim layer already holds
     * everything from spine_01 up rigid, so spine_05 moves exactly as the pelvis
     * does — identically to the weapon — while head can carry extra animation.
     *
     * Only POSITION is inherited: the camera has bUsePawnControlRotation, so the
     * bone's rotation never reaches the view.
     *
     * TO REVERT: clear this to None in BP_GothicPlayerCharacter. No rebuild.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Camera")
    FName CameraAttachBoneName = TEXT("spine_05");

    /**
     * Extra offset applied after anchoring, in bone space.
     *
     * Zero by default and deliberately so: the attach preserves the camera's
     * existing world position, so switching anchors does not move the eye and the
     * only thing that changes is what it follows. Use this to nudge afterwards.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Camera")
    FVector CameraBoneOffsetAdjust = FVector::ZeroVector;

    /** Press 1 for Sidearm, 2 for Piece, 3 for Rig. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> WeaponSlot1Action;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> WeaponSlot2Action;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> WeaponSlot3Action;

    /** Assign IA_InventoryToggle in BP_GothicPlayerCharacter. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> InventoryToggleAction;

    /** Assign IA_QuitMenu (Escape) in BP_GothicPlayerCharacter. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> QuitMenuAction;

    /**
     * Assign IA_Reload in BP_GothicPlayerCharacter.
     * Bound directly rather than through the ability tag pipeline — reload has no
     * cost, cooldown, or tags, and the hold needs both press and release on the
     * pawn. Sprint is bound the same way for the same reason.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> ReloadAction;

    /** Assign IA_Interact in BP_GothicPlayerCharacter. Collects the shared Selah
     *  prompt when the player is near the fallen prompt-corpse. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> InteractAction;

    // -------------------------------------------------------------------------
    // Inventory UI
    // -------------------------------------------------------------------------
    /** Assign WBP_Inventory in BP_GothicPlayerCharacter. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Inventory")
    TSubclassOf<UGothicInventoryWidget> InventoryWidgetClass;

    // -------------------------------------------------------------------------
    // Input Handler Component
    // Lyra pattern: ability input binding separated from the character class
    // Assign InputConfig on this component in BP_GothicPlayerCharacter
    // -------------------------------------------------------------------------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UGothicInputHandlerComponent> InputHandler;

    // -------------------------------------------------------------------------
    // Ability Sets — data driven
    // Lyra pattern: ability configuration lives in data assets not C++ arrays
    // Assign DA_HunterAbilitySet here in BP_GothicPlayerCharacter
    // -------------------------------------------------------------------------
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Abilities")
    TArray<TObjectPtr<UGothicAbilitySet>> StartupAbilitySets;

    // Combat damage values used to live here (DamageEffectClass, PistolDamage,
    // MeleeDamage) to serve OnFire() and OnMelee(). Both of those are gone, and
    // these had no readers left — a tuning trap, since editing them on the
    // Blueprint would have silently done nothing. Damage now belongs to the
    // abilities: GA_Fire reads UGothicWeaponData, GA_HuntersStrike and GA_Slicer
    // carry their own DamageEffectClass.

    /** Override in Blueprint to handle visual and audio of the Selah moment. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
    void OnSelahMoment();

    // -------------------------------------------------------------------------
    // Movement — sprint
    // -------------------------------------------------------------------------
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
    float WalkSpeed = 500.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
    float SprintSpeed = 800.f;

    // -------------------------------------------------------------------------
    // Input handlers — direct non-ability bindings
    // -------------------------------------------------------------------------
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void OnSprintStarted();
    void OnSprintStopped();

    /** IA_Interact — if a Selah prompt corpse is in range, ask the server to collect it. */
    void OnInteract();

    /** Server-side collect for a specific encounter, re-validated server-side. */
    UFUNCTION(Server, Reliable)
    void ServerCollectEncounterSelah(AGothicEncounterVolume* Enc);

    /** Per-frame (locally controlled): show/clear the "[E] Meditate" prompt. */
    void UpdateSelahInteractPrompt();

    // SelahInteractRange (400uu) lived here and was read by nothing — collection
    // has always used the encounter's own MeditationRange. Removed rather than
    // left as an EditDefaultsOnly knob that silently does nothing when tuned.

    /** The encounter the local interact prompt is currently raised for (to clear it correctly). */
    TWeakObjectPtr<AActor> ShownSelahPromptCorpse;

public:
    /**
     * Recompute MaxWalkSpeed from the sprint state plus the MovementSpeed
     * secondary stat. Call after anything that can change gear, since attribute
     * changes do not push into CharacterMovement on their own.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Movement")
    void RefreshMovementSpeed();

protected:
    /** True while the sprint input is held. Drives RefreshMovementSpeed's base. */
    bool bIsSprinting = false;

    void OnWeaponSlot1();
    void OnWeaponSlot2();
    void OnWeaponSlot3();

    // -------------------------------------------------------------------------
    // GAS init
    // -------------------------------------------------------------------------
    void InitGASFromPlayerState();

    /**
     * Called when the inventory equips or unequips an item.
     * If the item is a weapon, updates the matching WeaponSlot.
     */
    UFUNCTION()
    void OnEquipmentChanged(EGothicEquipSlot Slot, const FGothicItemInstance& Item);

    /**
     * Maps an equip slot enum to a weapon slot index.
     * Returns -1 if the slot is not a weapon slot.
     */
    static int32 EquipSlotToWeaponIndex(EGothicEquipSlot Slot);

    /** Refreshes the weapon mesh and crosshair if the given index is active. */
    void RefreshWeaponVisuals(int32 SlotIndex);

private:
    bool bHUDReady = false;
    bool bAbilitiesGranted = false;
    bool bInventoryBound = false;

    // -------------------------------------------------------------------------
    // HUD attribute delegates — lifetime
    //
    // The player's ASC lives on the PlayerState, so it OUTLIVES this pawn: a
    // respawn destroys the pawn and hands the same ASC to a new one. Three HUD
    // lambdas used to be bound to that ASC's attribute-change delegates
    // capturing `this` (the pawn), with no handles kept and nothing ever
    // unbinding them. After the dead pawn was collected, the next attribute
    // change on the surviving ASC called into freed memory — an access
    // violation inside the health lambda, reproduced twice in PIE.
    //
    // Worse, InitGASFromPlayerState runs TWICE per pawn (PossessedBy and
    // OnRep_PlayerState both call it — the doubled "InputComponent not yet
    // available" warning in the log is the same double-entry), so each life
    // registered two copies of every lambda and left two dangling.
    //
    // Both halves are closed by the same pair of functions: bind is
    // remove-then-add so it is idempotent however many times init runs, and
    // EndPlay removes them so nothing survives the pawn. Handles + explicit
    // Remove is the pattern already used for ASC delegates elsewhere in this
    // codebase — see AGothicBossAIController_BestialLucid::OnUnPossess.
    // -------------------------------------------------------------------------

    /** Idempotent: unbinds first, so repeat calls cannot stack registrations. */
    void BindHUDAttributeDelegates();

    /** Removes every HUD attribute delegate from the ASC they were bound to. */
    void UnbindHUDAttributeDelegates();

    /**
     * The ASC the handles below belong to. Weak because it lives on the
     * PlayerState and can legitimately outlive — or predecease — this pawn, and
     * removing a handle from the wrong ASC is a silent no-op that would leave
     * the real one dangling.
     */
    TWeakObjectPtr<UGothicAbilitySystemComponent> BoundHUDAttributeASC;

    FDelegateHandle HealthChangedHandle;
    FDelegateHandle SelahChangedHandle;
    FDelegateHandle SuperMeterChangedHandle;

    // -------------------------------------------------------------------------
    // Stun — making State.Stunned real for the player
    //
    // The boss's Roar and Cry land GE_Stun_BestialLucid on the PlayerState ASC,
    // and until now that tag only sat in the ActivationBlockedTags of GA_Fire
    // and GA_HuntersStrike — a "stunned" player kept running, sprinting and
    // repositioning freely, so the stun read as broken. This mirrors
    // AGothicEnemyBase::HandleStunTagChanged for a player-controlled pawn:
    // movement mode is cut on the server, move INPUT is ignored on the
    // controller, and look input is deliberately left alone — a stunned player
    // can still watch the boss wind up, they just cannot leave.
    //
    // Same lifetime rules as the HUD attribute delegates above: the ASC lives
    // on the PlayerState and outlives the pawn, InitGASFromPlayerState runs
    // twice per pawn, so bind is remove-then-add and EndPlay unbinds.
    // -------------------------------------------------------------------------

    /** Idempotent: unbinds first, so repeat init calls cannot stack registrations. */
    void BindStunTagListener();

    /** Removes the stun listener and releases any move-input ignore it applied. */
    void UnbindStunTagListener();

    /** State.Stunned was added to or fully removed from the ASC. */
    void HandleStunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

    /** Releases the move-input ignore on whichever controller received it. */
    void ClearStunMoveInputIgnore();

    /** The ASC the stun handle belongs to — weak for the same reason as
     *  BoundHUDAttributeASC: it can outlive or predecease this pawn. */
    TWeakObjectPtr<UGothicAbilitySystemComponent> BoundStunTagASC;

    FDelegateHandle StunTagChangedHandle;

    /**
     * The controller whose move input the stun suppressed. SetIgnoreMoveInput
     * is ref-counted and the CONTROLLER survives a respawn — a pawn that dies
     * stunned and never pays the decrement back would leave the next pawn
     * permanently unable to move. Tracked so the release always goes to the
     * controller that took the ignore, even across an unpossess.
     */
    TWeakObjectPtr<AController> StunMoveIgnoredController;

    /** See GetConsecutiveHits. Transient — a streak should not survive a respawn. */
    UPROPERTY(Transient)
    int32 ConsecutiveWeaponHits = 0;

    /** Resolve the granted Loved-and-the-Lost passive instance, or null. */
    const UGA_TheLovedAndTheLost* FindLovedAndLost() const;

    // -------------------------------------------------------------------------
    // Reload hold state
    // -------------------------------------------------------------------------

    /** Reload key went down — starts the hold timer. */
    void OnReloadPressed();

    /** Reload key came up — tap-reloads if the threshold was never reached. */
    void OnReloadReleased();

    /** Threshold reached, or a repeat interval elapsed, while the key is still held. */
    void HandleSteadfastHoldTick();

    /** Stops the hold timer and fires OnSteadfastConversionEnded if it had begun. */
    void EndSteadfastHold();

    /** Drives both the initial threshold delay and the repeat interval. */
    FTimerHandle SteadfastHoldTimerHandle;

    /** Set by TriggerSelahMoment, cleared by EndSelahMomentLock or its timer.
     *  Local only — the moment is a per-player beat, not shared world state. */
    bool bSelahMomentLock = false;

    FTimerHandle SelahMomentLockHandle;

    // ── Aim down sights ──────────────────────────────────────────────────
    void OnADSPressed();
    void OnADSReleased();

    /** Applies aim state locally, then tells the server. */
    void SetAiming(bool bNewAiming);

    /** The server needs the aim state for GA_Fire's spread; the owning client sets
     *  it locally first so the FOV and speed change on the same frame as the input
     *  rather than a round trip later. */
    UFUNCTION(Server, Reliable)
    void ServerSetAiming(bool bNewAiming);

    /** Resting FOV, captured from the camera in BeginPlay so the Blueprint's value
     *  is the source of truth rather than a duplicated constant here. */
    float HipFieldOfView = 90.f;

    /** Re-parents FirstPersonCamera onto CameraAttachBoneName. No-op if the name
     *  is None or the bone does not exist. */
    void AnchorCameraToBone();

    /**
     * Runs the automatic reload triggered by ConsumeRound emptying the magazine.
     * Single attempt, no Steadfast, and a no-op if one is already in flight.
     */
    void TryAutoReload();

    /**
     * True for the duration of an automatic reload. ReloadActiveWeapon calls into
     * Blueprint via OnReloadPerformed, and Blueprint can fire the weapon again
     * from there; without this the second shot's ConsumeRound would start another
     * auto-reload inside the first.
     */
    bool bAutoReloadInProgress = false;

    /** True while conversions are actually running — gates OnSteadfastConversionEnded. */
    bool bSteadfastConversionFired = false;

    /**
     * True once the key was held past the threshold, and only cleared on the next
     * press. Suppresses the tap reload on release even when the hold converted
     * nothing (no Steadfast, full reserve) or was abandoned by a weapon swap —
     * a hold is a hold, and must never pay out as a tap.
     */
    bool bSteadfastHoldThresholdReached = false;

    /** The live inventory screen. Null while closed — doubles as the open/closed flag. */
    UPROPERTY()
    TObjectPtr<UGothicInventoryWidget> ActiveInventoryWidget;
};