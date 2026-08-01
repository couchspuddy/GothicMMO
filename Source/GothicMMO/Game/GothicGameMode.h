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
     * Called when a player dies — from AGothicPlayerCharacter::OnDeath.
     *
     * Starts the respawn timer and DELIBERATELY leaves the dead pawn possessed
     * for its duration. An unpossess here is what made the old version unusable:
     * the controller's view target went with the pawn, so the player watched
     * nothing at all for the whole delay and read it as the game hanging. See
     * AGothicPlayerCharacter::TriggerFallRespawn, which was written to avoid this
     * function entirely for exactly that reason.
     *
     * The corpse already has its collision and movement switched off by OnDeath,
     * so it is inert — it is only there to be looked at. The unpossess and the
     * destroy happen in RespawnPlayer, one frame before the replacement pawn
     * spawns, which is too short to register.
     *
     * Safe to call more than once for the same controller: a repeat call while a
     * respawn is already pending is ignored.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|GameMode")
    void RequestRespawn(AController* DeadController);

protected:
    /**
     * How long (seconds) players wait before respawning.
     *
     * Five rather than the ten this shipped with: there is no death screen to
     * read, so every second past the first couple is dead air spent looking at
     * your own corpse. Raise it once there is something on screen to fill it.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|GameMode")
    float RespawnDelay = 5.f;

    /** Maximum number of players allowed in this session. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|GameMode")
    int32 MaxPlayers = 16;

    // GameModeBase overrides
    /** Reseeds the deterministic RNG stream for this run — see GothicDeterminism.h. */
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;

private:
    void RespawnPlayer(TWeakObjectPtr<AController> ControllerPtr);

    /**
     * Controllers with a respawn in flight, and the timer that will fire it.
     *
     * Serves two jobs: it rejects the duplicate RequestRespawn that two damage
     * instances landing in the same frame produce, and it gives Logout something
     * to cancel so a timer cannot fire against a controller that has left. The
     * payload is deliberately a weak pointer for the same reason — the old code
     * captured a raw AController* in the timer delegate, which nothing kept
     * alive.
     */
    TMap<TWeakObjectPtr<AController>, FTimerHandle> PendingRespawns;
};
