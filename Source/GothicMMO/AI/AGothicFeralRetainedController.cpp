// AGothicFeralRetainedController.cpp

#include "AI/AGothicFeralRetainedController.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AI/GA_FeralBreakout.h"
#include "Abilities/GameplayAbility.h"

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

void AGothicFeralRetainedController::OnUnPossess()
{
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
