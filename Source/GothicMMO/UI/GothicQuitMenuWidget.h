// GothicQuitMenuWidget.h
// Minimal quit-confirmation screen.
//
// Setup in editor:
//   1. Create WBP_QuitMenu, reparent it to GothicQuitMenuWidget
//   2. Add two Buttons named exactly "QuitButton" and "ResumeButton"
//   3. Assign WBP_QuitMenu to BP_GothicHUD's QuitMenuClass
//
// The buttons are bound by NAME in NativeConstruct rather than by
// BindWidget/UPROPERTY, matching the pattern GothicHUDWidget already uses for
// CooldownBar_0/1/2. BindWidget would be stricter, but it hard-fails Blueprint
// compilation if a designer renames or removes the button, which is a harsh
// trade for a menu whose whole job is to be un-fancy. A missing button logs
// instead, and the menu still closes on cancel.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GothicQuitMenuWidget.generated.h"

class UButton;

UCLASS()
class GOTHICMMO_API UGothicQuitMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Close the menu and hand input back to the game. Safe to call twice. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|UI")
    void DismissMenu();

    /** Quit to desktop. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|UI")
    void ConfirmQuit();

    /** Blueprint hooks for a fade/scale flourish, if one is ever wanted. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|UI")
    void OnMenuShown();

protected:
    virtual void NativeConstruct() override;

private:
    UFUNCTION()
    void HandleQuitClicked();

    UFUNCTION()
    void HandleResumeClicked();
};
