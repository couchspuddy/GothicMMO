// GothicSelahPromptWidget.h
// Base class for WBP_SelahPrompt.
// Handles the Accursed name cycling timer — Blueprint handles visuals.
//
// Flow:
//   1. BP_GothicGameState creates WBP_SelahPrompt (child of this class)
//   2. BP calls StartNameCycle with the names from GameState::SelahNames
//   3. This class fires OnNameRevealed for each name on a timer
//   4. After the last name, fires OnSelahMomentComplete
//   5. Blueprint drives all visual presentation from these events
//
// Reparent WBP_SelahPrompt to this class, add a TextBlock, and implement
// OnNameRevealed to set the text. That's the minimum wiring.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GothicSelahPromptWidget.generated.h"

UCLASS(Abstract, Blueprintable)
class GOTHICMMO_API UGothicSelahPromptWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /**
     * Starts cycling through the Accursed names. Call from Blueprint
     * after the widget is created and added to viewport.
     * Does nothing if Names is empty.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    void StartNameCycle(const TArray<FText>& Names);

    /**
     * Skips to the end — reveals all remaining names instantly
     * and fires OnSelahMomentComplete. For a "hold to skip" input.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Selah")
    void SkipToEnd();

    /** True while names are actively cycling. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Selah")
    bool IsCycling() const { return bIsCycling; }

protected:
    // -------------------------------------------------------------------------
    // Blueprint events — implement these in WBP_SelahPrompt
    // -------------------------------------------------------------------------

    /**
     * Called for each Accursed name in sequence.
     * @param Name      The name to display.
     * @param Index     0-based position in the sequence (0 = first name).
     * @param Total     Total number of names in this cycle.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
    void OnNameRevealed(const FText& Name, int32 Index, int32 Total);

    /**
     * Called after the last name has been shown and held for its full duration.
     * Use this to fade out the prompt, trigger collection UI, etc.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
    void OnSelahMomentComplete();

    /**
     * Called just before a name transitions to the next one.
     * Use for fade-out animations between names.
     * @param Index  The index of the name about to be replaced.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Selah")
    void OnNameFadeOut(int32 Index);

    // -------------------------------------------------------------------------
    // Tuning — set in WBP_SelahPrompt Class Defaults
    // -------------------------------------------------------------------------

    /** How long each name stays on screen before advancing. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float SecondsPerName = 2.0f;

    /** Pause before the first name appears (lets the silence land). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float InitialDelay = 0.5f;

    /** Pause after the last name before firing OnSelahMomentComplete. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float FinalHoldDuration = 1.5f;

private:
    TArray<FText> CachedNames;
    int32 CurrentNameIndex = 0;
    bool bIsCycling = false;

    FTimerHandle NameCycleTimer;

    void RevealNextName();
    void CompleteCycle();

    virtual void NativeDestruct() override;
};