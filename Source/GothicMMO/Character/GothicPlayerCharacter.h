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
#include "GothicPlayerCharacter.generated.h"

class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UGothicInputHandlerComponent;
class UGothicAbilitySet;
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

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Weapons")
    TArray<FGothicWeaponSlot> WeaponSlots;

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Weapons")
    int32 ActiveWeaponIndex = 0;
    
protected:
    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

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

    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float PistolDamage = 15.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float MeleeDamage = 10.f;
    
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

    // -------------------------------------------------------------------------
    // GAS init
    // -------------------------------------------------------------------------
    void InitGASFromPlayerState();

private:
    bool bHUDReady = false;
    bool bAbilitiesGranted = false;
};