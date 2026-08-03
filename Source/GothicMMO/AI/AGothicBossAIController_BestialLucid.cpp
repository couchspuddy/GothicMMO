// GothicBossAIController_BestialLucid.cpp

#include "AI/AGothicBossAIController_BestialLucid.h"
#include "AI/GothicVitalPointComponent.h"
#include "AI/GothicEnemyAIController.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "BehaviorTree/BlackboardComponent.h"

AGothicBossAIController_BestialLucid::AGothicBossAIController_BestialLucid()
{
    // ARENA LEASH. She is bound to the Rotunda; she does not follow you out.
    //
    // 3200 is derived from the room, not from play, and has not been tuned
    // against a live fight:
    //   - the pillar rectangle spans (14360..17910, -2130..2230), centre
    //     (16350, 40), half-diagonal ~2970uu. 3200 covers the whole arena
    //     including the corners, with ~230uu of slack.
    //   - the PlayerStart she was measured chasing to sits at (12905, 0),
    //     ~3450uu from that centre. 3200 excludes it by ~250uu, so a player who
    //     runs for the door or dies and respawns is outside the leash.
    // Those two numbers are 480uu apart in total, so this is a narrow band. If
    // the arena bounds or the PlayerStart move, this number has to be rederived
    // rather than nudged.
    //
    // Replaces the LeashRange 10000 that BP_BossAIController_BestialLucid
    // carried, which put both the arena and the PlayerStart comfortably inside
    // the leash and made every disengage check pass. The machinery was never
    // broken; the radius was.
    //
    // Set here rather than left to the base 3000 default so the boss's number is
    // data on the boss class. Base enemies keep 3000.
    LeashRange = 3200.f;

    // The arena half of the break condition: she gives up when the PLAYER
    // leaves the radius, not only when she has already been dragged 3200uu out
    // of her own room. Base default is already true — restated here because it
    // is load-bearing for an arena boss and must not be quietly flipped off on
    // this class.
    bLeashOnTargetDistance = true;
}

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

void AGothicBossAIController_BestialLucid::OnLeashReset()
{
    Super::OnLeashReset(); // health restore + CurrentPhase back to 1

    // Order matters only in that all three must happen. The guard is the one
    // that silently breaks the fight: leave it set and health can cross the
    // threshold again on the next pull without HandleHealthChanged reacting,
    // so she stays in Phase 1 forever with no transition and no vital lock.
    bTransitionTriggered = false;

    if (Blackboard && TransitionPendingBlackboardKey != NAME_None)
    {
        Blackboard->SetValueAsBool(TransitionPendingBlackboardKey, false);
    }

    if (CachedVitalPointComponent)
    {
        CachedVitalPointComponent->UnfreezeVitalPoint();
    }
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