// GothicHUD.h
// Master HUD class — owns and manages all screen widgets.
// Handles swapping between layout variants and crosshair types.
// Set this as the HUD class in BP_GothicGameMode.
//
// Blueprint child: BP_GothicHUD
//   - Assign all three layout widget classes
//   - Assign all four crosshair widget classes

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GothicHUDTypes.h"
#include "GothicHUD.generated.h"

class UGothicHUDWidget;
class UGothicCrosshairWidget;

UCLASS()
class GOTHICMMO_API AGothicHUD : public AHUD
{
    GENERATED_BODY()

public:
    AGothicHUD();

    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Layout switching — call from settings menu later
    // -------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void SetActiveLayout(EGothicHUDLayout NewLayout);

    UFUNCTION(BlueprintPure, Category = "Gothic|HUD")
    EGothicHUDLayout GetActiveLayout() const { return CurrentLayout; }

    // -------------------------------------------------------------------------
    // Crosshair switching — called by weapon equip system
    // -------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void SetCrosshairType(EGothicCrosshairType NewType);

    // -------------------------------------------------------------------------
    // Called every frame to update dynamic crosshair spread
    // based on player movement speed
    // -------------------------------------------------------------------------
    virtual void Tick(float DeltaTime) override;

    // -------------------------------------------------------------------------
    // Widget update passthrough — called by player/attribute delegates
    // -------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateHealth(float CurrentHealth, float MaxHealth);

    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateSuperMeter(float CurrentValue, float MaxValue);

    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateAbilityCooldown(EGothicAbilitySlot SlotIndex, float CooldownRemaining, float CooldownTotal);

    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void ShowDamageIndicator(float DamageAmount);

    UFUNCTION(BlueprintPure, Category = "Gothic|HUD")
    UGothicHUDWidget* GetHUDWidget() const { return ActiveHUDWidget; }
    
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateSelah(float CurrentSelah);
protected:
    // -------------------------------------------------------------------------
    // Widget classes — assign in BP_GothicHUD
    // -------------------------------------------------------------------------
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Layouts")
    TSubclassOf<UGothicHUDWidget> LayoutA_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Layouts")
    TSubclassOf<UGothicHUDWidget> LayoutC_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Layouts")
    TSubclassOf<UGothicHUDWidget> LayoutF_Class;

    // Crosshair widget classes
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Crosshairs")
    TSubclassOf<UGothicCrosshairWidget> Crosshair_Melee_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Crosshairs")
    TSubclassOf<UGothicCrosshairWidget> Crosshair_Pistol_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Crosshairs")
    TSubclassOf<UGothicCrosshairWidget> Crosshair_Rifle_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Crosshairs")
    TSubclassOf<UGothicCrosshairWidget> Crosshair_Throwable_Class;

    // -------------------------------------------------------------------------
    // Active widget instances
    // -------------------------------------------------------------------------
    UPROPERTY()
    TObjectPtr<UGothicHUDWidget> ActiveHUDWidget;

    UPROPERTY()
    TObjectPtr<UGothicCrosshairWidget> ActiveCrosshairWidget;

    EGothicHUDLayout CurrentLayout = EGothicHUDLayout::LayoutA;
    EGothicCrosshairType CurrentCrosshairType = EGothicCrosshairType::Melee;

    // Dynamic crosshair spread
    float CurrentSpread = 0.f;
    float TargetSpread  = 0.f;

private:
    void CreateAndShowLayout(TSubclassOf<UGothicHUDWidget> WidgetClass);
    void CreateAndShowCrosshair(TSubclassOf<UGothicCrosshairWidget> CrosshairClass);
    void RemoveActiveWidgets();
};
