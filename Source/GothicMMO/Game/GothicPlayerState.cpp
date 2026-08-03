// GothicPlayerState.cpp

#include "Game/GothicPlayerState.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "Items/GothicInventoryComponent.h"
#include "Net/UnrealNetwork.h"

AGothicPlayerState::AGothicPlayerState()
{
    // UE5.8: Use SetNetUpdateFrequency() instead of direct assignment
    SetNetUpdateFrequency(100.f);

    AbilitySystemComponent = CreateDefaultSubobject<UGothicAbilitySystemComponent>(
        TEXT("AbilitySystemComponent"));
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

    AttributeSet = CreateDefaultSubobject<UGothicAttributeSet>(TEXT("AttributeSet"));

    InventoryComponent = CreateDefaultSubobject<UGothicInventoryComponent>(TEXT("InventoryComponent"));
}

UAbilitySystemComponent* AGothicPlayerState::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}

void AGothicPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AGothicPlayerState, bIsDowned);
}

void AGothicPlayerState::SetDowned(bool bNewDowned)
{
    if (!HasAuthority() || bIsDowned == bNewDowned)
    {
        return;
    }

    bIsDowned = bNewDowned;
    ForceNetUpdate();

    // Mirror onto the ASC so GAS-side consumers — ability ActivationBlockedTags,
    // future downed GEs — can ask the usual way. Deliberately a SERVER-SIDE
    // CONVENIENCE and not a second source of truth: loose tags never replicate
    // (the same trap State.Dead fell into in GothicCharacterBase::OnDeath, where
    // it is invisible in a client build), so bIsDowned above is what clients read.
    //
    // SetLooseGameplayTagCount, not Add/Remove: loose tags are ref-counted and a
    // state reported twice would otherwise leave a count a single remove can't pay off.
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->SetLooseGameplayTagCount(
            GothicTags::State_Downed, bIsDowned ? 1 : 0);
    }

    const UWorld* World = GetWorld();
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|Downed|%s|player=%s"),
        World ? World->GetTimeSeconds() : 0.f, *GetNameSafe(this),
        bIsDowned ? TEXT("SET") : TEXT("CLEAR"), *GetPlayerName());

    // The authority never gets its own OnRep — fire it directly so the listen-server
    // host and standalone PIE see the change exactly like a remote client does.
    OnRep_IsDowned();
}

void AGothicPlayerState::OnRep_IsDowned()
{
    OnDownedChanged(bIsDowned);
}