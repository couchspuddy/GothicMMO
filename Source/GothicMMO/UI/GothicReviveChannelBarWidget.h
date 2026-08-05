// GothicReviveChannelBarWidget.h
// The revive channel bar. Shown to BOTH sides of a revive — the player doing the
// channelling and the player on the floor — on their own local HUDs.
//
// Deliberately NOT self-ticking, which is the one place it differs from
// UGothicSelahCollectBarWidget. That bar owns its own clock because a collection
// is a client-side flourish over a phase the server only starts and stops; a
// revive channel is authoritative for its whole length (movement, damage and
// release all cancel it server-side), so the fill has to be the SERVER's number
// or the two machines disagree about how close the revive got. Progress arrives
// through SetProgress from the replicated value on the downed player's
// PlayerState.
//
// Reparent WBP_ReviveChannelBar to this and bind a UProgressBar named
// "ChannelBar" (or read GetProgress() in Blueprint).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GothicReviveChannelBarWidget.generated.h"

class UProgressBar;
class APlayerState;

UCLASS(Abstract, Blueprintable)
class GOTHICMMO_API UGothicReviveChannelBarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** A channel just started. Both PlayerStates may be null on a machine that has
     *  not replicated them yet — the bar still shows, just without names. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Revive")
    void BeginChannel(APlayerState* InDownedPlayer, APlayerState* InReviver);

    /** Push the server's 0..1 fill. Called once per replicated update. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Revive")
    void SetProgress(float NewProgress);

    /** Channel ended. Completed snaps to full; interrupted freezes where it was. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Revive")
    void EndChannel(bool bCompleted);

    UFUNCTION(BlueprintPure, Category = "Gothic|Revive")
    float GetProgress() const { return Progress; }

    /** Name of the player being picked up, for "Reviving <name>" text. Empty when
     *  the PlayerState has not replicated yet. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Revive")
    FText GetDownedPlayerName() const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Revive")
    FText GetReviverName() const;

protected:
    /** Optional auto-driven bar — name a UProgressBar "ChannelBar" in the widget. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UProgressBar> ChannelBar;

    /** Seconds the finished/broken flourish lingers before the bar removes itself. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Revive")
    float DismissDelay = 0.8f;

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Revive")
    TWeakObjectPtr<APlayerState> DownedPlayer;

    UPROPERTY(BlueprintReadOnly, Category = "Gothic|Revive")
    TWeakObjectPtr<APlayerState> Reviver;

    // Blueprint flourish hooks.
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Revive")
    void OnChannelBegun();
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Revive")
    void OnChannelProgress(float NewProgress);
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Revive")
    void OnChannelInterrupted();
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Revive")
    void OnChannelCompleted();

    virtual void NativeDestruct() override;

private:
    float Progress = 0.f;

    FTimerHandle DismissTimer;

    void ApplyProgress();
    void ScheduleDismiss();
};
