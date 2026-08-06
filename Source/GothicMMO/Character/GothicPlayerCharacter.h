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
class UGothicHintManagerComponent;
class UGothicAbilitySet;
class UGothicInventoryWidget;
class UGA_TheLovedAndTheLost;
class AGothicPlayerState;
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

    // -------------------------------------------------------------------------
    // Downed fork (multiplayer)
    //
    // Zero health with at least one other player still up puts you DOWN instead
    // of dead: health pinned at 1, abilities cancelled, movement off, and the
    // SAME PAWN kept. Solo — or as the last one standing — nothing changes and
    // the existing death path runs untouched.
    //
    // The fork lives at the top of OnDeath_Implementation, ahead of Super,
    // because everything irreversible for this purpose is inside Super:
    // State.Dead (which blocks every ability's activation), CancelAllAbilities,
    // capsule collision off, DisableMovement. A downed player needs the pawn to
    // still be a pawn, so the branch has to happen before any of that.
    //
    // Nothing here touches the enemy side. AGothicCharacterBase::IsFightableActor
    // already asks the PlayerState whether this player is downed, so the moment
    // SetDowned(true) lands the AI stops targeting the body on its own cadence.
    // -------------------------------------------------------------------------

    /**
     * How long a downed player has before the revive window expires and the
     * normal death path runs. Tunable per-Blueprint.
     *
     * EditDefaultsOnly, so a placed player pawn (if any level ever places one
     * rather than spawning it from a PlayerStart) freezes this at its placement
     * value and must be re-placed to pick up a new default.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Downed",
              meta = (ClampMin = "1.0"))
    float ReviveWindowSeconds = 30.f;

    /**
     * Fraction of MaxHealth a revived player stands up on. A revive is not a
     * respawn — GE_InitStats_Player never runs — so this is the only thing that
     * lifts them off the 1 HP the downed state pinned them at, and a full heal
     * would make going down a free reset. PR-3 owns the real tuning.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Downed",
              meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float ReviveHealthFraction = 0.5f;

    /**
     * True if some OTHER player is alive and not downed — i.e. somebody is left
     * who could come and pick this one up. Meaningful on the authority only; it
     * walks the game state's PlayerArray, which is the closest thing this project
     * has to a party roster.
     *
     * PR-5 TOOK THE SEAM. The walk moved to AGothicGameState::
     * HasFightablePartyMember, which is the same roster the party-wipe census
     * uses; this is now a one-line forward to it. The two questions — "is anyone
     * left to pick me up" and "has the party wiped" — are answers to the same
     * census by construction and can no longer drift apart.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Downed")
    bool HasLivingPartyMember() const;

    /**
     * Server-only. Ends the revive window NOW and drops this player through to
     * the real death — the party wipe's route from downed to dead.
     *
     * Goes through the SAME OnReviveWindowExpired the timer drives rather than
     * duplicating it, so a wipe-killed player is byte-for-byte a bled-out player:
     * the State.Downed hygiene, the OnDeath banking, and the RequestRespawn at
     * the tail are all inherited. No-op if not downed.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Downed")
    void CollapseReviveWindow();

    /**
     * Server-only. Brings a downed player back IN PLACE — no new pawn, no
     * respawn. Clears the downed state, restores health, re-enables movement and
     * re-runs the ability-set grant so the passives that CancelAllAbilities shut
     * down come back (see the body: bAbilitiesGranted's successor latch would
     * otherwise skip the whole grant, because this is the same pawn holding the
     * same ASC it was already granted into).
     *
     * PR-3's channeled revive is the intended caller. Exposed to Blueprint and to
     * the Gothic.Revive console command now so the state is testable before that
     * ability exists.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Downed")
    void ReviveFromDowned();

    /**
     * Fired on the authority when this player goes down, and again when they are
     * revived. Blueprint hangs the downed camera/vignette/animation off this; the
     * replicated half of the same signal is AGothicPlayerState::OnDownedChanged.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Downed")
    void OnDownedStateChanged(bool bNowDowned);

    // -------------------------------------------------------------------------
    // Channelled revive (PR-3)
    //
    // A living player holds the interact key over a downed teammate for
    // ReviveChannelSeconds and they stand back up through ReviveFromDowned above —
    // the same function Gothic.Revive already drives, so everything that path
    // fixed (the passive re-grant most of all) is inherited rather than repeated.
    //
    // The channel is SERVER-AUTHORITATIVE end to end. The client sends two edges
    // (press, release) exactly as the reload hold does and nothing else; the clock,
    // every cancellation test and the revive itself run on the authority, and the
    // only thing that comes back is the replicated fill on the downed player's
    // PlayerState. A client that lies about holding the key gets a channel that
    // still has to survive the server's own range, movement and damage checks.
    //
    // The downed body is discovered by proximity, not by a trace. That matches how
    // the Selah prompt already works, and the capsule collision EnterDownedState
    // deliberately keeps is what makes the body a solid thing to stand on rather
    // than what makes it findable.
    // -------------------------------------------------------------------------

    /** How long the hold lasts. The single tuning knob for how much a pickup costs. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Downed",
              meta = (ClampMin = "0.2"))
    float ReviveChannelSeconds = 4.f;

    /**
     * How close the reviver must be to the body, both to see the prompt and to
     * keep the channel alive. Deliberately much tighter than the Selah prompt's
     * 1200uu meditation range — that one is an area you stand in, this one is a
     * body you stand over.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Downed",
              meta = (ClampMin = "50.0"))
    float ReviveChannelRange = 250.f;

    /**
     * How far the reviver may drift from where they started before the channel
     * breaks. This is the "interrupted by moving" rule, and it is a DISPLACEMENT
     * test rather than an input or velocity test on purpose: input can be held
     * against a wall, and a player knocked back by an attack has given no input at
     * all but has very much left the body. Small enough that a step off breaks it,
     * large enough that settling on uneven ground does not.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Downed",
              meta = (ClampMin = "0.0"))
    float ReviveChannelMoveTolerance = 100.f;

    /** Nearest downed teammate inside ReviveChannelRange, or null. Locally callable
     *  (for the prompt) and re-run server-side (for the channel) — never trusted
     *  across the wire. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Downed")
    AGothicPlayerCharacter* FindReviveTargetInRange() const;

    /**
     * Server-only. Opens a channel on Target. Refuses if either party is in the
     * wrong state, if Target is out of range, or if somebody is ALREADY channelling
     * on Target.
     *
     * That last refusal is where "a second reviver joining an in-progress channel"
     * is ruled out — deliberately out of scope for PR-3. The slot on the downed
     * player's PlayerState holds one reviver, so co-op reviving would need that
     * slot to become a set and the fill rate to become a function of its size.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Downed")
    void StartReviveChannel(AGothicPlayerCharacter* Target);

    /** Server-only. Breaks the channel this pawn is running, if any. Reason appears
     *  in the VigilTimeline line and nowhere else. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Downed")
    void CancelReviveChannel(const FString& Reason);

    /** True while this pawn is the one doing the channelling. Authority-side truth;
     *  clients read AGothicPlayerState::IsReviveChannelActive instead. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Downed")
    bool IsChannelingRevive() const { return ReviveChannelTargetPawn.IsValid(); }

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

    /**
     * Pause after the last shot before the muzzle starts coming back down.
     *
     * Short on purpose: it exists so sustained fire climbs instead of fighting its
     * own recovery mid-burst, not to make the gun feel sticky. Every shot restamps
     * it, so recovery only begins once the player stops firing.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons",
              meta = (ClampMin = "0.0"))
    float RecoilRecoveryDelay = 0.08f;

    /**
     * How fast the muzzle returns, as an exponential rate on the outstanding kick.
     * 12.0 walks ~95% of a kick back in roughly 0.25s, fast off the top and easing
     * into the resting position — the gun settling rather than the camera panning.
     *
     * 0.0 reproduces the pre-fix behaviour: recoil is applied and never recovered.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons",
              meta = (ClampMin = "0.0"))
    float RecoilRecoveryRate = 12.f;

    /** Returns the active weapon's data asset for stat lookups. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    const UGothicWeaponData* GetActiveWeaponData() const;

    /**
     * Gear Power of the item in the active weapon slot, or 0 when the slot was
     * filled from the Blueprint default loadout rather than a rolled drop.
     * GetActiveWeaponTierMultiplier() is what damage reads; this is the raw
     * number behind it.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    int32 GetActiveGearPower() const;

    /**
     * The active weapon's own damage multiplier — its instance Gear Power over
     * the Tier-1 baseline, so x1 at Tier 1 through x5 at Tier 5
     * (WEAPON_ARCHETYPES.md, DECIDED 2026-08-04). A slot with no rolled copy
     * behind it floors at 1.0 rather than scaling to zero. GA_Fire multiplies
     * the weapon's authored Damage by this; a tooltip can show it as-is.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    float GetActiveWeaponTierMultiplier() const;

    /**
     * Perks rolled onto the copy in the active weapon slot
     * (WEAPON_PERK_TABLES.md). Empty for a mundane drop, for a Blueprint default
     * loadout, and for every weapon until DA_WeaponPerkCatalog is authored.
     *
     * This and HasWeaponPerk() are the only route a perk effect should ever take
     * to the equipped copy — GA_Fire, the reload path and the Steadfast path all
     * already have the character in hand, and none of them should be reaching
     * into WeaponSlots themselves.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    const TArray<FGameplayTag>& GetActiveWeaponPerks() const;

    /** True if the active weapon's copy rolled Perk. False on an empty or perkless slot. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    bool HasWeaponPerk(FGameplayTag Perk) const;

    /**
     * Seconds the pawn has been holding still, or 0 while it is moving.
     *
     * Steady Read asks for "no movement input in the last 0.5s"; this measures
     * VELOCITY instead, which is the honest proxy — input can be held against a
     * wall, and a player being knocked around has given no input at all. Tracked
     * in Tick off the movement component's horizontal speed.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    float GetStationaryDuration() const;

    /**
     * True while the pawn is moving under its own power at a real pace — the
     * "sprinting/strafing" state Moving Target pays out on. Deliberately a speed
     * test rather than a sprint-flag test: the project has no sprint tag, and
     * strafing is in the perk's own wording, so any committed ground movement
     * counts.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Weapons")
    bool IsMovingUnderOwnPower() const;

    /**
     * Puts rounds back into the active magazine without touching the reserve —
     * Marksman's Due's refund. Clamped to the effective magazine cap, so a full
     * magazine silently absorbs nothing rather than overfilling.
     *
     * Returns the number actually loaded (0 when the weapon carries no ammo or
     * the magazine is already full). Pushes the HUD when it grants anything.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Weapons")
    int32 AddRoundsToMagazine(int32 Rounds = 1);

    /**
     * Overall Gear Power (average across equipped gear). No longer touches
     * damage — weapon damage now reads the weapon's own tier, and armor tier
     * expresses through Gear Score → Attack Power. Kept as the activity-gating
     * hook it was always also meant to be. Delegates to the inventory.
     */
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

    // ── Ammo persistence across pawns ────────────────────────────────────────
    // WeaponSlots are pawn state and the pawn does not survive death, so a
    // respawned player used to come back on full magazines: the init path runs
    // FGothicWeaponSlot::InitFromData, which refills. The bank lives on
    // AGothicPlayerState (see CacheAmmo there for why, and for how the downed
    // fork and level travel both land on this same restore).

    /** RAW per-slot counts for every slot holding an ammo-using weapon. */
    TArray<FGothicAmmoSlotSnapshot> CaptureAmmoSnapshot() const;

    /**
     * Writes a banked snapshot back into WeaponSlots, clamped to each slot's
     * EFFECTIVE capacities — the perks that move those ceilings (Deep Reserves,
     * Extended Magazine) live on the equipped copy, and the copy in the slot at
     * restore time is the one whose limits apply, not the one that was banked.
     * Slots that are empty, ammo-less, or absent from the snapshot are untouched.
     */
    void ApplyAmmoSnapshot(const TArray<FGothicAmmoSlotSnapshot>& Snapshot);

    /**
     * Owner-only catch-up for the restore, which happens on the server.
     *
     * A remote client's WeaponSlots are computed on its own machine (the array is
     * not replicated) and its own InitGASFromPlayerState refills them, so pushing
     * only the HUD numbers would leave the client's slots and its display
     * disagreeing the moment it fired. This writes the slots and then draws.
     *
     * Stashed as well as applied: the client's pawn may not have run its own init
     * pass when this arrives, and that pass would refill over the top — so the
     * tail of InitGASFromPlayerState replays whatever is pending. The stash is
     * pawn state and dies with the pawn, which is the whole point; nothing has to
     * clear it on respawn.
     */
    UFUNCTION(Client, Reliable)
    void Client_RestoreAmmo(const TArray<FGothicAmmoSlotSnapshot>& Snapshot);

    /**
     * The tutorial hint latch. Always valid — created as a default subobject.
     * Blueprint and level actors reach hints through this rather than through
     * the HUD, so the seen-set is consulted before anything is drawn.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Hints")
    UGothicHintManagerComponent* GetHintManager() const { return HintManager; }

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons")
    TArray<FGothicWeaponSlot> WeaponSlots;

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Weapons")
    int32 ActiveWeaponIndex = 0;

    /**
     * Gear Power one Tier-1 weapon carries — UGothicItemDefinition::GetGearPower()
     * is GearTier * 100, so this is the divisor that turns Gear Power back into a
     * tier multiplier. Not a tuning knob: change it only alongside GetGearPower().
     */
    static constexpr float WeaponTierBaselineGearPower = 100.f;

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

    /** Tutorial hints. See GetHintManager and the component's own header. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Hints")
    TObjectPtr<UGothicHintManagerComponent> HintManager;

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

    /**
     * Sprinting costs you the gun.
     *
     * With this on, sprinting carries State.Sprinting on the ASC and the gun goes
     * dead: fire is blocked by GA_Fire's ActivationBlockedTags, and reload,
     * Steadfast conversion and weapon swap early-out. Every OTHER ability cancels
     * the sprint and then runs.
     *
     * Off restores the pre-existing behaviour exactly — sprint changes speed and
     * the weapon pose and nothing else — so this is the one checkbox to flip if
     * the trade turns out to feel wrong in playtest.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
    bool bSprintBlocksGunActions = true;

    // -------------------------------------------------------------------------
    // Input handlers — direct non-ability bindings
    // -------------------------------------------------------------------------
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void OnSprintStarted();
    void OnSprintStopped();

    /**
     * IA_Interact pressed. A downed teammate in range takes priority over the Selah
     * prompt and starts a revive channel; otherwise this falls through to the
     * collect exactly as it always did.
     */
    void OnInteract();

    /** IA_Interact released — ends any revive channel. A Selah collect is a single
     *  press and ignores this edge entirely. */
    void OnInteractReleased();

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

    // ── Channelled revive internals ──────────────────────────────────────────

    /** Client → server: begin channelling on Target. Every guard is re-run there. */
    UFUNCTION(Server, Reliable)
    void ServerStartReviveChannel(AGothicPlayerCharacter* Target);

    /** Client → server: the key came up. */
    UFUNCTION(Server, Reliable)
    void ServerEndReviveChannel();

    /**
     * The authority's channel clock. A repeating 0.1s timer rather than Tick: the
     * fill replicates on every pass, and forty updates over a four-second channel
     * is plenty for a bar while a per-frame push would be ~240 for the same result.
     */
    void TickReviveChannel();

    /** Ends the channel and stands the target back up. Authority only. */
    void CompleteReviveChannel();

    /** Local: raise/clear the "[E] Revive <name>" prompt. Runs before the Selah
     *  prompt update, which yields to it. */
    void UpdateRevivePrompt();

    /** Local: mirror the replicated channel state onto THIS viewer's HUD. */
    void UpdateReviveChannelHUD();

    /** Who we are channelling on, or null. Doubles as the "am I channelling" flag. */
    TWeakObjectPtr<AGothicPlayerCharacter> ReviveChannelTargetPawn;

    FTimerHandle ReviveChannelTimer;

    /** Seconds of channel banked so far. */
    float ReviveChannelElapsed = 0.f;

    /** Where the reviver stood when the channel opened — the origin of the
     *  displacement test. See ReviveChannelMoveTolerance. */
    FVector ReviveChannelAnchor = FVector::ZeroVector;

    /**
     * The reviver's health as of the previous channel tick. Any DECREASE breaks
     * the channel.
     *
     * A poll rather than an attribute-change delegate, and that is a real trade:
     * it costs up to one tick interval of latency on the break, and it buys not
     * having to bind a delegate to an ASC that lives on the PlayerState and
     * outlives this pawn — the lifetime hazard BindHUDAttributeDelegates and
     * BindStunTagListener both exist to manage. The channel already has an
     * authoritative timer running; hanging one comparison off it has no lifetime
     * of its own to get wrong.
     */
    float ReviveChannelLastHealth = 0.f;

    /** Local: the downed pawn the interact prompt is currently raised for. */
    TWeakObjectPtr<AActor> ShownRevivePromptPawn;

    /** Local: the PlayerState whose channel this viewer's HUD is currently showing.
     *  Held so the END can still read the result off it after the reviver's own
     *  back-pointer has been cleared. */
    TWeakObjectPtr<AGothicPlayerState> ShownReviveChannelSubject;

    /** See TickReviveChannel. */
    static constexpr float ReviveChannelTickInterval = 0.1f;

public:
    /**
     * Recompute MaxWalkSpeed from the sprint state plus the MovementSpeed
     * secondary stat. Call after anything that can change gear, since attribute
     * changes do not push into CharacterMovement on their own.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Movement")
    void RefreshMovementSpeed();

    /**
     * THE way the sprint flag moves. Every start and every end path goes through
     * here so State.Sprinting cannot drift out of step with bIsSprinting — which
     * matters more than it looks, because the ASC lives on the PlayerState and
     * OUTLIVES the pawn: a tag left set at death would follow the player into
     * their respawn and leave them unable to fire, the same shape as the
     * State.Dead bug already fixed in InitGASFromPlayerState.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Movement")
    void SetSprinting(bool bNewSprinting);

    /** Convenience end-path for callers that only ever stop a sprint (abilities). */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Movement")
    void StopSprinting() { SetSprinting(false); }

    /**
     * True when gun actions should be refused right now: sprinting, with the
     * opportunity cost switched on. Fire is gated separately by GA_Fire's
     * ActivationBlockedTags — this is for the gun actions that are plain C++
     * input handlers and never touch GAS (reload, Steadfast, swap).
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Movement")
    bool AreGunActionsBlocked() const { return bSprintBlocksGunActions && bIsSprinting; }

    /**
     * Called from the ability input choke point when a non-gun ability is about
     * to activate: the sprint ends first, then the ability runs. No-op when the
     * opportunity cost is switched off.
     */
    void CancelSprintForAbility();

protected:
    /** True while the sprint input is held. Drives RefreshMovementSpeed's base. */
    bool bIsSprinting = false;

    /** Push bIsSprinting into the ASC as the State.Sprinting loose tag.
     *  SetLooseGameplayTagCount, not Add/Remove: loose tags are ref-counted, and
     *  an absolute count is the only form that is safe to call repeatedly. */
    void SyncSprintTag();

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
    bool bInventoryBound = false;

    /** Owning-client only. See Client_RestoreAmmo for why this is kept rather
     *  than applied once, and why nothing clears it. */
    UPROPERTY(Transient)
    TArray<FGothicAmmoSlotSnapshot> PendingClientAmmoRestore;

    // -------------------------------------------------------------------------
    // Which ASC the ability sets actually went into.
    //
    // This replaces a plain bAbilitiesGranted bool, and the difference is the
    // whole remote-player grant bug. InitGASFromPlayerState re-reads
    // AbilitySystemComponent from the PlayerState on EVERY pass, so the pointer
    // it grants into is not guaranteed to be the pointer the pawn ends up with —
    // a joining client's PlayerState resolves late, and the pass that ran first
    // is not necessarily the pass that saw the final ASC. A bool cannot tell
    // those apart: it records THAT a grant happened, not WHERE it landed, so the
    // first pass closed the door and the ASC the pawn kept stayed empty.
    //
    // Weak, because the ASC lives on the PlayerState and outlives nothing in
    // particular — a stale raw pointer here would compare equal to freed memory.
    // Null (or pointing at a different ASC than the current one) means "this ASC
    // has not been granted into yet", which is exactly the respawn case: a new
    // pawn starts with this empty and re-grants into the surviving PlayerState
    // ASC, where UGothicAbilitySet's own idempotency guard prevents duplicates
    // and re-activates the passives OnDeath cancelled. That flow is unchanged.
    // -------------------------------------------------------------------------
    TWeakObjectPtr<UGothicAbilitySystemComponent> AbilitiesGrantedIntoASC;

    // -------------------------------------------------------------------------
    // GAS init retry — authority only
    //
    // InitGASFromPlayerState has exactly two drivers: PossessedBy and
    // OnRep_PlayerState. The second one never fires on the server, so on the
    // AUTHORITY the function gets one attempt and one only. If the PlayerState
    // has not resolved at that instant the pawn is left permanently ungranted,
    // and the host cannot reproduce it — its pawn is possessed long after its
    // own PlayerState exists. This timer gives the authority the retry that
    // replication gives a client for free.
    // -------------------------------------------------------------------------

    /** Re-arms InitGASFromPlayerState on the authority when the PlayerState is not ready yet. */
    void ScheduleGASInitRetry();

    /** Emits the GASInit VigilTimeline line. Const — it reports, it does not initialize. */
    void LogGASInitComplete() const;

    FTimerHandle GASInitRetryTimer;

    // ── Downed fork internals ────────────────────────────────────────────────

    /** Authority-only. Pins health at 1, sets the replicated downed flag, cancels
     *  abilities, stops movement and arms ReviveWindowTimer. Does NOT call Super
     *  — see the comment on ReviveWindowSeconds for why that ordering is load-bearing. */
    void EnterDownedState(AActor* Killer);

    /** ReviveWindowTimer's callback: nobody came, so fall through to the real
     *  death path with the killer we banked when we went down. */
    void OnReviveWindowExpired();

    /**
     * Authority-only. Asks the game mode to re-run the party census.
     *
     * Called from every transition that can change whether the party is up:
     * going down, being revived, and dying. The game mode's evaluation is
     * idempotent and cheap, so this is deliberately over-called rather than
     * carefully placed — a missed call leaves a party stuck in a state nothing
     * can move it out of.
     */
    void NotifyPartyStateChanged();

    FTimerHandle ReviveWindowTimer;

    /** Whoever put us down, banked so the eventual death is still attributed to
     *  them. Weak — the killer can easily be dead itself by the time the window
     *  runs out. */
    TWeakObjectPtr<AActor> DownedKiller;

    /** Set for the single OnDeath call that the expiring window drives, so the
     *  fork does not put the same player straight back down. Cleared in
     *  EnterDownedState so a later life can go down normally. */
    bool bReviveWindowExpiring = false;
    int32 GASInitRetryCount = 0;

    /** Roughly a couple of frames — long enough to let possession settle, short enough to be invisible. */
    static constexpr float GASInitRetryInterval = 0.05f;

    /** ~1s of attempts. Past that it is not a race, it is a broken PlayerState class. */
    static constexpr int32 MaxGASInitRetries = 20;

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
    FDelegateHandle SteadfastChangedHandle;

    /**
     * Latches so the "resource is full" hints fire on the EDGE, not on every
     * write at the ceiling. Steadfast and SuperMeter both sit pinned at max for
     * long stretches, and the attribute delegate fires on every clamped write.
     * The hint manager's own seen-set would swallow the repeats, but the log
     * line and the HUD churn would not.
     */
    bool bSteadfastWasFull = false;
    bool bSuperMeterWasFull = false;

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

    /**
     * World time of the last frame this pawn was moving, for
     * GetStationaryDuration (Steady Read). Negative until the first Tick so a
     * pawn that has never moved still reads as stationary rather than as having
     * stood still since world time zero.
     */
    UPROPERTY(Transient)
    float LastMovingWorldTime = -1.f;

    /**
     * Horizontal speed (cm/s) at or above which the pawn counts as moving, for
     * both Steady Read and Moving Target. Well above the residual drift a
     * standing character reports on uneven ground, well below a walk.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Weapons", meta = (ClampMin = "0.0"))
    float PerkMovementSpeedThreshold = 20.f;

    /** Resolve the granted Loved-and-the-Lost passive instance, or null. */
    const UGA_TheLovedAndTheLost* FindLovedAndLost() const;

    // -------------------------------------------------------------------------
    // Recoil recovery
    // -------------------------------------------------------------------------

    /**
     * Pitch kick applied by ApplyRecoilKick and not yet returned — in the same
     * units ApplyPitchInput takes, so recovery goes back out through the same call
     * and picks up whatever scaling the kick did. Negative, matching RecoilPitch.
     *
     * Reduced by the player's own downward look input, so pulling the gun back on
     * target does not then get double-corrected by the automatic recovery.
     */
    float PendingRecoilPitch = 0.f;

    /** World time of the most recent kick, for RecoilRecoveryDelay. */
    float LastRecoilKickTime = -1.f;

    /** Walks PendingRecoilPitch back to zero. Local-only; called from Tick. */
    void TickRecoilRecovery(float DeltaTime);

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