// GothicGameMode.cpp

#include "Game/GothicGameMode.h"
#include "Character/GothicPlayerCharacter.h"
#include "Game/GothicPlayerState.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Game/GothicGameState.h"

AGothicGameMode::AGothicGameMode()
{
    // Default classes — override with Blueprint versions in Project Settings.
    // These point to the C++ classes; replace with BP_GothicPlayerCharacter etc.
    DefaultPawnClass          = AGothicPlayerCharacter::StaticClass();
    PlayerStateClass          = AGothicPlayerState::StaticClass();
    GameStateClass = AGothicGameState::StaticClass();
    // HUDClass and GameStateClass set in Blueprint child BP_GothicGameMode.
}

void AGothicGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

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
    APawn* NewPawn = Controller->GetPawn();
    AGothicGameState* GS = GetGameState<AGothicGameState>();

    if (NewPawn && GS && !GS->CheckpointLocation.IsZero())
    {
        // Lifted clear of the floor, for the same reason TriggerFallRespawn does
        // it: a checkpoint recorded at floor level drops the capsule into the
        // geometry it was standing on.
        FVector RespawnLocation = GS->CheckpointLocation;
        RespawnLocation.Z += 100.f;

        NewPawn->SetActorLocation(RespawnLocation, false, nullptr, ETeleportType::TeleportPhysics);
    }
}