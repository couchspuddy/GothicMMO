// GothicCrosshairWidget.h
// Base class for all crosshair widgets.
// Each weapon type has a Blueprint child that draws the correct shape.
// The spread value drives dynamic expansion on movement.
//
// Blueprint children:
//   WBP_Crosshair_Melee      — single center dot
//   WBP_Crosshair_Pistol     — four lines with gap, center dot
//   WBP_Crosshair_Rifle      — tight four lines, small dot
//   WBP_Crosshair_Throwable  — circle with center dot

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GothicCrosshairWidget.generated.h"

UCLASS(Abstract, Blueprintable)
class GOTHICMMO_API UGothicCrosshairWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /**
     * Called every frame by AGothicHUD with the current spread value.
     * 0.0 = standing still (tightest)
     * 1.0 = sprinting (most spread)
     * Use this in Blueprint to drive the position of crosshair lines
     * via render transforms or padding bindings.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Crosshair")
    void OnSpreadUpdated(float SpreadValue);

    /**
     * Called when a shot fires — triggers a brief expansion animation.
     * Blueprint plays a "fire spread" animation then returns to base.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Crosshair")
    void OnWeaponFired();

    /**
     * Called when aiming down sights — tightens crosshair significantly.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Crosshair")
    void OnAimDownSights(bool bIsAiming);

    /**
     * Called when the crosshair passes over an enemy.
     * Change color to red or add a hit indicator in Blueprint.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Crosshair")
    void OnEnemyDetected(bool bEnemyInSights);

protected:
    /** Base spread at rest — set per crosshair type in Blueprint. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Crosshair")
    float BaseSpread = 0.f;

    /** Max additional spread when sprinting. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Crosshair")
    float MaxSpread = 30.f;

    /**
     * Crosshair color at rest.
     * White for neutral, red when on enemy (driven by OnEnemyDetected).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Crosshair")
    FLinearColor NeutralColor = FLinearColor(1.f, 1.f, 1.f, 0.85f);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Crosshair")
    FLinearColor EnemyColor = FLinearColor(0.9f, 0.2f, 0.2f, 0.9f);
};
