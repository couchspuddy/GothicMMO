// GothicEnemyHealthBarWidget.h
// Base class for the world/screen-space health bar hosted on a
// UWidgetComponent attached to each enemy (AGothicEnemyBase::HealthBarWidget).
//
// Why this exists instead of the widget looking up its own owner: a
// UWidgetComponent-hosted widget has no OwningPlayer set — it isn't created
// via CreateWidget(PlayerController, Class) the way GothicHUDWidget is, so
// GetOwningPlayer()/GetOwningPlayerPawn() return null here, and there is no
// built-in "get the actor I'm attached to" node for a UMG widget. (The
// GetOwningActor entries visible in the Blueprint node search under
// Animation/GameplayAbility categories are unrelated functions from those
// systems — not usable from a UserWidget graph.)
//
// So this follows the SAME push convention GothicHUD already uses for
// GothicHUDWidget: the owner calls SetOwningEnemy() once, explicitly, right
// after the WidgetComponent creates its widget instance. From then on the
// widget reads through the cached weak pointer — no lookup, no per-frame
// search, and it degrades safely to 0%/blank if the enemy is ever destroyed
// out from under it (TWeakObjectPtr, not a raw pointer).
//
// Blueprint child: WBP_EnemyHealthBar
//   - Bind the Progress Bar's Percent property to GetHealthPercent()
//   - Bind the name TextBlock's Text property to GetAccursedName()

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GothicEnemyHealthBarWidget.generated.h"

class AGothicEnemyBase;

UCLASS(Abstract, Blueprintable)
class GOTHICMMO_API UGothicEnemyHealthBarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /**
     * Called once by AGothicEnemyBase right after the WidgetComponent creates
     * this widget instance. Not BlueprintCallable on purpose — this is C++-to-
     * widget plumbing, not something a designer wires up per-instance in UMG.
     */
    void SetOwningEnemy(AGothicEnemyBase* InOwningEnemy);

    /**
     * 0–1 health fraction, read live off the enemy's AttributeSet through the
     * cached weak pointer. Bind the Progress Bar's Percent to this — UMG's
     * native property binding polls it automatically, same idiom
     * GothicHUDWidget::GetHealthPercent() already uses for the player HUD.
     * Returns 0 if the enemy reference is stale (destroyed, corpse cleaned up).
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy")
    float GetHealthPercent() const;

    /** The Accursed's name, straight through from AGothicEnemyBase::AccursedName. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy")
    FText GetAccursedName() const;

protected:
    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Enemy")
    TWeakObjectPtr<AGothicEnemyBase> OwningEnemy;
};