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

    // Unpossess the dead pawn (leaves the corpse in the world).
    if (APawn* DeadPawn = DeadController->GetPawn())
    {
        DeadController->UnPossess();
        // The corpse's timer (set in OnDeath) will destroy it after CorpseLifetime.
        DeadPawn->Destroy();
    }

    // Schedule respawn after the delay.
    FTimerHandle RespawnTimer;
    FTimerDelegate RespawnDelegate;
    RespawnDelegate.BindUObject(this, &AGothicGameMode::RespawnPlayer, DeadController);
    GetWorldTimerManager().SetTimer(RespawnTimer, RespawnDelegate, RespawnDelay, false);

}

void AGothicGameMode::RespawnPlayer(AController* Controller)
{
    if (!Controller)
    {
        return;
    }

    // RestartPlayer finds a player start and spawns the DefaultPawnClass there.
    RestartPlayer(Controller);

    // Solo Contract: teleport to last Selah checkpoint instead of random PlayerStart.
    APawn* NewPawn = Controller->GetPawn();
    AGothicGameState* GS = GetGameState<AGothicGameState>();

    if (NewPawn && GS && !GS->CheckpointLocation.IsZero())
    {
        NewPawn->SetActorLocation(GS->CheckpointLocation);
    }
}