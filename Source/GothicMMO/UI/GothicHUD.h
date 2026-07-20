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

/** Internal tracking for a single floating damage number on screen. */
struct FGothicDamageNumber
{
    FVector WorldLocation;
    float DamageAmount = 0.f;
    bool bWasVital = false;
    float SpawnTime = 0.f;
    float RandomOffsetX = 0.f;
};

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
    
    // AGothicHUD.h — add to public section, alongside the other Update* passthroughs
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void NotifyOwningPawnChanged(APawn* NewPawn);
    
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateAmmo(int32 Magazine, int32 MagazineCapacity, int32 Reserve, int32 MaxReserve);

    // -------------------------------------------------------------------------
    // Damage numbers — floating text on dealt damage
    // Called from AGothicEnemyBase::MulticastOnHit on every client.
    // -------------------------------------------------------------------------
    void ShowDamageNumber(FVector WorldLocation, float DamageAmount, bool bWasVital);

    virtual void DrawHUD() override;
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

    // -------------------------------------------------------------------------
    // Damage numbers — tuning (EditDefaultsOnly via BP_GothicHUD)
    // -------------------------------------------------------------------------

    /** How long the number stays visible before fully fading. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    float DamageNumberDuration = 1.2f;

    /** Pixels per second the number floats upward. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    float DamageNumberFloatSpeed = 60.f;

    /** Body hit text color. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    FLinearColor BodyHitColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

    /** Vital hit text color — gold by default; tunable in BP_GothicHUD. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    FLinearColor VitalHitColor = FLinearColor(1.f, 0.85f, 0.1f, 1.f);

    /** Scale multiplier for vital hit numbers (1.0 = same as body). */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    float VitalHitScale = 1.4f;

    TArray<FGothicDamageNumber> ActiveDamageNumbers;
};