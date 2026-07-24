// GothicSelahPromptWidget.cpp

#include "UI/GothicSelahPromptWidget.h"
#include "TimerManager.h"
#include "Engine/World.h"

void UGothicSelahPromptWidget::StartNameCycle(const TArray<FText>& Names)
{
    if (Names.Num() == 0)
    {
        return;
    }

    CachedNames = Names;
    CurrentNameIndex = 0;
    bIsCycling = true;


    // Initial delay — let the silence land before the first name
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            NameCycleTimer,
            this,
            &UGothicSelahPromptWidget::RevealNextName,
            InitialDelay,
            false);
    }
}

void UGothicSelahPromptWidget::RevealNextName()
{
    if (!bIsCycling || CurrentNameIndex >= CachedNames.Num())
    {
        CompleteCycle();
        return;
    }

    const FText& Name = CachedNames[CurrentNameIndex];


    OnNameRevealed(Name, CurrentNameIndex, CachedNames.Num());

    CurrentNameIndex++;

    if (UWorld* World = GetWorld())
    {
        if (CurrentNameIndex < CachedNames.Num())
        {
            // Fire fade-out event shortly before the next name
            FTimerHandle FadeTimer;
            const int32 FadingIndex = CurrentNameIndex - 1;
            TWeakObjectPtr<UGothicSelahPromptWidget> WeakThis(this);
            World->GetTimerManager().SetTimer(
                FadeTimer,
                [WeakThis, FadingIndex]()
                {
                    if (WeakThis.IsValid() && WeakThis->bIsCycling)
                    {
                        WeakThis->OnNameFadeOut(FadingIndex);
                    }
                },
                SecondsPerName * 0.7f,
                false);

            // Advance to the next name
            World->GetTimerManager().SetTimer(
                NameCycleTimer,
                this,
                &UGothicSelahPromptWidget::RevealNextName,
                SecondsPerName,
                false);
        }
        else
        {
            // Last name — hold, then complete
            World->GetTimerManager().SetTimer(
                NameCycleTimer,
                this,
                &UGothicSelahPromptWidget::CompleteCycle,
                FinalHoldDuration,
                false);
        }
    }
}

void UGothicSelahPromptWidget::SkipToEnd()
{
    if (!bIsCycling) return;


    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(NameCycleTimer);
    }

    // Reveal any unseen names instantly
    while (CurrentNameIndex < CachedNames.Num())
    {
        OnNameRevealed(CachedNames[CurrentNameIndex], CurrentNameIndex, CachedNames.Num());
        CurrentNameIndex++;
    }

    CompleteCycle();
}

void UGothicSelahPromptWidget::CompleteCycle()
{
    bIsCycling = false;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(NameCycleTimer);
    }


    OnSelahMomentComplete();
}

void UGothicSelahPromptWidget::NativeDestruct()
{
    bIsCycling = false;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(NameCycleTimer);
    }

    Super::NativeDestruct();
}