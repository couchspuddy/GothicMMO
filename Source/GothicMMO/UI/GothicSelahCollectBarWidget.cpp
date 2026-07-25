// GothicSelahCollectBarWidget.cpp

#include "UI/GothicSelahCollectBarWidget.h"
#include "Components/ProgressBar.h"
#include "TimerManager.h"
#include "Engine/World.h"

void UGothicSelahCollectBarWidget::StartCollect(float Duration)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DismissTimer);
    }

    FillDuration = FMath::Max(0.01f, Duration);
    Progress = 0.f;
    bFilling = true;
    ApplyProgress();
    OnCollectStarted(FillDuration);
}

void UGothicSelahCollectBarWidget::InterruptCollect()
{
    bFilling = false;
    ApplyProgress();
    OnCollectInterrupted();
    ScheduleDismiss();
}

void UGothicSelahCollectBarWidget::CompleteCollect()
{
    bFilling = false;
    Progress = 1.f;
    ApplyProgress();
    OnCollectCompleted();
    ScheduleDismiss();
}

void UGothicSelahCollectBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (!bFilling)
    {
        return;
    }

    Progress = FMath::Clamp(Progress + InDeltaTime / FillDuration, 0.f, 1.f);
    ApplyProgress();

    if (Progress >= 1.f)
    {
        bFilling = false;
    }
}

void UGothicSelahCollectBarWidget::ApplyProgress()
{
    if (CollectBar)
    {
        CollectBar->SetPercent(Progress);
    }
}

void UGothicSelahCollectBarWidget::ScheduleDismiss()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            DismissTimer,
            [WeakThis = TWeakObjectPtr<UGothicSelahCollectBarWidget>(this)]()
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

void UGothicSelahCollectBarWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DismissTimer);
    }
    Super::NativeDestruct();
}
