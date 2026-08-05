// GothicGameMode.h
// Server-authoritative GameMode.
// Handles player spawning, respawning, and session lifecycle.
// Only exists on the server — clients use GameState for shared info.
//
// Set in Project Settings > Maps & Modes or per-map in World Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Game/GothicGameState.h"           // EGothicPartyState
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

    // -------------------------------------------------------------------------
    // Party wipe (PR-5)
    //
    // A wipe is "every party member is dead-or-downed at the same moment". When
    // it happens the whole party respawns together at the checkpoint, and the
    // revive windows of anyone still on the floor are collapsed into it — the
    // alternative is a downed player bleeding out alone for thirty seconds with
    // nobody left in the world who could possibly reach them.
    //
    // The census is NOT here. It lives on AGothicGameState (ComputePartyState /
    // HasFightablePartyMember) so that one walk of the roster serves both the
    // downed fork's "is anyone left to pick me up" question and this one. What
    // this class owns is the decision and the execution.
    // -------------------------------------------------------------------------

    /**
     * Authority-only. Re-runs the party census, stores the result on the
     * GameState, and executes the wipe on the TRANSITION into Wiped.
     *
     * Cheap and idempotent by design, because it is called from every membership
     * and state transition there is: EnterDownedState, both revive paths, revive
     * window expiry, OnDeath, respawn completion, PostLogin and Logout. Calling
     * it more often than necessary costs one roster walk; missing a call costs a
     * party stuck in a state nobody can leave.
     *
     * IgnoreController is for Logout, where the leaver is still on the roster —
     * see AGothicGameState::ComputePartyState.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Party")
    void EvaluatePartyState(AController* IgnoreController = nullptr);

    /**
     * Authority-only. Runs the wipe: cancel every revive channel, collapse every
     * revive window (which routes those players through the ordinary death path,
     * banking what death banks), and let the respawn each of them already asked
     * for carry them to the checkpoint.
     *
     * Normally reached through EvaluatePartyState. Public and BlueprintCallable
     * so Gothic.ForceWipe can drive the execution path without having to arrange
     * the census that triggers it.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Party")
    void ExecutePartyWipe();

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

    /**
     * Lateral spacing between simultaneously-spawning players at the checkpoint.
     *
     * A wipe puts everybody through the checkpoint teleport on the same frame,
     * and the checkpoint is a single point — without this they all arrive inside
     * one another and the capsule depenetration flings them apart. Slot 0 gets
     * the checkpoint itself; everyone else gets a ring position around it.
     *
     * ~2.5 player capsule radii. Big enough that nobody starts overlapping, small
     * enough that a four-player party still lands as a group rather than a
     * scatter across the room.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|GameMode",
              meta = (ClampMin = "0.0"))
    float PartySpawnRingRadius = 220.f;

    /** Ring positions before the pattern steps out to a wider ring. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|GameMode",
              meta = (ClampMin = "1"))
    int32 PartySpawnRingSlots = 6;

    // GameModeBase overrides
    /** Reseeds the deterministic RNG stream for this run — see GothicDeterminism.h. */
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;

private:
    void RespawnPlayer(TWeakObjectPtr<AController> ControllerPtr);

    /**
     * Puts Controller's CURRENT pawn on the checkpoint, spaced off it by that
     * player's ring slot and nudged out of anything it would have spawned inside.
     *
     * Factored out of RespawnPlayer because PostLogin needs the identical
     * behaviour: a player joining a session that is already deep into the level
     * used to be dropped at a PlayerStart back at the entrance — the checkpoint
     * was recorded but nothing ever teleported a NEW arrival to it. No-ops when
     * no checkpoint has been recorded yet.
     */
    void TeleportToCheckpoint(AController* Controller);

    /**
     * Ring offset for a spawn slot. Slot 0 is the origin itself; every other slot
     * walks a ring of PartySpawnRingSlots positions, stepping out one radius per
     * completed ring so an arbitrarily large party never collides with itself.
     *
     * Deliberately DETERMINISTIC and index-derived rather than a seeded draw: the
     * point is that two players never land on the same spot, and a random offset
     * only makes that unlikely rather than impossible.
     */
    FVector GetPartySpawnOffset(int32 SlotIndex) const;

    /** This controller's index in the GameState roster, or 0 if it isn't on it. */
    int32 GetPartySlotIndex(const AController* Controller) const;

    /**
     * Re-entry guard for the wipe.
     *
     * ExecutePartyWipe collapses revive windows, each of which runs the full
     * death path, each of which calls back into EvaluatePartyState. Without this
     * the second player's death would re-enter the wipe that is currently
     * processing them. Also stops the census churning while the whole party is
     * dead and waiting on RespawnDelay.
     */
    bool bWipeInProgress = false;

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
