// GothicGameMode.cpp

#include "Game/GothicGameMode.h"
#include "Character/GothicPlayerCharacter.h"
#include "Game/GothicPlayerState.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

AGothicGameMode::AGothicGameMode()
{
    // Default classes — override with Blueprint versions in Project Settings.
    // These point to the C++ classes; replace with BP_GothicPlayerCharacter etc.
    DefaultPawnClass          = AGothicPlayerCharacter::StaticClass();
    PlayerStateClass          = AGothicPlayerState::StaticClass();
    // HUDClass and GameStateClass set in Blueprint child BP_GothicGameMode.
}

void AGothicGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    UE_LOG(LogTemp, Log, TEXT("GothicGameMode: Player joined. Active players: %d"),
        GetNumPlayers());
}

void AGothicGameMode::Logout(AController* Exiting)
{
    Super::Logout(Exiting);

    UE_LOG(LogTemp, Log, TEXT("GothicGameMode: Player left."));
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
    }

    // Schedule respawn after the delay.
    FTimerHandle RespawnTimer;
    FTimerDelegate RespawnDelegate;
    RespawnDelegate.BindUObject(this, &AGothicGameMode::RespawnPlayer, DeadController);
    GetWorldTimerManager().SetTimer(RespawnTimer, RespawnDelegate, RespawnDelay, false);

    UE_LOG(LogTemp, Log, TEXT("GothicGameMode: Respawning player in %.1f seconds."), RespawnDelay);
}

void AGothicGameMode::RespawnPlayer(AController* Controller)
{
    if (!Controller)
    {
        return;
    }

    // RestartPlayer finds a player start and spawns the DefaultPawnClass there.
    RestartPlayer(Controller);
}
