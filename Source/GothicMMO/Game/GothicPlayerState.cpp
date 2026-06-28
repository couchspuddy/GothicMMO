// GothicPlayerState.cpp

#include "Game/GothicPlayerState.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"

AGothicPlayerState::AGothicPlayerState()
{
    // UE5.8: Use SetNetUpdateFrequency() instead of direct assignment
    SetNetUpdateFrequency(100.f);

    AbilitySystemComponent = CreateDefaultSubobject<UGothicAbilitySystemComponent>(
        TEXT("AbilitySystemComponent"));
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

    AttributeSet = CreateDefaultSubobject<UGothicAttributeSet>(TEXT("AttributeSet"));
}

UAbilitySystemComponent* AGothicPlayerState::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}
