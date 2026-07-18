// GothicPackSubsystem.h
// Server-side pack coordination for pack-hunting enemies (Thralls, primarily).
//
// The synchronization IS the feature: when a pack member dies, every survivor
// reacts in the same frame — a pack flinching as one organism is what sells
// "they hunt together." Membership is keyed on a PackID FName stamped per
// enemy instance or per spawn point, keeping pack grouping a LEVEL DESIGN
// decision, orthogonal to encounter volumes (the plaza runs multiple waves
// through one volume; each wave regroups independently under its own PackID).
//
// No replication here, deliberately: AI is server-authoritative, and the
// visible reaction (the guard pose via GA_PackGuard) replicates to clients
// through GAS montage replication on its own. This subsystem never needs to
// exist meaningfully on a client — every entry point is guarded by the
// enemy-side HasAuthority() checks at the call sites.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GothicPackSubsystem.generated.h"

class AGothicEnemyBase;

UCLASS()
class GOTHICMMO_API UGothicPackSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /**
     * Adds a member to a pack. Called from AGothicEnemyBase::SetPackID on the
     * server. Registering under NAME_None is a silent no-op — packless is the
     * default state and the boss/Retained opt out by simply never setting one.
     */
    void RegisterMember(FName PackID, AGothicEnemyBase* Member);

    /** Removes a member. Called on death, on EndPlay, and when SetPackID
     *  moves a member between packs. Safe to call redundantly. */
    void UnregisterMember(FName PackID, AGothicEnemyBase* Member);

    /**
     * Called from AGothicEnemyBase::OnDeath_Implementation (server-only by
     * construction — see the comment on MulticastOnDeath in that class).
     * Unregisters the dead member, then triggers the regroup pause on every
     * living survivor in the same loop, same frame.
     */
    void NotifyMemberDeath(FName PackID, AGothicEnemyBase* DeadMember);

protected:
    /**
     * Weak pointers, not raw: enemies destroy on CorpseLifetime timers and on
     * encounter collection, and a stale entry must not crash the notify loop.
     * Same class of exposure as standing gotcha #4 — state that outlives an
     * actor cannot hold hard pointers to it.
     */
    TMap<FName, TArray<TWeakObjectPtr<AGothicEnemyBase>>> Packs;

    /**
     * Per-pack retrigger lockout (world seconds until eligible again).
     * Design decision, deliberate: the pause fires once per window rather
     * than refreshing on every death — refresh-on-death lets a fast player
     * chain-freeze a pack permanently. The guard pose already limits the
     * exploit (vital occluded, body shots only), but a pack that can be held
     * frozen indefinitely stops reading as a pack. To reverse this decision,
     * delete the lockout check in NotifyMemberDeath — it is isolated to one
     * spot precisely so that reversal stays one line.
     */
    TMap<FName, float> PackRegroupCooldownUntil;

    /**
     * Seconds a pack is immune to a second regroup trigger after one fires.
     * Plain member rather than UPROPERTY: subsystem class defaults aren't
     * editable in-editor anyway; promote to config if tuning demands it.
     */
    float RegroupCooldown = 8.f;
};