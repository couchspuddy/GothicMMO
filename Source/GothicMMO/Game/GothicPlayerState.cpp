// GothicPlayerState.cpp

#include "Game/GothicPlayerState.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "HAL/IConsoleManager.h"
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

// ---------------------------------------------------------------------------
// Gothic.SetDowned <PlayerIndex> <0|1> — debug console command
//
// SetDowned is BlueprintCallable and server-only, and until now that made the
// downed state unreachable from every surface a verification session actually
// has. There is no Exec binding on it, the scripting sandbox has no handle on
// the object, and writing bIsDowned directly bypasses the two things worth
// verifying — the tag mirrored onto the ASC and the OnRep that tells the
// client. So the behaviour shipped unverified: nothing could put a player
// down on purpose.
//
// This drives the real setter, exactly as gothic.collect drives the real
// collection entry point rather than a debug shortcut. Everything SetDowned
// guards on — authority, the no-op on an unchanged value — still applies, and
// a command that cannot reach the authority reports that instead of appearing
// to work.
//
// PlayerIndex is an index into the game state's PlayerArray: 0 is the host on
// a listen server, 1 the first joining player. That array is ordered by login,
// not by anything stable across sessions, so the command prints the resolved
// player's name and lets the caller confirm they downed who they meant to.
// ---------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
static void GothicSetDownedConsoleCommand(const TArray<FString>& Args, UWorld* World)
{
    if (!World)
    {
        return;
    }

    if (Args.Num() < 2)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.SetDowned: usage is Gothic.SetDowned <PlayerIndex> <0|1>."));
        return;
    }

    const AGameStateBase* GS = World->GetGameState();
    if (!GS)
    {
        UE_LOG(LogVigilCombat, Warning, TEXT("Gothic.SetDowned: no game state in this world."));
        return;
    }

    const int32 PlayerIndex = FCString::Atoi(*Args[0]);
    if (!GS->PlayerArray.IsValidIndex(PlayerIndex))
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.SetDowned: no player at index %d — this world has %d."),
            PlayerIndex, GS->PlayerArray.Num());
        return;
    }

    AGothicPlayerState* PS = Cast<AGothicPlayerState>(GS->PlayerArray[PlayerIndex]);
    if (!PS)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.SetDowned: player %d is a %s, not an AGothicPlayerState."),
            PlayerIndex, *GetNameSafe(GS->PlayerArray[PlayerIndex]));
        return;
    }

    // Typed into a client's console this reaches the client's copy of the
    // PlayerState, where SetDowned correctly refuses. Saying so is the whole
    // difference between "the downed state is broken" and "run it on the host".
    if (!PS->HasAuthority())
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.SetDowned: %s is not authoritative in this world — run this on the server."),
            *GetNameSafe(PS));
        return;
    }

    const bool bDowned = (FCString::Atoi(*Args[1]) != 0);

    UE_LOG(LogVigilCombat, Warning,
        TEXT("Gothic.SetDowned: setting %s (index %d, player '%s') downed=%d."),
        *GetNameSafe(PS), PlayerIndex, *PS->GetPlayerName(), bDowned ? 1 : 0);

    PS->SetDowned(bDowned);
}

static FAutoConsoleCommandWithWorldAndArgs GGothicSetDownedCmd(
    TEXT("Gothic.SetDowned"),
    TEXT("Gothic.SetDowned <PlayerIndex> <0|1> — put a player into or out of the downed state on the authority. Debug only."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GothicSetDownedConsoleCommand));
#endif