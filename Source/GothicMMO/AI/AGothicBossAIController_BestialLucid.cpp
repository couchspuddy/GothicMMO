// GothicBossAIController_BestialLucid.cpp

#include "AI/AGothicBossAIController_BestialLucid.h"
#include "AI/GothicVitalPointComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"

void AGothicBossAIController_BestialLucid::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (InPawn)
    {
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
        // AGothicBossAIController_BestialLucid.cpp — extend OnPossess, after the existing
        // vital point binding block, before the closing brace of the outer if(InPawn)
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), DestructibleZoneTag, DestructibleZones);

        UE_LOG(LogTemp, Log, TEXT("BestialLucid AI: Found %d destructible zones tagged '%s'"),
            DestructibleZones.Num(), *DestructibleZoneTag.ToString());
    }
}

void AGothicBossAIController_BestialLucid::OnUnPossess()
{
    if (CachedVitalPointComponent)
    {
        CachedVitalPointComponent->OnVitalPointShifted.RemoveDynamic(
            this, &AGothicBossAIController_BestialLucid::HandleVitalPointShifted);
        CachedVitalPointComponent = nullptr;
    }
    // AGothicBossAIController_BestialLucid.cpp — extend OnUnPossess, before Super::OnUnPossess()
    if (ZoneCollapseTimerHandle.IsValid())
    {
        GetWorldTimerManager().ClearTimer(ZoneCollapseTimerHandle);
    }
    Super::OnUnPossess();
}

void AGothicBossAIController_BestialLucid::HandleVitalPointShifted(int32 NewIndex, FVector NewWorldLocation)
{
    // Only trigger the phase advance once, on reaching the designated index,
    // and only from Phase 1 -> Phase 2 (guard against re-triggering if the
    // vital later cycles back through this index in Phase 2's faster shifting).
    if (GetCurrentPhase() == 1 && NewIndex == Phase2TriggerVitalIndex)
    {
        UE_LOG(LogTemp, Log, TEXT("BestialLucid AI: Vital reached trigger index %d — advancing phase"),
            NewIndex);

        OnPhaseAdvance();
    }
}

// AGothicBossAIController_BestialLucid.cpp — replace the stub OnPhaseAdvance() body
void AGothicBossAIController_BestialLucid::OnPhaseAdvance()
{
    Super::OnPhaseAdvance(); // generic bookkeeping, Blackboard write, broadcast

    // Vital Point Freeze — per design doc, Phase 2's deliberate inversion:
    // the vital becomes a fixed, known target the moment the fight gets harder.
    if (CachedVitalPointComponent)
    {
        CachedVitalPointComponent->FreezeVitalPoint();
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