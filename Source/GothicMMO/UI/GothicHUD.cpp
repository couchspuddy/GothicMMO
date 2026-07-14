// GothicHUD.cpp

#include "UI/GothicHUD.h"
#include "UI/GothicHUDWidget.h"
#include "UI/GothicCrosshairWidget.h"
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

    // Crosshair enemy detection — line trace from center screen
    if (ActiveCrosshairWidget && GetOwningPlayerController())
    {
        FVector WorldLocation, WorldDirection;
        if (GetOwningPlayerController()->DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
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