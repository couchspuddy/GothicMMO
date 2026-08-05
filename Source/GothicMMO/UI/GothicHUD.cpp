// GothicHUD.cpp

#include "UI/GothicHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UI/GothicHUDWidget.h"
#include "UI/GothicCrosshairWidget.h"
#include "UI/GothicQuitMenuWidget.h"
#include "UI/GothicHintManagerComponent.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "AI/GothicEnemyBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

AGothicHUD::AGothicHUD()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AGothicHUD::BeginPlay()
{
    Super::BeginPlay();

    // Start with Layout A and melee crosshair by default.
    // Player can switch layouts from settings later.
    SetActiveLayout(EGothicHUDLayout::LayoutA);
    SetCrosshairType(EGothicCrosshairType::Melee);
}

void AGothicHUD::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Nothing to drive while a menu owns the cursor — the crosshair is hidden and
    // a center-screen trace would be meaningless anyway.
    if (!bCrosshairVisible)
    {
        return;
    }

    // Dynamic crosshair spread based on player movement speed.
    if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwningPawn()))
    {
        const float Speed    = OwnerChar->GetCharacterMovement()->Velocity.Size();
        const float MaxSpeed = OwnerChar->GetCharacterMovement()->MaxWalkSpeed;

        // Normalize speed to 0-1 range
        TargetSpread = FMath::Clamp(Speed / MaxSpeed, 0.f, 1.f);
    }

    // Smooth spread interpolation — tightens faster than it spreads
    const float InterpSpeed = (CurrentSpread > TargetSpread) ? 8.f : 4.f;
    CurrentSpread = FMath::FInterpTo(CurrentSpread, TargetSpread, DeltaTime, InterpSpeed);

    // Push spread to active crosshair widget
    if (ActiveCrosshairWidget)
    {
        ActiveCrosshairWidget->OnSpreadUpdated(CurrentSpread);
    }

    // Crosshair enemy detection — line trace from center screen.
    // Deprojects the viewport center rather than the mouse: the crosshair is pinned
    // to center, and the cursor is free-floating whenever a menu is open.
    if (ActiveCrosshairWidget && GetOwningPlayerController())
    {
        // Zero-initialised on purpose: FVector2D's default constructor leaves the
        // components undefined, and if GameViewport is null nothing below writes
        // them before the ViewportSize.X > 0 guard reads them. The guard cannot
        // protect against that -- performing the read IS the undefined behaviour,
        // and in practice it meant a null viewport could pass the check on
        // whatever happened to be on the stack.
        FVector2D ViewportSize = FVector2D::ZeroVector;
        if (GEngine && GEngine->GameViewport)
        {
            GEngine->GameViewport->GetViewportSize(ViewportSize);
        }

        FVector WorldLocation, WorldDirection;
        if (ViewportSize.X > 0.f &&
            GetOwningPlayerController()->DeprojectScreenPositionToWorld(
                ViewportSize.X * 0.5f, ViewportSize.Y * 0.5f, WorldLocation, WorldDirection))
        {
            // Short trace to detect enemies in crosshair
            FHitResult Hit;
            FVector TraceEnd = WorldLocation + (WorldDirection * 2000.f);
            FCollisionQueryParams Params;
            Params.AddIgnoredActor(GetOwningPawn());

            bool bHitEnemy = false;
            if (GetWorld()->LineTraceSingleByChannel(Hit, WorldLocation, TraceEnd, ECC_Pawn, Params))
            {
                // Check if hit actor has an ASC (is a combat participant)
                if (Hit.GetActor() && Hit.GetActor() != GetOwningPawn())
                {
                    bHitEnemy = true;
                }
            }
            ActiveCrosshairWidget->OnEnemyDetected(bHitEnemy);
        }
    }
}

void AGothicHUD::SetActiveLayout(EGothicHUDLayout NewLayout)
{
    CurrentLayout = NewLayout;

    switch (NewLayout)
    {
        case EGothicHUDLayout::LayoutA:
            CreateAndShowLayout(LayoutA_Class);
            break;
        case EGothicHUDLayout::LayoutC:
            CreateAndShowLayout(LayoutC_Class);
            break;
        case EGothicHUDLayout::LayoutF:
            CreateAndShowLayout(LayoutF_Class);
            break;
    }
}

void AGothicHUD::SetCrosshairType(EGothicCrosshairType NewType)
{
    CurrentCrosshairType = NewType;

    switch (NewType)
    {
        case EGothicCrosshairType::Melee:
            CreateAndShowCrosshair(Crosshair_Melee_Class);
            break;
        case EGothicCrosshairType::Pistol:
            CreateAndShowCrosshair(Crosshair_Pistol_Class);
            break;
        case EGothicCrosshairType::Rifle:
            CreateAndShowCrosshair(Crosshair_Rifle_Class);
            break;
        case EGothicCrosshairType::Throwable:
            CreateAndShowCrosshair(Crosshair_Throwable_Class);
            break;
    }
}

void AGothicHUD::NotifyAimDownSights(bool bIsAiming)
{
    if (ActiveCrosshairWidget)
    {
        ActiveCrosshairWidget->OnAimDownSights(bIsAiming);
    }
}

void AGothicHUD::SetCrosshairVisible(bool bVisible)
{
    bCrosshairVisible = bVisible;

    if (ActiveCrosshairWidget)
    {
        ActiveCrosshairWidget->SetVisibility(
            bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }

    // Reset spread so reopening doesn't snap from a stale value
    if (!bVisible)
    {
        CurrentSpread = 0.f;
        TargetSpread  = 0.f;
    }
}

void AGothicHUD::UpdateHealth(float CurrentHealth, float MaxHealth)
{
    if (ActiveHUDWidget)
    {
        ActiveHUDWidget->CachedHealth    = CurrentHealth;
        ActiveHUDWidget->CachedMaxHealth = MaxHealth;
        ActiveHUDWidget->OnHealthChanged(CurrentHealth, MaxHealth);
    }
}

void AGothicHUD::UpdateSuperMeter(float CurrentValue, float MaxValue)
{
    if (ActiveHUDWidget)
    {
        const float Normalized = MaxValue > 0.f ? CurrentValue / MaxValue : 0.f;
        ActiveHUDWidget->CachedSuperMeter = Normalized;
        ActiveHUDWidget->OnSuperMeterChanged(CurrentValue, MaxValue);

        // Notify when super is ready
        if (Normalized >= 1.f)
        {
            ActiveHUDWidget->OnSuperReady();
        }
    }
}

void AGothicHUD::UpdateSteadfast(float CurrentValue, float MaxValue)
{
    if (ActiveHUDWidget)
    {
        ActiveHUDWidget->CachedSteadfast = MaxValue > 0.f ? CurrentValue / MaxValue : 0.f;
        ActiveHUDWidget->OnSteadfastChanged(CurrentValue, MaxValue);
    }
}

void AGothicHUD::UpdateAbilityCooldown(EGothicAbilitySlot SlotIndex, float CooldownRemaining, float CooldownTotal)
{
    if (ActiveHUDWidget)
    {
        // Drive the cooldown overlay bars directly (C++ BindWidget), then fire the
        // BP event for any additional layout-specific flourishes.
        ActiveHUDWidget->SetAbilityCooldownDisplay(SlotIndex, CooldownRemaining, CooldownTotal);
        ActiveHUDWidget->OnAbilityCooldownChanged(SlotIndex, CooldownRemaining, CooldownTotal);

        // Notify when ability fully recharged
        if (CooldownRemaining <= 0.f)
        {
            ActiveHUDWidget->OnAbilityRecharged(SlotIndex);
        }
    }
}

void AGothicHUD::ShowDamageIndicator(float DamageAmount)
{
    if (ActiveHUDWidget)
    {
        ActiveHUDWidget->OnDamageReceived(DamageAmount);
    }
}

void AGothicHUD::CreateAndShowLayout(TSubclassOf<UGothicHUDWidget> WidgetClass)
{
    if (!WidgetClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("AGothicHUD: Widget class not assigned. Assign in BP_GothicHUD."));
        return;
    }

    // Remove existing HUD widget
    if (ActiveHUDWidget)
    {
        ActiveHUDWidget->RemoveFromParent();
        ActiveHUDWidget = nullptr;
    }

    ActiveHUDWidget = CreateWidget<UGothicHUDWidget>(GetOwningPlayerController(), WidgetClass);
    if (ActiveHUDWidget)
    {
        ActiveHUDWidget->AddToViewport(0);  // Z-order 0 — behind crosshair
    }
}

void AGothicHUD::UpdateSelah(float CurrentSelah)
{

    if (ActiveHUDWidget)
    {
        ActiveHUDWidget->OnSelahChanged(FMath::FloorToInt(CurrentSelah));
    }
}

void AGothicHUD::CreateAndShowCrosshair(TSubclassOf<UGothicCrosshairWidget> CrosshairClass)
{

    if (!CrosshairClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("CrosshairClass is NULL - check BP_GothicHUD Class Defaults"));
        return;
    }

    if (ActiveCrosshairWidget)
    {
        ActiveCrosshairWidget->RemoveFromParent();
        ActiveCrosshairWidget = nullptr;
    }

    ActiveCrosshairWidget = CreateWidget<UGothicCrosshairWidget>(
        GetOwningPlayerController(), CrosshairClass);


    if (ActiveCrosshairWidget)
    {
        ActiveCrosshairWidget->AddToViewport(1);

        // A weapon swap can happen while a menu is open — don't resurrect a hidden crosshair
        if (!bCrosshairVisible)
        {
            ActiveCrosshairWidget->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

void AGothicHUD::RemoveActiveWidgets()
{
    if (ActiveHUDWidget)
    {
        ActiveHUDWidget->RemoveFromParent();
        ActiveHUDWidget = nullptr;
    }
    if (ActiveCrosshairWidget)
    {
        ActiveCrosshairWidget->RemoveFromParent();
        ActiveCrosshairWidget = nullptr;
    }
}

// AGothicHUD.cpp — new function, same passthrough-to-widgets pattern as UpdateHealth/UpdateSelah
void AGothicHUD::NotifyOwningPawnChanged(APawn* NewPawn)
{
    if (ActiveHUDWidget)
    {
        ActiveHUDWidget->OnOwningPawnChanged(NewPawn);
    }

    if (ActiveCrosshairWidget)
    {
        ActiveCrosshairWidget->OnOwningPawnChanged(NewPawn);
    }

}

void AGothicHUD::UpdateAmmo(int32 Magazine, int32 MagCapacity, int32 Reserve, int32 MaxReserve)
{
    if (!ActiveHUDWidget)
    {
        return;
    }
    

    ActiveHUDWidget->CachedMagazineAmmo     = Magazine;
    ActiveHUDWidget->CachedMagazineCapacity = MagCapacity;
    ActiveHUDWidget->CachedReserveAmmo      = Reserve;
    ActiveHUDWidget->CachedMaxReserveAmmo   = MaxReserve;

    ActiveHUDWidget->OnAmmoChanged(Magazine, MagCapacity, Reserve, MaxReserve);

    if (Magazine == 0)
    {
        ActiveHUDWidget->OnMagazineEmpty();
    }
}

void AGothicHUD::ShowDamageNumber(FVector WorldLocation, float DamageAmount, bool bWasVital)
{
    FGothicDamageNumber NewNumber;
    NewNumber.WorldLocation = WorldLocation;
    NewNumber.DamageAmount  = DamageAmount;
    NewNumber.bWasVital     = bWasVital;
    NewNumber.SpawnTime     = GetWorld()->GetTimeSeconds();
    // Random horizontal offset so rapid hits don't stack on top of each other
    NewNumber.RandomOffsetX = FMath::RandRange(-20.f, 20.f);
    ActiveDamageNumbers.Add(NewNumber);
}

void AGothicHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas) return;

    const float CurrentTime = GetWorld()->GetTimeSeconds();
    const UFont* DrawFont   = GEngine->GetLargeFont();

    for (int32 i = ActiveDamageNumbers.Num() - 1; i >= 0; --i)
    {
        FGothicDamageNumber& Num = ActiveDamageNumbers[i];
        const float Elapsed = CurrentTime - Num.SpawnTime;

        if (Elapsed >= DamageNumberDuration)
        {
            ActiveDamageNumbers.RemoveAt(i);
            continue;
        }

        // Fade: full opacity for first 30%, then linear fade
        const float FadeStart = DamageNumberDuration * 0.3f;
        const float Alpha = (Elapsed < FadeStart)
            ? 1.f
            : 1.f - ((Elapsed - FadeStart) / (DamageNumberDuration - FadeStart));

        const float FloatOffset = Elapsed * DamageNumberFloatSpeed;

        const FVector ScreenPos = Project(Num.WorldLocation);

        // Skip if projected behind the camera or off screen
        if (ScreenPos.X < 0.f || ScreenPos.X > Canvas->SizeX ||
            ScreenPos.Y < 0.f || ScreenPos.Y > Canvas->SizeY)
        {
            continue;
        }

        FLinearColor Color = Num.bWasVital ? VitalHitColor : BodyHitColor;
        Color.A = Alpha;

        const FString DamageText = FString::Printf(TEXT("%.0f"), Num.DamageAmount);

        FCanvasTextItem TextItem(
            FVector2D(ScreenPos.X + Num.RandomOffsetX, ScreenPos.Y - FloatOffset),
            FText::FromString(DamageText),
            DrawFont,
            Color.ToFColor(true));

        TextItem.bOutlined   = true;
        TextItem.OutlineColor = FLinearColor(0.f, 0.f, 0.f, Alpha * 0.8f);
        TextItem.bCentreX    = true;

        if (Num.bWasVital)
        {
            TextItem.Scale = FVector2D(VitalHitScale, VitalHitScale);
        }

        Canvas->DrawItem(TextItem);
    }

    DrawEnemyHealthBars();
    DrawInteractPrompt();
    DrawTutorialHint();
}

void AGothicHUD::SetInteractPrompt(AActor* Interactable, const FText& PromptText)
{
    // Record who raised it so ClearInteractPrompt can reject a stale clear
    // arriving from an interactable the player has already left.
    InteractPromptText  = PromptText;
    InteractPromptOwner = Interactable;

    // First prompt of the session teaches the key. Hooked here rather than in any
    // one interactable because every interactable in the game funnels through
    // this call — including the Blueprint ones, which a C++-side hook in the
    // player pawn would never see.
    if (APawn* Pawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr)
    {
        if (UGothicHintManagerComponent* Hints =
                Pawn->FindComponentByClass<UGothicHintManagerComponent>())
        {
            Hints->ShowHint(GothicTags::Hint_Interact);
        }
    }
}

void AGothicHUD::SetTutorialHint(const FText& HintText)
{
    TutorialHintText = HintText;
}

void AGothicHUD::ClearTutorialHint()
{
    TutorialHintText = FText::GetEmpty();
}

void AGothicHUD::DrawTutorialHint()
{
    if (!Canvas || TutorialHintText.IsEmpty())
    {
        return;
    }

    const UFont* DrawFont = GEngine->GetLargeFont();

    // No key-hint prefix wrapper here, unlike the interact prompt. Hint copy names
    // its own key inline ("Hold R — convert Steadfast to ammo") because the key
    // is part of the sentence, not a bracket in front of it.
    FCanvasTextItem TextItem(
        FVector2D(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f + TutorialHintOffsetY),
        TutorialHintText,
        DrawFont,
        TutorialHintColor.ToFColor(true));

    TextItem.bCentreX     = true;
    TextItem.bOutlined    = true;
    TextItem.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.85f);
    TextItem.Scale        = FVector2D(TutorialHintScale, TutorialHintScale);

    Canvas->DrawItem(TextItem);
}

void AGothicHUD::ClearInteractPrompt(AActor* Requester)
{
    // Only the actor that raised the prompt may clear it. Without this, walking
    // from one interactable straight into another blanks the new prompt, because
    // the second actor's BeginOverlap fires before the first actor's EndOverlap.
    if (Requester && InteractPromptOwner.IsValid() && InteractPromptOwner.Get() != Requester)
    {
        return;
    }

    InteractPromptText = FText::GetEmpty();
    InteractPromptOwner = nullptr;
}

void AGothicHUD::DrawInteractPrompt()
{
    if (!Canvas || InteractPromptText.IsEmpty())
    {
        return;
    }

    const UFont* DrawFont = GEngine->GetLargeFont();

    const FString Composed = InteractKeyHint.IsEmpty()
        ? InteractPromptText.ToString()
        : FString::Printf(TEXT("[%s]   %s"), *InteractKeyHint, *InteractPromptText.ToString());

    FCanvasTextItem TextItem(
        FVector2D(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f + InteractPromptOffsetY),
        FText::FromString(Composed),
        DrawFont,
        InteractPromptColor.ToFColor(true));

    TextItem.bCentreX     = true;
    TextItem.bOutlined    = true;
    TextItem.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.85f);
    TextItem.Scale        = FVector2D(InteractPromptScale, InteractPromptScale);

    Canvas->DrawItem(TextItem);
}

void AGothicHUD::RegisterEnemyHealthBar(AGothicEnemyBase* Enemy)
{
    if (!Enemy) return;

    // Update existing entry if already tracked
    for (FEnemyHealthBarEntry& Entry : ActiveHealthBars)
    {
        if (Entry.Enemy.Get() == Enemy)
        {
            Entry.LastHitTime = GetWorld()->GetTimeSeconds();
            return;
        }
    }

    // New entry
    FEnemyHealthBarEntry NewEntry;
    NewEntry.Enemy = Enemy;
    NewEntry.LastHitTime = GetWorld()->GetTimeSeconds();
    ActiveHealthBars.Add(NewEntry);
}

void AGothicHUD::DrawEnemyHealthBars()
{
    const float CurrentTime = GetWorld()->GetTimeSeconds();
    const APawn* ViewPawn = GetOwningPawn();
    if (!ViewPawn) return;

    const FVector ViewLocation = ViewPawn->GetActorLocation();

    for (int32 i = ActiveHealthBars.Num() - 1; i >= 0; --i)
    {
        FEnemyHealthBarEntry& Entry = ActiveHealthBars[i];

        // Remove dead weak pointers and expired entries
        AGothicEnemyBase* Enemy = Entry.Enemy.Get();
        if (!Enemy || !Enemy->IsAlive() ||
            (CurrentTime - Entry.LastHitTime) > HealthBarVisibleDuration)
        {
            ActiveHealthBars.RemoveAt(i);
            continue;
        }

        // Distance cull
        const float Distance = FVector::Dist(ViewLocation, Enemy->GetActorLocation());
        if (Distance > HealthBarMaxDistance)
        {
            continue;
        }

        // Project enemy position to screen, offset above head
        const FVector WorldPos = Enemy->GetActorLocation() +
            FVector(0.f, 0.f, HealthBarWorldZOffset);
        const FVector ScreenPos = Project(WorldPos);

        // Off screen check
        if (ScreenPos.X < -HealthBarWidth || ScreenPos.X > Canvas->SizeX + HealthBarWidth ||
            ScreenPos.Y < -HealthBarHeight || ScreenPos.Y > Canvas->SizeY + HealthBarHeight)
        {
            continue;
        }

        // Health percentage
        const float MaxHP = Enemy->GetMaxHealth();
        const float HealthPct = (MaxHP > 0.f) ? FMath::Clamp(Enemy->GetHealth() / MaxHP, 0.f, 1.f) : 0.f;

        // Center the bar horizontally on the projected point
        const float BarX = ScreenPos.X - (HealthBarWidth * 0.5f);
        const float BarY = ScreenPos.Y;

        // Background
        FCanvasTileItem Background(
            FVector2D(BarX, BarY),
            FVector2D(HealthBarWidth, HealthBarHeight),
            HealthBarBackgroundColor.ToFColor(true));
        Background.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Background);

        // Health fill
        if (HealthPct > 0.f)
        {
            FCanvasTileItem Fill(
                FVector2D(BarX, BarY),
                FVector2D(HealthBarWidth * HealthPct, HealthBarHeight),
                HealthBarFillColor.ToFColor(true));
            Fill.BlendMode = SE_BLEND_Translucent;
            Canvas->DrawItem(Fill);
        }

        // NO NAME HERE, deliberately.
        //
        // The Accursed's name belongs to the Selah moment and nowhere else. Reading
        // it off a health bar mid-fight spends the reveal before it happens: the
        // whole point is that you learn who they were AFTER you killed them, and a
        // nameplate you shot at for thirty seconds is not a revelation.
        //
        // The names are cycled by UGothicSelahPromptWidget::StartNameCycle, driven
        // from AGothicGameState::OnSelahMomentStarted. This canvas draw was the real
        // source of in-combat names — WBP_EnemyHealthBar's binding was a second,
        // unused path, so removing the widget node changed nothing on screen.
    }
}

void AGothicHUD::ToggleQuitMenu()
{
    // Already up -> this is a "close" press. Route through DismissMenu so input
    // mode and the cursor are restored the same way the Resume button does it.
    if (ActiveQuitMenu)
    {
        ActiveQuitMenu->DismissMenu();
        ActiveQuitMenu = nullptr;
        return;
    }

    if (!QuitMenuClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicHUD: QuitMenuClass is unset — assign WBP_QuitMenu on BP_GothicHUD or "
                 "the quit screen can never open."));
        return;
    }

    APlayerController* PC = GetOwningPlayerController();
    if (!PC)
    {
        return;
    }

    ActiveQuitMenu = CreateWidget<UGothicQuitMenuWidget>(PC, QuitMenuClass);
    if (ActiveQuitMenu)
    {
        // High Z so it sits over the HUD and any open inventory rather than
        // under them -- a confirmation the player cannot see is worse than none.
        ActiveQuitMenu->AddToViewport(1000);
    }
}
