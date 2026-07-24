// GothicBossAIController_BestialLucid.cpp

#include "AI/AGothicBossAIController_BestialLucid.h"
#include "AI/GothicVitalPointComponent.h"
#include "AI/GothicEnemyAIController.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "BehaviorTree/BlackboardComponent.h"

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

    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("BestialLucid AI: No VitalPointComponent found on %s"),
            *InPawn->GetName());
    }

    CachedASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InPawn);
    if (CachedASC)
    {
        HealthChangedHandle = CachedASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetHealthAttribute())
            .AddUObject(this, &AGothicBossAIController_BestialLucid::HandleHealthChanged);

    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("BestialLucid AI: No ASC on %s -- Phase 2 can never fire"),
            *InPawn->GetName());
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

    if (CachedASC && HealthChangedHandle.IsValid())
    {
        CachedASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
        HealthChangedHandle.Reset();
    }
    CachedASC = nullptr;

    Super::OnUnPossess();
}

void AGothicBossAIController_BestialLucid::HandleVitalPointShifted(int32 NewIndex, FVector NewWorldLocation)
{
    // No longer drives the phase -- that's HandleHealthChanged's job. Kept bound
    // because a Phase 1 reaction to her own vital shifting is a plausible beat.
}

void AGothicBossAIController_BestialLucid::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
    // Guards on bTransitionTriggered, NOT GetCurrentPhase() -- CurrentPhase
    // deliberately doesn't flip until the scripted beat (Cry, forced move to
    // a pillar, forced Wall Pound) finishes, but health keeps changing during
    // that beat as more hits land. Without this separate flag, every one of
    // those hits would re-enter this branch and try to start the transition
    // again mid-sequence.
    if (bTransitionTriggered || !CachedASC)
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

        bTransitionTriggered = true;

        if (Blackboard && TransitionPendingBlackboardKey != NAME_None)
        {
            Blackboard->SetValueAsBool(TransitionPendingBlackboardKey, true);
        }
        else
        {
            UE_LOG(LogTemp, Error,
                TEXT("BestialLucid AI: TransitionPendingBlackboardKey not set or Blackboard invalid -- ")
                TEXT("the scripted transition can never start. Add '%s' as a Bool key on BB_BestialLucid."),
                *TransitionPendingBlackboardKey.ToString());
        }
    }
}

void AGothicBossAIController_BestialLucid::CompletePhase2Transition()
{
    if (Blackboard && TransitionPendingBlackboardKey != NAME_None)
    {
        Blackboard->SetValueAsBool(TransitionPendingBlackboardKey, false);
    }

    OnPhaseAdvance(); // generic bookkeeping (Blackboard write, broadcast) + vital freeze below
}

void AGothicBossAIController_BestialLucid::OnPhaseAdvance()
{
    Super::OnPhaseAdvance(); // generic bookkeeping, Blackboard write, broadcast

    // Vital Point Freeze -- per design doc, Phase 2's deliberate inversion:
    // the vital becomes a fixed, known target the moment the fight gets harder.
    // Now fires here, at the END of the scripted transition beat, rather than
    // the instant health crossed the threshold -- matches the vital lock
    // actually landing right when the player sees Phase 2 begin, not several
    // seconds earlier while Cry/the pillar walk/Wall Pound are still playing out.
    if (CachedVitalPointComponent)
    {
        CachedVitalPointComponent->FreezeVitalPoint(Phase2LockedVitalIndex);
    }

    // Zone Collapse timer retired this pass -- superseded by Wall Pound, which
    // does the same job but is player-influenceable rather than firing on an
    // unconditional clock regardless of positioning.
}