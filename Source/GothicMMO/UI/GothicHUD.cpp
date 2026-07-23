// GothicHUD.cpp

#include "UI/GothicHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UI/GothicHUDWidget.h"
#include "UI/GothicCrosshairWidget.h"
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
        FVector2D ViewportSize;
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
    
    GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange,
        FString::Printf(TEXT("UpdateHealth called: %.1f / %.1f | Widget: %s"),
            CurrentHealth, MaxHealth,
            ActiveHUDWidget ? TEXT("Valid") : TEXT("NULL")));
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

void AGothicHUD::UpdateAbilityCooldown(EGothicAbilitySlot SlotIndex, float CooldownRemaining, float CooldownTotal)
{
    if (ActiveHUDWidget)
    {
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
    UE_LOG(LogTemp, Log, TEXT("GothicHUD: UpdateSelah called — %.0f"), CurrentSelah);

    if (ActiveHUDWidget)
    {
        ActiveHUDWidget->OnSelahChanged(FMath::FloorToInt(CurrentSelah));
    }
}

void AGothicHUD::CreateAndShowCrosshair(TSubclassOf<UGothicCrosshairWidget> CrosshairClass)
{
    UE_LOG(LogTemp, Log, TEXT("CreateAndShowCrosshair called - Class: %s"),
        CrosshairClass ? *CrosshairClass->GetName() : TEXT("NULL"));

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

    UE_LOG(LogTemp, Log, TEXT("Crosshair widget created: %s"),
        ActiveCrosshairWidget ? TEXT("Success") : TEXT("Failed"));

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

    UE_LOG(LogTemp, Log, TEXT("AGothicHUD: Notified widgets of owning pawn change — %s"),
        NewPawn ? *NewPawn->GetName() : TEXT("NULL"));
}

void AGothicHUD::UpdateAmmo(int32 Magazine, int32 MagCapacity, int32 Reserve, int32 MaxReserve)
{
    if (!ActiveHUDWidget)
    {
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("UpdateAmmo: Mag=%d/%d Reserve=%d/%d | Widget=%s"),
    Magazine, MagCapacity, Reserve, MaxReserve,
    ActiveHUDWidget ? *ActiveHUDWidget->GetClass()->GetName() : TEXT("NULL"));

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

        // Name above the bar
        const FText EnemyName = Enemy->GetAccursedName();
        if (!EnemyName.IsEmpty())
        {
            FCanvasTextItem NameText(
                FVector2D(ScreenPos.X, BarY - 18.f),
                EnemyName,
                GEngine->GetSmallFont(),
                FColor(200, 200, 200, 200));
            NameText.bCentreX = true;
            NameText.bOutlined = true;
            NameText.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);
            Canvas->DrawItem(NameText);
        }
    }
}