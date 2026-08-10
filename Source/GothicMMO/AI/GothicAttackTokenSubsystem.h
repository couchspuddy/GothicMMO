// GothicAttackTokenSubsystem.h
// Server-side attacker-slot arbitration so a pack takes turns instead of dogpiling.
//
// The problem it closes: every packmate that reaches attack range attacks at
// once, so five Thralls collapse onto a player as one wall of hits. This is a
// SLOT POOL, not a coordinator — N tokens (default 2) exist for the whole world;
// a member must hold one to be allowed into its attack branch, and returns it
// when it leaves range or dies. Whoever doesn't hold one circles and waits.
//
// Relationship to UGothicPackSubsystem: orthogonal, not overlapping. The pack
// subsystem is a DEATH-REACTION broadcaster (a packmate falls → survivors play
// GA_PackGuard in the same frame) keyed on PackID; it has no notion of attack
// slots. This subsystem owns the "how many may swing at once" budget and has no
// notion of PackID. They could be unified later behind one "pack" umbrella, but
// the concerns are genuinely separate and fusing them would tangle a death
// broadcaster with a per-frame slot ledger — kept apart deliberately.
//
// No replication, same reasoning as the pack subsystem: AI is server-authoritative
// and the only observable effect (an enemy that does or doesn't start its attack)
// replicates through its own montage/GAS path. The BT service that drives this is
// server-side; the subsystem never needs to matter on a client.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GothicAttackTokenSubsystem.generated.h"

/**
 * A finite pool of attack permits shared across all attackers in the world.
 * Request/Release is owner-tracked, so Release is idempotent and a holder that
 * dies or is destroyed forfeits its slot automatically on the next Request.
 */
UCLASS(Config = Game)
class GOTHICMMO_API UGothicAttackTokenSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /**
     * Grants Requester an attack permit if one is free (or it already holds one).
     * Returns true when the caller may attack this tick. Idempotent: a holder
     * re-requesting keeps its slot and gets true without consuming a second.
     *
     * Every call first sweeps stale holders — invalid weak pointers and dead
     * pawns — so a token stranded by a missed Release (BT torn down mid-swing,
     * pawn destroyed on a corpse timer) is reclaimed here rather than leaking
     * the slot for the rest of the encounter.
     */
    bool RequestToken(AActor* Requester);

    /** Returns Requester's permit to the pool. Safe to call when it holds none. */
    void ReleaseToken(AActor* Requester);

    /** True if Requester currently holds a permit (no side effects, no sweep). */
    bool HasToken(const AActor* Requester) const;

protected:
    /**
     * How many attackers may hold a permit at once. Config-backed rather than a
     * plain EditDefaultsOnly member because WorldSubsystem class defaults are not
     * exposed in any Details panel (same limitation UGothicPackSubsystem notes on
     * RegroupCooldown) — Config makes it genuinely tunable from DefaultGame.ini
     * under [/Script/GothicMMO.GothicAttackTokenSubsystem]. Two = the "one player,
     * one flanker" read the brief specifies.
     */
    UPROPERTY(Config, EditDefaultsOnly, Category = "Attack Tokens", meta = (ClampMin = "0"))
    int32 MaxTokens = 2;

    /**
     * Current permit holders. Weak, not raw: enemies destroy on corpse timers and
     * on encounter collection, and a stale entry must never keep a slot occupied
     * or crash the sweep — the same exposure class as standing gotcha #4.
     */
    TArray<TWeakObjectPtr<AActor>> Holders;

private:
    /** Drops invalid and dead holders from the pool. Returns count reclaimed. */
    int32 SweepStaleHolders();
};
