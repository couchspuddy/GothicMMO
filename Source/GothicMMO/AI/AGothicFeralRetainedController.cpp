// AGothicFeralRetainedController.cpp

#include "AI/AGothicFeralRetainedController.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AI/GA_FeralBreakout.h"
#include "Abilities/GameplayAbility.h"
#include "BrainComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
    /** Named so nothing else's RestartLogic can resume the leap by accident. */
    const TCHAR* GLeapPauseReason = TEXT("FeralBreakoutLeap");
}

void AGothicFeralRetainedController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (!InPawn)
    {
        return;
    }

    CachedASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InPawn);
    if (CachedASC)
    {
        HealthChangedHandle = CachedASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetHealthAttribute())
            .AddUObject(this, &AGothicFeralRetainedController::HandleHealthChanged);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("FeralRetained AI: No ASC on %s — break-out can never fire"),
            *InPawn->GetName());
    }
}

void AGothicFeralRetainedController::BeginLeapFlight()
{
    ACharacter* MeChar = Cast<ACharacter>(GetPawn());
    UCharacterMovementComponent* Move = MeChar ? MeChar->GetCharacterMovement() : nullptr;
    if (!MeChar || !Move)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("FeralBreakout[%s]: leap flight not guarded — no character/movement component. "
                 "The behaviour tree can steer her out of the arc."),
            *GetNameSafe(GetPawn()));
        return;
    }

    if (bLeapInFlight)
    {
        // Second launch before the first landed. Don't re-save AirControl or
        // SavedAirControl would be overwritten with the zero we just wrote.
        GetWorldTimerManager().SetTimer(
            LeapFlightSafetyHandle, this, &AGothicFeralRetainedController::HandleLeapFlightTimeout,
            LeapFlightSafetySeconds, false);
        return;
    }

    bLeapInFlight = true;

    // 1. No new move requests for the duration of the arc.
    if (UBrainComponent* Brain = GetBrainComponent())
    {
        Brain->PauseLogic(GLeapPauseReason);
    }

    // 2. And no steering even if one slips through — a Move To issued in
    //    MOVE_Falling is spent as air-control acceleration, which is exactly
    //    what was cancelling her horizontal velocity.
    SavedAirControl = Move->AirControl;
    Move->AirControl = 0.f;

    MeChar->LandedDelegate.AddDynamic(this, &AGothicFeralRetainedController::HandleLeapLanded);

    GetWorldTimerManager().SetTimer(
        LeapFlightSafetyHandle, this, &AGothicFeralRetainedController::HandleLeapFlightTimeout,
        LeapFlightSafetySeconds, false);

    UE_LOG(LogTemp, Verbose,
        TEXT("FeralBreakout[%s]: leap flight begun — brain paused, AirControl %.3f -> 0 "
             "(safety %.1fs)"),
        *GetNameSafe(MeChar), SavedAirControl, LeapFlightSafetySeconds);
}

void AGothicFeralRetainedController::HandleLeapLanded(const FHitResult& Hit)
{
    EndLeapFlight(TEXT("landed"));
}

void AGothicFeralRetainedController::HandleLeapFlightTimeout()
{
    EndLeapFlight(TEXT("safety timer — landing never arrived"));
}

void AGothicFeralRetainedController::EndLeapFlight(const TCHAR* Reason)
{
    if (!bLeapInFlight)
    {
        return;
    }
    bLeapInFlight = false;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(LeapFlightSafetyHandle);
    }

    ACharacter* MeChar = Cast<ACharacter>(GetPawn());
    if (MeChar)
    {
        MeChar->LandedDelegate.RemoveDynamic(this, &AGothicFeralRetainedController::HandleLeapLanded);

        if (UCharacterMovementComponent* Move = MeChar->GetCharacterMovement())
        {
            Move->AirControl = SavedAirControl;
        }
    }

    if (UBrainComponent* Brain = GetBrainComponent())
    {
        Brain->ResumeLogic(GLeapPauseReason);
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("FeralBreakout[%s]: leap flight ended (%s) — brain resumed, AirControl restored to %.3f"),
        *GetNameSafe(MeChar), Reason, SavedAirControl);
}

void AGothicFeralRetainedController::OnUnPossess()
{
    // Before the pawn goes: hand back its AirControl and drop the binding, or a
    // re-possess would inherit a zeroed value and a stale delegate.
    EndLeapFlight(TEXT("unpossessed"));

    if (CachedASC && HealthChangedHandle.IsValid())
    {
        CachedASC->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
        HealthChangedHandle.Reset();
    }
    CachedASC = nullptr;

    Super::OnUnPossess();
}

void AGothicFeralRetainedController::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
    if (bBrokenOut || !CachedASC)
    {
        return;
    }

    const float MaxHealth = CachedASC->GetNumericAttribute(UGothicAttributeSet::GetMaxHealthAttribute());
    if (MaxHealth <= 0.f)
    {
        return;
    }

    const float Fraction = Data.NewValue / MaxHealth;
    if (Fraction > BreakoutHealthThreshold)
    {
        return;
    }

    bBrokenOut = true;

    // Generic phase bookkeeping (Blackboard write + broadcast) so anything
    // watching the phase reacts, same as the boss.
    OnPhaseAdvance();

    // Activate the break-out ability by CLASS, not asset tag. BP-default asset
    // tags don't reliably feed GetAssetTags() in this project — some abilities'
    // tags resolve (DraugrStrike) and some don't (Pounce/Rend/Breakout), all
    // structurally identical BP children of UGothicGameplayAbility — so
    // TryActivateAbilitiesByTag is not a dependable activation path here.
    // Finding the granted spec by class sidesteps the whole gremlin.
    bool bActivated = false;
    for (const FGameplayAbilitySpec& Spec : CachedASC->GetActivatableAbilities())
    {
        if (Spec.Ability && Spec.Ability->IsA(UGA_FeralBreakout::StaticClass()))
        {
            bActivated = CachedASC->TryActivateAbility(Spec.Handle);
            break;
        }
    }

    if (!bActivated)
    {
        UE_LOG(LogTemp, Error,
            TEXT("FeralRetained AI: break-out threshold crossed but GA_FeralBreakout did not "
                 "activate — is BP_GA_FeralBreakout in her StartupAbilities and free of blocking tags?"));
    }
}
