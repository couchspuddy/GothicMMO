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
#include "GothicPlayerCharacter.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCollectionInterrupted);

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

    // BlueprintCallable so Blueprint Event Graph can invoke these directly
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void OnFire();

    UFUNCTION(BlueprintCallable, Category = "Combat")
    void OnMelee();
    
    UFUNCTION(Client, Reliable, BlueprintCallable, Category = "Gothic|Selah")
    void TriggerSelahMoment();
    
    /** Called by the player when they trigger a completed encounter's shared Selah prompt. */
    UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Gothic|Selah")
    void ServerCollectEncounterSelah(class AGothicEncounterVolume* Encounter);
    
    // GothicPlayerCharacter.h — add to public section
    virtual void OnDeath_Implementation(AActor* Killer) override;

protected:
    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;
    
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Recoil")
    float RecoilKickPitch = 2.0f;

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

    float CurrentRecoilOffset = 0.f;
    float TargetRecoilOffset = 0.f;

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
    
    UPROPERTY(BlueprintReadWrite, Category = "Gothic|Selah")
    bool bCollecting = false;
    
    UPROPERTY(BlueprintAssignable, Category = "Gothic|Selah")
    FOnCollectionInterrupted OnCollectionInterrupted;

    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    void CancelCollectionRite();
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
    bool bAbilitiesGranted = false;
    float CurrentRecoilPitch = 0.f;
    bool IsReckoningActive() const;
};
