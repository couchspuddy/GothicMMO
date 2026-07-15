// GothicBossAIController_BestialLucid.cpp

#include "AI/AGothicBossAIController_BestialLucid.h"
#include "AI/GothicVitalPointComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/GothicAttributeSet.h"

void AGothicBossAIController_BestialLucid::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (!InPawn)
    {
        return;
    }

    CachedVitalPointComponent = InPawn->FindComponentByClass<UGothicVitalPointComponent>();

    if (CachedVitalPointComponent)
    {
        CachedVitalPointComponent->OnVitalPointShifted.AddDynamic(
            this, &AGothicBossAIController_BestialLucid::HandleVitalPointShifted);

        UE_LOG(LogTemp, Log, TEXT("BestialLucid AI: Bound to vital point shifts on %s"),
            *InPawn->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("BestialLucid AI: No VitalPointComponent found on %s"),
            *InPawn->GetName());
    }

    // Phase 2 now triggers off health, not the vital index.
    CachedASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InPawn);
    if (CachedASC)
    {
        HealthChangedHandle = CachedASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetHealthAttribute())
            .AddUObject(this, &AGothicBossAIController_BestialLucid::HandleHealthChanged);

        UE_LOG(LogTemp, Log, TEXT("BestialLucid AI: Bound to health changes on %s — Phase 2 at %.0f%%"),
            *InPawn->GetName(), Phase2HealthThreshold * 100.f);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("BestialLucid AI: No ASC on %s — Phase 2 can never fire"),
            *InPawn->GetName());
    }

    UGameplayStatics::GetAllActorsWithTag(GetWorld(), DestructibleZoneTag, DestructibleZones);

    UE_LOG(LogTemp, Log, TEXT("BestialLucid AI: Found %d destructible zones tagged '%s'"),
        DestructibleZones.Num(), *DestructibleZoneTag.ToString());
}

void AGothicBossAIController_BestialLucid::OnUnPossess()
{
    if (CachedVitalPointComponent)
    {
        CachedVitalPointComponent->OnVitalPointShifted.RemoveDynamic(
            this, &AGothicBossAIController_BestialLucid::HandleVitalPointShifted);
        CachedVitalPointComponent = nullptr;
    }

    if (CachedASC && HealthChangedHandle.IsValid())
    {
        CachedASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
        HealthChangedHandle.Reset();
    }
    CachedASC = nullptr;

    if (ZoneCollapseTimerHandle.IsValid())
    {
        GetWorldTimerManager().ClearTimer(ZoneCollapseTimerHandle);
    }

    Super::OnUnPossess();
}

void AGothicBossAIController_BestialLucid::HandleVitalPointShifted(int32 NewIndex, FVector NewWorldLocation)
{
    // No longer drives the phase — that's HandleHealthChanged's job. Kept bound
    // because a Phase 1 reaction to her own vital shifting is a plausible beat.
}

// AGothicBossAIController_BestialLucid.cpp — replace the stub OnPhaseAdvance() body
void AGothicBossAIController_BestialLucid::OnPhaseAdvance()
{
    Super::OnPhaseAdvance(); // generic bookkeeping, Blackboard write, broadcast

    // Vital Point Freeze — per design doc, Phase 2's deliberate inversion:
    // the vital becomes a fixed, known target the moment the fight gets harder.
    if (CachedVitalPointComponent)
    {
        CachedVitalPointComponent->FreezeVitalPoint(Phase2LockedVitalIndex);
    }

    // Timed Ceiling Collapse — starts only if we actually found tagged zones.
    if (DestructibleZones.Num() > 0)
    {
        GetWorldTimerManager().SetTimer(
            ZoneCollapseTimerHandle,
            this,
            &AGothicBossAIController_BestialLucid::HandleZoneCollapseTimer,
            ZoneCollapseInterval,
            true); // looping
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("BestialLucid AI: Phase 2 entered but no destructible zones found — tag debris actors '%s' in the level"),
            *DestructibleZoneTag.ToString());
    }
}

void AGothicBossAIController_BestialLucid::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
    // The phase guard is what stops this re-firing on every damage tick below 50%.
    if (GetCurrentPhase() != 1 || !CachedASC)
    {
        return;
    }

    const float MaxHealth = CachedASC->GetNumericAttribute(UGothicAttributeSet::GetMaxHealthAttribute());
    if (MaxHealth <= 0.f)
    {
        return;
    }

    const float Fraction = Data.NewValue / MaxHealth;
    if (Fraction <= Phase2HealthThreshold)
    {
        UE_LOG(LogTemp, Log, TEXT("BestialLucid AI: Health %.1f/%.1f (%.0f%%) crossed %.0f%% — advancing phase"),
            Data.NewValue, MaxHealth, Fraction * 100.f, Phase2HealthThreshold * 100.f);

        OnPhaseAdvance();
    }
}

// AGothicBossAIController_BestialLucid.cpp — new function
void AGothicBossAIController_BestialLucid::HandleZoneCollapseTimer()
{
    if (DestructibleZones.Num() == 0)
    {
        return; // shouldn't happen — timer only starts if zones exist — but cheap to guard
    }

    const int32 ZoneIndex = NextZoneCollapseIndex;
    NextZoneCollapseIndex = (NextZoneCollapseIndex + 1) % DestructibleZones.Num();

    AActor* ZoneActor = DestructibleZones[ZoneIndex];

    UE_LOG(LogTemp, Log, TEXT("BestialLucid AI: Triggering zone collapse — index %d (%s)"),
        ZoneIndex, ZoneActor ? *ZoneActor->GetName() : TEXT("NULL"));

    TriggerZoneCollapse(ZoneIndex, ZoneActor);
}