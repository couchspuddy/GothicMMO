// GothicReviveChannelBarWidget.cpp

#include "UI/GothicReviveChannelBarWidget.h"
#include "Components/ProgressBar.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "Engine/World.h"

void UGothicReviveChannelBarWidget::BeginChannel(APlayerState* InDownedPlayer, APlayerState* InReviver)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DismissTimer);
    }

    DownedPlayer = InDownedPlayer;
    Reviver      = InReviver;
    Progress     = 0.f;
    ApplyProgress();
    OnChannelBegun();
}

void UGothicReviveChannelBarWidget::SetProgress(float NewProgress)
{
    Progress = FMath::Clamp(NewProgress, 0.f, 1.f);
    ApplyProgress();
    OnChannelProgress(Progress);
}

void UGothicReviveChannelBarWidget::EndChannel(bool bCompleted)
{
    if (bCompleted)
    {
        Progress = 1.f;
        ApplyProgress();
        OnChannelCompleted();
    }
    else
    {
        // Frozen where it stopped — the player should be able to see how close
        // they got, which is the entire readable payload of an interruption.
        ApplyProgress();
        OnChannelInterrupted();
    }

    ScheduleDismiss();
}

FText UGothicReviveChannelBarWidget::GetDownedPlayerName() const
{
    const APlayerState* PS = DownedPlayer.Get();
    return PS ? FText::FromString(PS->GetPlayerName()) : FText::GetEmpty();
}

FText UGothicReviveChannelBarWidget::GetReviverName() const
{
    const APlayerState* PS = Reviver.Get();
    return PS ? FText::FromString(PS->GetPlayerName()) : FText::GetEmpty();
}

void UGothicReviveChannelBarWidget::ApplyProgress()
{
    if (ChannelBar)
    {
        ChannelBar->SetPercent(Progress);
    }
}

void UGothicReviveChannelBarWidget::ScheduleDismiss()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            DismissTimer,
            [WeakThis = TWeakObjectPtr<UGothicReviveChannelBarWidget>(this)]()
            {
                if (WeakThis.IsValid())
                {
                    WeakThis->RemoveFromParent();
                }
            },
            FMath::Max(0.05f, DismissDelay),
            false);
    }
}

void UGothicReviveChannelBarWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DismissTimer);
    }
    Super::NativeDestruct();
}
