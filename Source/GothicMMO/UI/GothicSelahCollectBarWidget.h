// GothicSelahCollectBarWidget.h
// The Selah collection channel bar. Fills 0 -> 1 over the collect duration.
// The Feral Retained encounter interrupts it partway (the wave crashes in and
// the bar "breaks"); a normal collection fills it to full and rewards.
//
// Driven entirely by AGothicGameState's replicated collect phase, so every
// client shows the same bar. Reparent WBP_SelahCollectBar to this and bind a
// UProgressBar named "CollectBar" (or read GetProgress() in Blueprint).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GothicSelahCollectBarWidget.generated.h"

class UProgressBar;

UCLASS(Abstract, Blueprintable)
class GOTHICMMO_API UGothicSelahCollectBarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Begin filling from 0 to full over Duration seconds. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    void StartCollect(float Duration);

    /** The interrupt — freeze the bar where it is and flag the break. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    void InterruptCollect();

    /** Collection finished — snap to full, then dismiss. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    void CompleteCollect();

    /** 0..1 fill fraction, for Blueprints that bind the bar themselves. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Selah")
    float GetProgress() const { return Progress; }

protected:
    /** Optional auto-driven bar — name a UProgressBar "CollectBar" in the widget. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UProgressBar> CollectBar;

    /** Seconds the "broken"/"filled" flourish lingers before the bar removes itself. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float DismissDelay = 1.1f;

    // Blueprint flourish hooks — color/shake on interrupt, flash on complete.
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
    void OnCollectStarted(float Duration);
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
    void OnCollectInterrupted();
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
    void OnCollectCompleted();

    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual void NativeDestruct() override;

private:
    float Progress = 0.f;
    float FillDuration = 5.f;
    bool bFilling = false;

    FTimerHandle DismissTimer;

    void ApplyProgress();
    void ScheduleDismiss();
};
