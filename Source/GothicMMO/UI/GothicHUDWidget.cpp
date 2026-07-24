// GothicHUDWidget.cpp

#include "UI/GothicHUDWidget.h"

#include "Components/ProgressBar.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"

void UGothicHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();
    // Subclass Blueprint widgets initialize their own bindings here.
    // C++ just sets up the cached defaults.
}

void UGothicHUDWidget::SetAbilityCooldownDisplay(EGothicAbilitySlot AbilitySlot, float Remaining, float Total)
{
    // Overlay fills UP as the ability recharges: empty the instant it's used,
    // full when ready. Total <= 0 means "not on cooldown" -> full (ready).
    const float Percent = (Total > 0.f) ? FMath::Clamp(1.f - Remaining / Total, 0.f, 1.f) : 1.f;

    FName BarName = NAME_None;
    switch (AbilitySlot)
    {
        case EGothicAbilitySlot::Ability1: BarName = TEXT("CooldownBar_0"); break;
        case EGothicAbilitySlot::Ability2: BarName = TEXT("CooldownBar_1"); break;
        case EGothicAbilitySlot::Ability3: BarName = TEXT("CooldownBar_2"); break;
        default: return;
    }

    if (UProgressBar* Bar = Cast<UProgressBar>(GetWidgetFromName(BarName)))
    {
        Bar->SetPercent(Percent);
    }
}
