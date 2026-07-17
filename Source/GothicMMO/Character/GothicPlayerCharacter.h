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
#include "GothicMMO.h"
#include "GothicPlayerCharacter.generated.h"




class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UGothicInputHandlerComponent;
class UGothicAbilitySet;
class UGothicCombatStateComponent;
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



    UFUNCTION(BlueprintCallable, Category = "Combat")
    void OnMelee();
    
    UFUNCTION(Client, Reliable, BlueprintCallable, Category = "Gothic|Selah")
    void TriggerSelahMoment();
    
    /** Called by the player when they trigger a completed encounter's shared Selah prompt. */
    UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Gothic|Selah")
    void ServerCollectEncounterSelah(class AGothicEncounterVolume* Encounter);
    
    // GothicPlayerCharacter.h — add to public section
    virtual void OnDeath_Implementation(AActor* Killer) override;
    
    /** True if the magazine has at least one round. Gates GA_Fire activation. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Ammo")
    bool HasRoundChambered() const { return CurrentMagazineAmmo > 0; }

    /** Decrements the magazine and notifies the HUD. Called by GA_Fire on commit. */
    void ConsumeRound();

    /**
     * Kicks the camera up by RecoilKickPitch and hands the recovery to Tick.
     *
     * The recovery loop in Tick has always been correct and has never had a caller —
     * RecoilKickPitch sat in this header with no reader, so the gun had a recoil
     * recovery system and no recoil. This is the missing half.
     *
     * Local-only cosmetic. Call from GA_Fire's IsLocallyControlled block, never
     * from the server path.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Combat")
    void ApplyRecoilKick();

protected:
    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;
    
    /**
     * Degrees added to the recoil target per shot. This is a request, not an
     * applied rotation — Tick chases it at RecoilRiseSpeed. Because the target
     * decays while the camera is still climbing toward it, the visible peak lands
     * around 70% of this value. Tune by feel, not arithmetic.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Recoil")
    float RecoilKickPitch = 5.0f;

    /**
     * How fast the camera climbs toward the target. This is the node that was
     * missing: AddControllerPitchInput applies a one-frame rotation delta, so a
     * direct kick teleports the camera on frame 1 and reads as a twitch rather
     * than a gun going off. ~30 gives roughly a 4-frame rise at 60fps.
     *
     * Must stay meaningfully above RecoilRecoverySpeed or the camera never
     * catches the target and the kick flattens to nothing.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Recoil")
    float RecoilRiseSpeed = 30.0f;

    /** How fast the target bleeds back to zero. Aim always returns to origin. */
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Recoil")
    float RecoilRecoverySpeed = 8.0f;
    
    // ── ADS ──────────────────────────────────────────────────────────────────
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float HipFireFOV = 100.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float ADSFOV = 70.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float ADSInterpSpeed = 10.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float ADSMovementSpeed = 250.f;
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
    TObjectPtr<UGothicCombatStateComponent> CombatStateComponent;
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Steadfast")
    TObjectPtr<class UGothicSteadfastComponent> SteadfastComponent;
    
    

    bool bIsADS = false;

    void OnADSStart();
    void OnADSEnd();


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
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> ADSAction;
    
    
    
    

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

    // -------------------------------------------------------------------------
    // Combat — direct damage values for non-GAS actions
    // -------------------------------------------------------------------------
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    TSubclassOf<UGameplayEffect> DamageEffectClass;
    
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> SprintAction;

    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float PistolDamage = 15.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float MeleeDamage = 10.f;
    
    /** Override in Blueprint to handle visual and audio of the Selah moment. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
    void OnSelahMoment();
    
    UPROPERTY(BlueprintReadWrite, Category = "Gothic|Combat")
    bool bPistolBound = false;
    


    
    /**
 * Fired whenever magazine or reserve ammo changes, for any reason.
 * The HUD binds to this rather than reading the properties directly, so that
 * moving ammo behind a Server RPC / OnRep later does not touch the UI layer.
 *
 * AUTHORITY NOTE: today this broadcasts from client-local fire/reload code
 * (OnFire, TapReload and HoldReload are input-bound with no Server RPC — this
 * chain is standalone/listen-host only). Ammo is deliberately NOT replicated
 * for that reason: a replicated property would be clobbered by the server's
 * stale value on the next update. When fire moves server-side, add
 * ReplicatedUsing = OnRep_Ammo and broadcast from there instead. Nothing
 * downstream of this delegate needs to change.
 */
    DECLARE_MULTICAST_DELEGATE_FourParams(FOnAmmoChanged, int32 /*Magazine*/, int32 /*MagCapacity*/, int32 /*Reserve*/, int32 /*MaxReserve*/);
    FOnAmmoChanged OnAmmoChanged;
    // Per-weapon ammo state — belongs in GothicPlayerCharacter.h
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Ammo")
    int32 MagazineCapacity = 6;

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Ammo")
    int32 CurrentMagazineAmmo = 6;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Ammo")
    int32 MaxReserveAmmo = 18;

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Ammo")
    int32 CurrentReserveAmmo = 18;
    
    // Header additions
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<class UInputAction> ReloadAction;

    float ReloadPressStartTime = 0.f;
    static constexpr float HoldThreshold = 0.4f; // seconds — tune to feel

    void OnReloadPressed();
    void OnReloadReleased();
    void TapReload();
    void HoldReload();
    /** Pushes current ammo to the local HUD. Single funnel — call after ANY ammo mutation. */
    void BroadcastAmmoChanged();

    /** Returns the HUD belonging to THIS pawn's controller, not the first PC in the world. */
    class AGothicHUD* GetLocalGothicHUD() const;
    

    // -------------------------------------------------------------------------
    // Input handlers — direct non-ability bindings
    // -------------------------------------------------------------------------
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);

    // -------------------------------------------------------------------------
    // GAS init
    // -------------------------------------------------------------------------
    void InitGASFromPlayerState();
    void OnSprintStart();
    void OnSprintStop();

private:
    bool bHUDReady = false;
    float CurrentRecoilPitch = 0.f;

    /** Where the camera is climbing to. Shots add, Tick bleeds it away. */
    float TargetRecoilPitch = 0.f;
    bool IsReckoningActive() const;
    bool bAttributeDelegatesBound = false;
};