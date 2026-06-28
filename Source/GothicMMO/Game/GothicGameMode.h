// GothicGameMode.h
// Server-authoritative GameMode.
// Handles player spawning, respawning, and session lifecycle.
// Only exists on the server — clients use GameState for shared info.
//
// Set in Project Settings > Maps & Modes or per-map in World Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GothicGameMode.generated.h"

UCLASS()
class GOTHICMMO_API AGothicGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AGothicGameMode();

    /**
     * Called when a player dies (triggered from AGothicCharacterBase::OnDeath).
     * Starts the respawn timer and then calls RestartPlayer.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|GameMode")
    void RequestRespawn(AController* DeadController);

protected:
    /** How long (seconds) players wait before respawning. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|GameMode")
    float RespawnDelay = 10.f;

    /** Maximum number of players allowed in this session. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|GameMode")
    int32 MaxPlayers = 16;

    // GameModeBase overrides
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;

private:
    void RespawnPlayer(AController* Controller);
};
