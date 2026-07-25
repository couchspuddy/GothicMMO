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
     * Gear Power of the copy in the active weapon slot, or 0 when the slot was
     * filled from the Blueprint default loadout rather than a rolled drop.
     * Callers treat 0 as "baseline, no scaling" — see UGA_Fire::PerformFireTrace.
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

    /** Fired after a successful tap reload — play the reload montage and audio here. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Weapons")
    void OnReloadPerformed();

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