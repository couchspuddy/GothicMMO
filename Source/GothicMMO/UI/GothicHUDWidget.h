// GothicHUDWidget.h
// Base class for all three HUD layout widgets.
// Each layout (A, C, F) is a Blueprint child of this class.
// The HUD class calls the update functions here which drive
// Blueprint-side animations and value bindings.
//
// Blueprint children:
//   WBP_HUD_LayoutA  — Destiny corners
//   WBP_HUD_LayoutC  — Gothic asymmetric
//   WBP_HUD_LayoutF  — Immersive

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GothicHUDWidget.generated.h"

UCLASS(Abstract, Blueprintable)
class GOTHICMMO_API UGothicHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // -------------------------------------------------------------------------
    // Called by AGothicHUD to push new values into the widget.
    // Implement visual updates in Blueprint (animate bars, flash numbers etc.)
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|HUD")
    void OnHealthChanged(float CurrentHealth, float MaxHealth);

    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|HUD")
    void OnSuperMeterChanged(float CurrentValue, float MaxValue);

    /**
     * @param SlotIndex     0 = ability 1, 1 = ability 2, 2 = ability 3
     * @param Remaining     Seconds remaining on cooldown (0 = ready)
     * @param Total         Total cooldown duration (for filling the overlay)
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|HUD")
    void OnAbilityCooldownChanged(int32 SlotIndex, float Remaining, float Total);

    /**
     * Flash a damage number on screen.
     * Layout A shows it near health bar.
     * Layout C shows it along the left edge.
     * Layout F shows it as a brief subtle red pulse.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|HUD")
    void OnDamageReceived(float DamageAmount);

    /**
     * Called when the super meter reaches 100%.
     * Trigger a "SUPER READY" animation in Blueprint.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|HUD")
    void OnSuperReady();

    /**
     * Called when an ability charge refills.
     * @param SlotIndex  Which ability slot recharged.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|HUD")
    void OnAbilityRecharged(int32 SlotIndex);

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|HUD")
    float CachedHealth = 100.f;

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|HUD")
    float CachedMaxHealth = 100.f;

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|HUD")
    float CachedSuperMeter = 0.f;
    
protected:
    virtual void NativeConstruct() override;

    // -------------------------------------------------------------------------
    // Cached values — available to Blueprint via getter nodes
    // -------------------------------------------------------------------------


    // Normalized 0-1 values for progress bar bindings
    UFUNCTION(BlueprintPure, Category = "Gothic|HUD")
    float GetHealthPercent() const
    {
        return CachedMaxHealth > 0.f ? CachedHealth / CachedMaxHealth : 0.f;
    }

    UFUNCTION(BlueprintPure, Category = "Gothic|HUD")
    float GetSuperPercent() const { return CachedSuperMeter; }
};
