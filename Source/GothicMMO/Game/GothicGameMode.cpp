// GothicGameMode.cpp

#include "Game/GothicGameMode.h"
#include "Character/GothicPlayerCharacter.h"
#include "Game/GothicPlayerState.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Game/GothicGameState.h"
#include "Game/GothicDeterminism.h"
#include "GothicMMO.h"                          // LogVigilCombat
#include "HAL/IConsoleManager.h"                // Gothic.ForceWipeCheck / Gothic.ForceWipe
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"

AGothicGameMode::AGothicGameMode()
{
    // Default classes — override with Blueprint versions in Project Settings.
    // These point to the C++ classes; replace with BP_GothicPlayerCharacter etc.
    DefaultPawnClass          = AGothicPlayerCharacter::StaticClass();
    PlayerStateClass          = AGothicPlayerState::StaticClass();
    GameStateClass = AGothicGameState::StaticClass();
    // HUDClass and GameStateClass set in Blueprint child BP_GothicGameMode.
}

void AGothicGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);

    // Restart the seeded stream at the top of every level load, so run N and run
    // N+1 draw the same sequence instead of run N+1 continuing where N stopped.
    // Logs the seed when deterministic mode is on; silent otherwise.
    FGothicDeterminism::ResetStream();
}

void AGothicGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    // Seed the checkpoint at the level entrance.
    //
    // CheckpointLocation stayed zero until the first Selah payout wrote it, so
    // every death before the first encounter cleared fell through the IsZero()
    // guards in the respawn readers and put the player at a RANDOM PlayerStart.
    // The checkpoint is no longer allowed to be "unset": from the moment someone
    // logs in, dying returns them to where they came in.
    //
    // Read after Super, and off the pawn rather than FindPlayerStart: PostLogin
    // has already run RestartPlayer by this point, so the pawn is standing on the
    // start that was actually chosen — and our FindPlayerStart override picks
    // RANDOMLY, so asking it a second time could name a different one.
    //
    // The IsZero() check stops a mid-session rejoin stomping real progress.
    if (AGothicGameState* GS = GetGameState<AGothicGameState>())
    {
        if (GS->CheckpointLocation.IsZero())
        {
            if (const APawn* NewPawn = NewPlayer ? NewPlayer->GetPawn() : nullptr)
            {
                GS->SetCheckpointFromLocation(NewPawn->GetActorLocation());
            }
        }
        else
        {
            // THE LATE-JOINER BUG. The branch above only ever SEEDS the
            // checkpoint; a player arriving into a session that has already
            // pushed several encounters deep fell straight through it and was
            // left standing on whatever PlayerStart FindPlayerStart picked —
            // which is back at the level entrance, alone, with the party a map
            // away. Nothing in the project teleported a NEW arrival to the
            // checkpoint; only respawns got that.
            //
            // Same call the respawn path makes, so a late joiner and a
            // respawning player land the same way, spaced by the same ring.
            TeleportToCheckpoint(NewPlayer);
        }
    }

    // A join changes party membership, so the readout has to move with it — most
    // obviously the case where the party had wiped and the state must fall back
    // out of Wiped now that somebody upright is present.
    EvaluatePartyState();
}

void AGothicGameMode::Logout(AController* Exiting)
{
    // A respawn timer outliving its controller would fire into a destroyed
    // object. The weak payload makes that survivable; clearing the timer makes
    // it not happen.
    if (FTimerHandle* PendingTimer = PendingRespawns.Find(Exiting))
    {
        GetWorldTimerManager().ClearTimer(*PendingTimer);
        PendingRespawns.Remove(Exiting);
    }

    Super::Logout(Exiting);

    // THE FORGOTTEN EDGE. A wipe is not only "everyone died" — it is also "the
    // last person who could have revived you closed the game". Without this a
    // downed player left alone by a disconnect sits on the floor for the rest of
    // their window and then bleeds out as if nobody had ever been coming, which
    // reads as the game having forgotten about them.
    //
    // Exiting is passed as the ignore: their PlayerState is STILL in PlayerArray
    // at this point (it leaves later, in AController::Destroyed →
    // CleanupPlayerState), so a census that counted them would find the party
    // perfectly healthy and do nothing.
    EvaluatePartyState(Exiting);

    // TODO: Save player progression data here.
}

AActor* AGothicGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
    // Find all PlayerStart actors in the level.
    TArray<AActor*> PlayerStarts;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);

    if (PlayerStarts.Num() == 0)
    {
        // Fallback to default behavior if no player starts exist.
        return Super::FindPlayerStart_Implementation(Player, IncomingName);
    }

    // Deterministic mode pins the start rather than seeding the pick: a seeded
    // draw would still move the player when a designer adds a PlayerStart to the
    // level, and time-to-contact is measured from where the player lands. Sorted
    // by name because GetAllActorsOfClass order is not contractually stable.
    if (FGothicDeterminism::IsEnabled())
    {
        PlayerStarts.Sort([](const AActor& A, const AActor& B)
        {
            return A.GetName() < B.GetName();
        });

        // ...but PINNED PER PLAYER, not per session. Returning PlayerStarts[0]
        // unconditionally meant every player in a multiplayer session spawned on
        // the same actor. Solo that is invisible; with two players it is a pair of
        // capsules depenetrating out of each other on the first frame, and a party
        // wipe — which restarts EVERYONE at once — makes it a certainty rather
        // than a race.
        //
        // Slot-indexed and wrapped, so it stays fully deterministic: player 0
        // still gets exactly the start it always got, and every measurement taken
        // against the solo baseline is unchanged.
        const int32 Slot = GetPartySlotIndex(Player);
        return PlayerStarts[Slot % PlayerStarts.Num()];
    }

    // Simple random selection for prototype.
    // TODO: Replace with zone-based respawn point selection.
    return PlayerStarts[FMath::RandRange(0, PlayerStarts.Num() - 1)];
}

void AGothicGameMode::RequestRespawn(AController* DeadController)
{
    if (!DeadController)
    {
        return;
    }

    // One respawn per death. OnDeath already guards re-entry on State.Dead, but a
    // Blueprint or a future system calling this directly must not be able to
    // stack timers — two would race, and the second would destroy the pawn the
    // first had just spawned.
    if (PendingRespawns.Contains(DeadController))
    {
        return;
    }

    // The pawn stays possessed. It is the player's view target, and it is the
    // only thing standing between the death delay and a black screen — see the
    // header. OnDeath has already made it harmless.
    FTimerDelegate RespawnDelegate;
    RespawnDelegate.BindUObject(this, &AGothicGameMode::RespawnPlayer,
        TWeakObjectPtr<AController>(DeadController));

    FTimerHandle& RespawnTimer = PendingRespawns.Add(DeadController);
    GetWorldTimerManager().SetTimer(RespawnTimer, RespawnDelegate, RespawnDelay, false);
}

void AGothicGameMode::RespawnPlayer(TWeakObjectPtr<AController> ControllerPtr)
{
    AController* Controller = ControllerPtr.Get();
    if (!Controller)
    {
        return;
    }

    PendingRespawns.Remove(ControllerPtr);

    // Now, and not a moment earlier. RestartPlayerAtPlayerStart REUSES the
    // controller's existing pawn when it has one, so leaving the corpse attached
    // would teleport the corpse to the spawn point — collisionless, immobile and
    // still tagged dead — instead of building a live pawn. Clearing it here also
    // keeps the view on the body for the entire delay and hands it over on the
    // frame the new pawn possesses.
    if (APawn* DeadPawn = Controller->GetPawn())
    {
        Controller->UnPossess();
        DeadPawn->Destroy();
    }

    // RestartPlayer finds a player start and spawns the DefaultPawnClass there.
    // The new pawn's PossessedBy runs InitGASFromPlayerState, which is where the
    // State.Dead tag and the health are put right — the ASC it inherits from the
    // PlayerState is still carrying both from the death that got us here.
    RestartPlayer(Controller);

    // Solo Contract: teleport to last Selah checkpoint instead of random PlayerStart.
    TeleportToCheckpoint(Controller);

    // A pawn came back up, so the party readout has to follow it out of Wiped (or
    // out of InPeril, if this was the last one still down). This is the ONLY way
    // a wipe ends — nothing else clears the state.
    EvaluatePartyState();
}

// ---------------------------------------------------------------------------
// Checkpoint placement
// ---------------------------------------------------------------------------

int32 AGothicGameMode::GetPartySlotIndex(const AController* Controller) const
{
    const AGameStateBase* GS = GetGameState<AGameStateBase>();
    APlayerState* PS = Controller ? Controller->PlayerState : nullptr;
    if (!GS || !PS)
    {
        return 0;
    }

    // Falls back to slot 0 for a controller not on the roster — a spectator, or a
    // pawn possessed by AI. Slot 0 is the unoffset checkpoint itself, which is the
    // right answer for "I don't know where you belong".
    const int32 Index = GS->PlayerArray.IndexOfByKey(PS);
    return (Index == INDEX_NONE) ? 0 : Index;
}

FVector AGothicGameMode::GetPartySpawnOffset(int32 SlotIndex) const
{
    if (SlotIndex <= 0 || PartySpawnRingRadius <= 0.f)
    {
        return FVector::ZeroVector;
    }

    const int32 Slots     = FMath::Max(1, PartySpawnRingSlots);
    const int32 Adjusted  = SlotIndex - 1;              // slot 0 sits on the point
    const int32 Ring      = Adjusted / Slots;           // 0 = first ring out
    const int32 PosInRing = Adjusted % Slots;

    const float Angle  = (2.f * PI) * (static_cast<float>(PosInRing) / static_cast<float>(Slots));
    const float Radius = PartySpawnRingRadius * static_cast<float>(Ring + 1);

    return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
}

void AGothicGameMode::TeleportToCheckpoint(AController* Controller)
{
    APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    AGothicGameState* GS = GetGameState<AGothicGameState>();

    if (!Pawn || !GS || GS->CheckpointLocation.IsZero())
    {
        return;
    }

    // Lifted clear of the floor, for the same reason TriggerFallRespawn does it:
    // a checkpoint recorded at floor level drops the capsule into the geometry it
    // was standing on. The checkpoint is stored UNLIFTED precisely so each reader
    // does this once — see AGothicGameState::SetCheckpointFromLocation.
    FVector RespawnLocation = GS->CheckpointLocation + GetPartySpawnOffset(GetPartySlotIndex(Controller));
    RespawnLocation.Z += 100.f;

    // The ring keeps players off each other; this keeps the ring off the world. An
    // offset that lands inside a pillar or through a wall is the obvious failure
    // of spacing players out from a single recorded point, and FindTeleportSpot is
    // the engine's own answer to it — it nudges the capsule out of anything it
    // would be encroaching, including the teammates already placed this frame.
    // On failure it leaves RespawnLocation untouched and we teleport anyway: a
    // slightly overlapping player still depenetrates, a player left standing at
    // the old PlayerStart does not.
    if (UWorld* World = GetWorld())
    {
        FVector AdjustedLocation = RespawnLocation;
        if (World->FindTeleportSpot(Pawn, AdjustedLocation, Pawn->GetActorRotation()))
        {
            RespawnLocation = AdjustedLocation;
        }
    }

    Pawn->SetActorLocation(RespawnLocation, false, nullptr, ETeleportType::TeleportPhysics);
}

// ---------------------------------------------------------------------------
// Party wipe
// ---------------------------------------------------------------------------

void AGothicGameMode::EvaluatePartyState(AController* IgnoreController)
{
    AGothicGameState* GS = GetGameState<AGothicGameState>();
    if (!GS)
    {
        return;
    }

    // Mid-wipe the census is meaningless — every player is dead by construction
    // and would simply re-report Wiped into the wipe that is already running.
    if (bWipeInProgress)
    {
        return;
    }

    const EGothicPartyState Previous = GS->GetPartyState();
    const EGothicPartyState Current  = GS->ComputePartyState(IgnoreController);

    // Stored BEFORE the wipe executes, so anything the wipe re-enters reads the
    // new state rather than the one it is replacing.
    GS->SetPartyState(Current);

    if (Current != Previous)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("VigilTimeline|t=%.3f|GameMode|Party|%s->%s"),
            GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
            *UEnum::GetValueAsString(Previous), *UEnum::GetValueAsString(Current));
    }

    // On the TRANSITION only. Re-entering Wiped from Wiped would restart the wipe
    // on every death the wipe itself causes.
    //
    // OPEN DESIGN QUESTION, DELIBERATELY NOT ANSWERED HERE: the Rotunda endgame is
    // specified as "max aggression + sealed zones, NO wipe", which — read strictly
    // — means the boss encounter wants a party wipe to mean something other than
    // "everybody respawns at the checkpoint". Nothing in the arena or encounter
    // code currently carries a flag this could consult, so this PR wipes uniformly
    // and the exemption is left as future work. The hook, when it exists, is one
    // condition on this line.
    if (Current == EGothicPartyState::Wiped && Previous != EGothicPartyState::Wiped)
    {
        ExecutePartyWipe();
    }
}

void AGothicGameMode::ExecutePartyWipe()
{
    UWorld* World = GetWorld();
    AGothicGameState* GS = World ? GetGameState<AGothicGameState>() : nullptr;
    if (!GS || bWipeInProgress)
    {
        return;
    }

    TGuardValue<bool> WipeGuard(bWipeInProgress, true);

    UE_LOG(LogVigilCombat, Warning,
        TEXT("VigilTimeline|t=%.3f|GameMode|PartyWipe|BEGIN|players=%d"),
        World->GetTimeSeconds(), GS->PlayerArray.Num());

    // The roster is walked into a local array first. Collapsing a revive window
    // runs the full death path, which destroys and respawns pawns and can in
    // principle touch PlayerArray — iterating it directly while doing that is the
    // shape of a crash nobody wants to find in a wipe.
    TArray<AGothicPlayerCharacter*> Members;
    Members.Reserve(GS->PlayerArray.Num());
    for (const APlayerState* PS : GS->PlayerArray)
    {
        if (AGothicPlayerCharacter* Char = PS ? Cast<AGothicPlayerCharacter>(PS->GetPawn()) : nullptr)
        {
            Members.Add(Char);
        }
    }

    // Channels first. A channel whose reviver has just died would be caught by
    // TickReviveChannel within a tenth of a second anyway, but the bodies it
    // points at are about to be destroyed, and a wipe should not leave a "someone
    // is reviving you" bar replicated across the transition.
    for (AGothicPlayerCharacter* Char : Members)
    {
        if (IsValid(Char))
        {
            Char->CancelReviveChannel(TEXT("party-wipe"));
        }
    }

    // Then the windows. CollapseReviveWindow goes through the SAME
    // OnReviveWindowExpired the timer drives, so a wipe-killed downed player is
    // indistinguishable from one who bled out: same tag hygiene, same OnDeath,
    // same banking, same RequestRespawn. Players who were already dead rather than
    // downed have a respawn pending from their own OnDeath and need nothing here —
    // RequestRespawn refuses a second one regardless.
    for (AGothicPlayerCharacter* Char : Members)
    {
        if (IsValid(Char) && Char->IsDowned())
        {
            Char->CollapseReviveWindow();
        }
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("VigilTimeline|t=%.3f|GameMode|PartyWipe|END|respawns_pending=%d"),
        World->GetTimeSeconds(), PendingRespawns.Num());
}

// ---------------------------------------------------------------------------
// Gothic.ForceWipeCheck / Gothic.ForceWipe — debug console commands
//
// The natural transitions cover the real cases, but neither of the two that
// matter most is convenient to stage: "the last living player disconnects"
// needs a client to actually leave, and the execution path is otherwise only
// reachable by getting a whole party onto the floor at once. ForceWipeCheck
// re-runs the census and prints it player by player, so a verification run can
// see WHY the party is or is not wiped; ForceWipe drives ExecutePartyWipe
// directly, so the respawn half can be proved without arranging the census.
// ---------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
static AGothicGameMode* GothicWipeCmdGameMode(UWorld* World, const TCHAR* CmdName)
{
    AGothicGameMode* GM = World ? World->GetAuthGameMode<AGothicGameMode>() : nullptr;
    if (!GM)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("%s: no authoritative AGothicGameMode in this world — run this on the server."),
            CmdName);
    }
    return GM;
}

static void GothicForceWipeCheckConsoleCommand(UWorld* World)
{
    AGothicGameMode* GM = GothicWipeCmdGameMode(World, TEXT("Gothic.ForceWipeCheck"));
    if (!GM)
    {
        return;
    }

    AGothicGameState* GS = GM->GetGameState<AGothicGameState>();
    if (!GS)
    {
        UE_LOG(LogVigilCombat, Warning, TEXT("Gothic.ForceWipeCheck: no game state in this world."));
        return;
    }

    // The per-player readout is the point. A wipe that did not fire is nearly
    // always one player the census still counts as up, and this says which.
    for (int32 Index = 0; Index < GS->PlayerArray.Num(); ++Index)
    {
        const APlayerState* PS = GS->PlayerArray[Index];
        const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
        const AGothicCharacterBase* Char = Cast<const AGothicCharacterBase>(Pawn);

        UE_LOG(LogVigilCombat, Warning,
            TEXT("Gothic.ForceWipeCheck: [%d] '%s' pawn=%s alive=%d downed=%d fightable=%d"),
            Index, PS ? *PS->GetPlayerName() : TEXT("<null>"), *GetNameSafe(Pawn),
            Char ? Char->IsAlive() : 0, Char ? Char->IsDowned() : 0,
            AGothicCharacterBase::IsFightableActor(Pawn) ? 1 : 0);
    }

    UE_LOG(LogVigilCombat, Warning,
        TEXT("Gothic.ForceWipeCheck: stored=%s computed=%s — evaluating."),
        *UEnum::GetValueAsString(GS->GetPartyState()),
        *UEnum::GetValueAsString(GS->ComputePartyState()));

    GM->EvaluatePartyState();
}

static void GothicForceWipeConsoleCommand(UWorld* World)
{
    if (AGothicGameMode* GM = GothicWipeCmdGameMode(World, TEXT("Gothic.ForceWipe")))
    {
        UE_LOG(LogVigilCombat, Warning, TEXT("Gothic.ForceWipe: executing a party wipe unconditionally."));

        // The state is set first because ExecutePartyWipe deliberately does not set
        // it — EvaluatePartyState owns that — and a forced wipe should still show
        // the same replicated readout a natural one does.
        if (AGothicGameState* GS = GM->GetGameState<AGothicGameState>())
        {
            GS->SetPartyState(EGothicPartyState::Wiped);
        }
        GM->ExecutePartyWipe();
    }
}

static FAutoConsoleCommandWithWorld GGothicForceWipeCheckCmd(
    TEXT("Gothic.ForceWipeCheck"),
    TEXT("Gothic.ForceWipeCheck — print the party census player by player and re-run the wipe evaluation. Debug only."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&GothicForceWipeCheckConsoleCommand));

static FAutoConsoleCommandWithWorld GGothicForceWipeCmd(
    TEXT("Gothic.ForceWipe"),
    TEXT("Gothic.ForceWipe — execute a party wipe unconditionally, ignoring the census. Debug only."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&GothicForceWipeConsoleCommand));
#endif // !UE_BUILD_SHIPPING