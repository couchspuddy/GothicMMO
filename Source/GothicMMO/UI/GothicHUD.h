// GothicHUD.h
// Master HUD class — owns and manages all screen widgets.
// Handles swapping between layout variants and crosshair types.
// Set this as the HUD class in BP_GothicGameMode.
//
// Blueprint child: BP_GothicHUD
//   - Assign all three layout widget classes
//   - Assign all four crosshair widget classes

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GothicHUDTypes.h"
#include "GothicHUD.generated.h"

class UGothicHUDWidget;
class UGothicCrosshairWidget;
class UGothicQuitMenuWidget;
class UGothicReviveChannelBarWidget;
class AGothicEnemyBase;
class AGothicPlayerState;

/** Internal tracking for a single floating damage number on screen. */
struct FGothicDamageNumber
{
    FVector WorldLocation;
    float DamageAmount = 0.f;
    bool bWasVital = false;
    float SpawnTime = 0.f;
    float RandomOffsetX = 0.f;
};

/** Tracks an enemy whose health bar should render on the HUD. */
struct FEnemyHealthBarEntry
{
    TWeakObjectPtr<AGothicEnemyBase> Enemy;
    float LastHitTime = 0.f;
};

/**
 * Fired on the SHOOTER'S local client the instant one of their shots is confirmed
 * landing on a valid target. bIsVital is true for a vital/critical hit. The
 * procedural crosshair widget (or any UMG) binds this to flash a hit marker — the
 * C++ side only raises the signal; the visual wiring lives in the editor lane.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGothicOnHitConfirmed, bool, bIsVital);

UCLASS()
class GOTHICMMO_API AGothicHUD : public AHUD
{
    GENERATED_BODY()

public:
    AGothicHUD();

    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Layout switching — call from settings menu later
    // -------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void SetActiveLayout(EGothicHUDLayout NewLayout);

    UFUNCTION(BlueprintPure, Category = "Gothic|HUD")
    EGothicHUDLayout GetActiveLayout() const { return CurrentLayout; }

    // -------------------------------------------------------------------------
    // Crosshair switching — called by weapon equip system
    // -------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void SetCrosshairType(EGothicCrosshairType NewType);

    /**
     * Show or hide the crosshair. Hidden while a full-screen menu owns the cursor
     * (inventory, pause) — spread and enemy detection stop updating while hidden.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void SetCrosshairVisible(bool bVisible);

    /**
     * Tells the active crosshair the player started or stopped aiming.
     *
     * UGothicCrosshairWidget::OnAimDownSights existed from the start with nothing
     * calling it, because ADS itself was never implemented — the reticle has been
     * drawing a spread the bullets did not have. Routed through the HUD rather than
     * letting the pawn reach into the widget, so the HUD stays the only thing that
     * knows which crosshair is currently up.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD|Crosshairs")
    void NotifyAimDownSights(bool bIsAiming);

    // -------------------------------------------------------------------------
    // Called every frame to update dynamic crosshair spread
    // based on player movement speed
    // -------------------------------------------------------------------------
    virtual void Tick(float DeltaTime) override;

    // -------------------------------------------------------------------------
    // Widget update passthrough — called by player/attribute delegates
    // -------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateHealth(float CurrentHealth, float MaxHealth);

    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateSuperMeter(float CurrentValue, float MaxValue);

    /** Steadfast pip row. See UGothicHUDWidget::OnSteadfastChanged for why it was missing. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateSteadfast(float CurrentValue, float MaxValue);

    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateAbilityCooldown(EGothicAbilitySlot SlotIndex, float CooldownRemaining, float CooldownTotal);

    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void ShowDamageIndicator(float DamageAmount);

    /**
     * Local player's State.Stunned toggled — forwards to the active layout widget's
     * OnStunnedStateChanged. Driven by AGothicPlayerCharacter::HandleStunTagChanged
     * (the same tag listener the pawn uses to freeze movement); local player only.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void NotifyStunnedStateChanged(bool bStunned, float ExpectedDuration);

    UFUNCTION(BlueprintPure, Category = "Gothic|HUD")
    UGothicHUDWidget* GetHUDWidget() const { return ActiveHUDWidget; }
    
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateSelah(float CurrentSelah);
    
    // AGothicHUD.h — add to public section, alongside the other Update* passthroughs
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void NotifyOwningPawnChanged(APawn* NewPawn);
    
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void UpdateAmmo(int32 Magazine, int32 MagazineCapacity, int32 Reserve, int32 MaxReserve);

    // -------------------------------------------------------------------------
    // Damage numbers — floating text on dealt damage
    // Called from AGothicEnemyBase::MulticastOnHit on every client.
    // -------------------------------------------------------------------------
    void ShowDamageNumber(FVector WorldLocation, float DamageAmount, bool bWasVital);

    /** Register an enemy to show a HUD-drawn health bar. Called from MulticastOnHit. */
    void RegisterEnemyHealthBar(AGothicEnemyBase* Enemy);

    // -------------------------------------------------------------------------
    // Hit marker — shooter-local confirmation for the crosshair/UMG layer.
    // -------------------------------------------------------------------------

    /**
     * Bindable in BP_GothicHUD or the crosshair widget to flash a hit marker.
     * Broadcast only on the shooter's client (MulticastOnHit scopes it by
     * instigator), so it never fires for someone else's hits.
     */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|HUD|HitMarker")
    FGothicOnHitConfirmed OnHitConfirmed;

    /** Broadcasts OnHitConfirmed. Called from the shooter-scoped branch of
     *  AGothicEnemyBase::MulticastOnHit; the caller guarantees the local scoping. */
    void NotifyHitConfirmed(bool bIsVital);

    /**
     * Show the quit-confirmation screen, or dismiss it if it is already up.
     *
     * BlueprintCallable so it can be driven from an input action without any
     * extra plumbing, and console-reachable via
     * `ke BP_GothicHUD_C_0 ToggleQuitMenu` for testing without a bound key.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD")
    void ToggleQuitMenu();

    UFUNCTION(BlueprintPure, Category = "Gothic|HUD")
    bool IsQuitMenuOpen() const { return ActiveQuitMenu != nullptr; }

    // -------------------------------------------------------------------------
    // Interaction prompt — contextual, never persistent.
    // Called from interactable Blueprints on begin/end overlap. Deliberately a
    // canvas draw rather than a widget: it matches the damage-number and enemy
    // health-bar path already here, and needs no per-interactable widget state.
    // Sits just below the reticle — contextual, so it does not violate the HUD
    // Doctrine's rule that nothing persistent may occupy Zone 3.
    // -------------------------------------------------------------------------

    /**
     * Show an interaction prompt. Pass self as Interactable and the result of
     * the BPI_Interactable GetInteractText call as PromptText.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD|Interact")
    void SetInteractPrompt(AActor* Interactable, const FText& PromptText);

    /**
     * Clear the prompt, but only if the caller is the actor that set it.
     * Guards the overlap race: walking from A straight into B fires B's Begin
     * before A's End, and an unguarded clear would blank B's prompt.
     * Pass nullptr to clear unconditionally.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD|Interact")
    void ClearInteractPrompt(AActor* Requester);

    /**
     * Who currently owns the prompt, or null if the slot is free. Callers that
     * re-assert a prompt every tick (the Selah "Collect Selah" prompt) must check this
     * and yield, otherwise they starve every proximity interactable in range.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|HUD|Interact")
    AActor* GetInteractPromptOwner() const { return InteractPromptOwner.Get(); }

    // -------------------------------------------------------------------------
    // Tutorial hint — a SECOND, INDEPENDENT canvas slot above the interact band.
    //
    // Canvas-drawn for the reasons the interact prompt's header gives, which
    // apply unchanged here: no per-hint widget state, and it joins the same draw
    // path as the damage numbers and health bars.
    //
    // It is deliberately NOT the interact prompt. That slot is single-owner and
    // arbitrated by GetInteractPromptOwner — routing hints through it would mean
    // a hint and a doorway fighting over one line of text, and the hint would win
    // for five seconds while the player stood at the door pressing E. Separate
    // slot, separate Y offset, separate colour, no interaction between them.
    // -------------------------------------------------------------------------

    /** Put a hint on screen. Replaces whatever hint was there; never touches the interact prompt. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD|Hints")
    void SetTutorialHint(const FText& HintText);

    /** Clear the hint slot. No owner check — the hint manager is the only writer. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD|Hints")
    void ClearTutorialHint();

    /** True while a hint is on screen. */
    UFUNCTION(BlueprintPure, Category = "Gothic|HUD|Hints")
    bool HasTutorialHint() const { return !TutorialHintText.IsEmpty(); }

    // -------------------------------------------------------------------------
    // Revive channel bar
    //
    // Widget rather than a canvas draw, unlike the interact prompt above: this one
    // has real per-frame state (a fill, two names, a break/complete flourish) and a
    // designer will want to art it, which is exactly the line the Selah collect bar
    // already sits on the other side of.
    //
    // The HUD owns it because the HUD is per-local-player BY CONSTRUCTION — one
    // AHUD per local PlayerController — so CreateWidget(GetOwningPlayerController())
    // here cannot make the mistake AGothicGameState::OnRep_SelahCollect had to be
    // fixed for, where GetFirstPlayerController() gave the listen-server host the
    // only bar in the game. Nothing in this feature ever asks for player 0.
    //
    // Driven from AGothicPlayerCharacter::Tick on the locally-controlled pawn,
    // reading the replicated channel state off AGothicPlayerState. The HUD does not
    // poll for itself — it has no opinion about which channel concerns this viewer.
    // -------------------------------------------------------------------------

    /**
     * Show the bar, or update it if it is already up. Progress is the SERVER's
     * 0..1 value — the bar does not run its own clock.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD|Revive")
    void ShowReviveChannel(AGothicPlayerState* DownedPlayer, AGothicPlayerState* Reviver, float Progress);

    /** Close the bar. bCompleted picks the finished flourish over the broken one. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|HUD|Revive")
    void HideReviveChannel(bool bCompleted);

    /** 0..1 of the bar currently up, or 0 when none is. */
    UFUNCTION(BlueprintPure, Category = "Gothic|HUD|Revive")
    float GetReviveChannelProgress() const { return ReviveChannelProgress; }

    UFUNCTION(BlueprintPure, Category = "Gothic|HUD|Revive")
    bool IsReviveChannelShown() const { return bReviveChannelShown; }

    // Blueprint hooks for HUDs that would rather drive their own visuals than
    // reparent a widget — these fire whether or not ReviveChannelBarClass is set.
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|HUD|Revive")
    void OnReviveChannelShown(AGothicPlayerState* DownedPlayer, AGothicPlayerState* Reviver);
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|HUD|Revive")
    void OnReviveChannelUpdated(float Progress);
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|HUD|Revive")
    void OnReviveChannelHidden(bool bCompleted);

    virtual void DrawHUD() override;
protected:
    void DrawInteractPrompt();
    void DrawTutorialHint();

    /** Active hint text. Empty means nothing is drawn. */
    FText TutorialHintText;

    /**
     * Pixels from screen centre. NEGATIVE — the hint sits ABOVE the reticle while
     * the interact prompt sits below it, so a hint and a prompt can be up at the
     * same time without overlapping. Change the sign here and the two collide.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|Hints")
    float TutorialHintOffsetY = -160.f;

    /** Colder than the interact prompt's warm gold — a hint is instruction, not an offer. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|Hints")
    FLinearColor TutorialHintColor = FLinearColor(0.78f, 0.86f, 0.94f, 1.f);

    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|Hints")
    float TutorialHintScale = 1.05f;

    /** Active prompt text. Empty means nothing is drawn. */
    FText InteractPromptText;

    /** Actor that owns the active prompt — see ClearInteractPrompt. */
    UPROPERTY()
    TWeakObjectPtr<AActor> InteractPromptOwner;

    /** Key hint rendered before the prompt text. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|Interact")
    FString InteractKeyHint = TEXT("E");

    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|Interact")
    FLinearColor InteractPromptColor = FLinearColor(0.96f, 0.90f, 0.72f, 1.f);

    /** Pixels below screen centre. Keeps the prompt clear of the reticle. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|Interact")
    float InteractPromptOffsetY = 96.f;

    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|Interact")
    float InteractPromptScale = 1.15f;

    // -------------------------------------------------------------------------
    // Widget classes — assign in BP_GothicHUD
    // -------------------------------------------------------------------------
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Layouts")
    TSubclassOf<UGothicHUDWidget> LayoutA_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Layouts")
    TSubclassOf<UGothicHUDWidget> LayoutC_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Layouts")
    TSubclassOf<UGothicHUDWidget> LayoutF_Class;

    // Crosshair widget classes
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Crosshairs")
    TSubclassOf<UGothicCrosshairWidget> Crosshair_Melee_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Crosshairs")
    TSubclassOf<UGothicCrosshairWidget> Crosshair_Pistol_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Crosshairs")
    TSubclassOf<UGothicCrosshairWidget> Crosshair_Rifle_Class;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Crosshairs")
    TSubclassOf<UGothicCrosshairWidget> Crosshair_Throwable_Class;

    /** Assign WBP_QuitMenu here. Unset simply means ToggleQuitMenu does nothing. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Menus")
    TSubclassOf<UGothicQuitMenuWidget> QuitMenuClass;

    /** The live quit menu, or null when it is closed. */
    UPROPERTY(Transient)
    TObjectPtr<UGothicQuitMenuWidget> ActiveQuitMenu;

    /** Assign WBP_ReviveChannelBar here. Unset means the bar is invisible and only
     *  the OnReviveChannel* events fire — the feature still works, it just has no
     *  default presentation until the widget exists. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|HUD|Revive")
    TSubclassOf<UGothicReviveChannelBarWidget> ReviveChannelBarClass;

    /** The live revive bar. Kept across a channel's whole life; the widget removes
     *  ITSELF after its dismiss delay, so this can hold a detached widget briefly. */
    UPROPERTY(Transient)
    TObjectPtr<UGothicReviveChannelBarWidget> ActiveReviveChannelBar;

    float ReviveChannelProgress = 0.f;
    bool bReviveChannelShown = false;

    // -------------------------------------------------------------------------
    // Active widget instances
    // -------------------------------------------------------------------------
    UPROPERTY()
    TObjectPtr<UGothicHUDWidget> ActiveHUDWidget;

    UPROPERTY()
    TObjectPtr<UGothicCrosshairWidget> ActiveCrosshairWidget;

    EGothicHUDLayout CurrentLayout = EGothicHUDLayout::LayoutA;
    EGothicCrosshairType CurrentCrosshairType = EGothicCrosshairType::Melee;

    // Dynamic crosshair spread
    float CurrentSpread = 0.f;
    float TargetSpread  = 0.f;

    /** False while a menu owns the cursor. Survives crosshair swaps. */
    bool bCrosshairVisible = true;

private:
    void CreateAndShowLayout(TSubclassOf<UGothicHUDWidget> WidgetClass);
    void CreateAndShowCrosshair(TSubclassOf<UGothicCrosshairWidget> CrosshairClass);
    void RemoveActiveWidgets();

    // -------------------------------------------------------------------------
    // Damage numbers — tuning (EditDefaultsOnly via BP_GothicHUD)
    // -------------------------------------------------------------------------

    /** How long the number stays visible before fully fading (feel pass: ~0.8s). */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    float DamageNumberDuration = 0.8f;

    /** Pixels per second the number floats upward. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    float DamageNumberFloatSpeed = 60.f;

    /** Body hit text color. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    FLinearColor BodyHitColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

    /** Vital hit text color — gold by default; tunable in BP_GothicHUD. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    FLinearColor VitalHitColor = FLinearColor(1.f, 0.85f, 0.1f, 1.f);

    /** Scale multiplier for vital hit numbers (1.0 = same as body; feel pass: 1.5x). */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|DamageNumbers")
    float VitalHitScale = 1.5f;

    TArray<FGothicDamageNumber> ActiveDamageNumbers;

    // -------------------------------------------------------------------------
    // Enemy health bars — HUD-drawn, replaces WidgetComponent approach
    // -------------------------------------------------------------------------

    /** How long the health bar stays visible after the last hit. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|HealthBars")
    float HealthBarVisibleDuration = 5.f;

    /** Bar width in screen pixels. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|HealthBars")
    float HealthBarWidth = 120.f;

    /** Bar height in screen pixels. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|HealthBars")
    float HealthBarHeight = 10.f;

    /** World-space Z offset above the enemy's location (clears the head). */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|HealthBars")
    float HealthBarWorldZOffset = 120.f;

    /** Background bar color. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|HealthBars")
    FLinearColor HealthBarBackgroundColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.7f);

    /** Health fill color. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|HealthBars")
    FLinearColor HealthBarFillColor = FLinearColor(0.8f, 0.15f, 0.1f, 0.9f);

    /** Max distance from player before the bar stops rendering. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|HUD|HealthBars")
    float HealthBarMaxDistance = 3000.f;

    TArray<FEnemyHealthBarEntry> ActiveHealthBars;

    void DrawEnemyHealthBars();
};