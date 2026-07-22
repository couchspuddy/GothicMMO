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

    /**
     * Swap to a weapon slot by index. Updates mesh, crosshair, and ammo state.
     * Safe to call with the current index — early-outs if already active.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Weapons")
    void SwapWeapon(int32 NewIndex);

    /** Called after SwapWeapon completes — Blueprint can play swap animations, sounds. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Weapons")
    void OnWeaponSwapped(int32 NewIndex, const UGothicWeaponData* NewWeaponData);

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
};