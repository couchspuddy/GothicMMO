// GothicBossAIController_BestialLucid.cpp

#include "AI/AGothicBossAIController_BestialLucid.h"
#include "AI/GothicVitalPointComponent.h"
#include "GameFramework/Pawn.h"

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

void AGothicBossAIController_BestialLucid::OnPhaseAdvance()
{
    Super::OnPhaseAdvance(); // handles the generic bookkeeping, Blackboard write, broadcast

    // Bestial Lucid-specific Phase 2 entry behavior goes here once designed —
    // e.g. triggering the Stillness beat's brief pause, swapping to a
    // faster vital shift timer interval, enabling environment-as-weapon
    // behavior flags on the Blackboard for BT nodes to read.
    //
    // Left as a stub for now — the Stillness beat itself (a moment of
    // absolute stillness before Phase 2 combat resumes) likely needs a
    // short delay/timer here before Phase 2 behavior actually becomes
    // active, rather than an instant cut. Revisit once in-engine testing
    // shows how the transition actually feels.
}