// GothicQuitMenuWidget.cpp

#include "UI/GothicQuitMenuWidget.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/PlayerController.h"

void UGothicQuitMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (UButton* QuitButton = Cast<UButton>(GetWidgetFromName(TEXT("QuitButton"))))
    {
        QuitButton->OnClicked.AddDynamic(this, &UGothicQuitMenuWidget::HandleQuitClicked);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicQuitMenuWidget: no button named 'QuitButton' — the quit option will do "
                 "nothing. Add one to WBP_QuitMenu with that exact name."));
    }

    if (UButton* ResumeButton = Cast<UButton>(GetWidgetFromName(TEXT("ResumeButton"))))
    {
        ResumeButton->OnClicked.AddDynamic(this, &UGothicQuitMenuWidget::HandleResumeClicked);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicQuitMenuWidget: no button named 'ResumeButton' — the menu can only be "
                 "closed by whatever opened it."));
    }

    // Take UI input while the menu is up. Without this the player keeps firing
    // and looking around behind the menu, and the cursor stays hidden so the
    // buttons cannot be clicked at all.
    if (APlayerController* PC = GetOwningPlayer())
    {
        FInputModeUIOnly Mode;
        Mode.SetWidgetToFocus(TakeWidget());
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode);
        PC->bShowMouseCursor = true;
    }

    OnMenuShown();
}

void UGothicQuitMenuWidget::DismissMenu()
{
    // Restore game input BEFORE removing, while GetOwningPlayer() is still valid.
    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
    }

    RemoveFromParent();
}

void UGothicQuitMenuWidget::ConfirmQuit()
{
    // Quit rather than EndPlay: in PIE this ends the session, in a packaged
    // build it closes the game, and callers do not have to care which.
    UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(),
                                   EQuitPreference::Quit, /*bIgnorePlatformRestrictions*/ false);
}

void UGothicQuitMenuWidget::HandleQuitClicked()
{
    ConfirmQuit();
}

void UGothicQuitMenuWidget::HandleResumeClicked()
{
    DismissMenu();
}
