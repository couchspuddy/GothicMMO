// GothicBossArenaManager.cpp

#include "AI/GothicBossArenaManager.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AI/GothicRotundaPillar.h"
#include "AI/GothicEnemyBase.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
    /**
     * The `t=` stamp every VigilTimeline line carries. Free function rather than
     * a member so this file needs no header change to log — and it answers 0
     * rather than asserting for a manager logging before/after its world, which
     * is a diagnostic, not a place to fail.
     */
    float ArenaTimelineNow(const AActor* Actor)
    {
        const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
        return World ? World->GetTimeSeconds() : 0.f;
    }
}

AGothicBossArenaManager::AGothicBossArenaManager()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;

    // Default aggression curve: index = pillars remaining
    // [0 pillars, 1 pillar, 2 pillars, 3 pillars, 4 pillars]
    AggressionByPillarCount = { 2.0f, 1.6f, 1.35f, 1.15f, 1.0f };

    // Ambient collapse interval, same indexing. Index 0 is never read.
    // UNMEASURED starting points — see the header.
    CollapseIntervalByPillarCount = { 0.f, 50.f, 60.f, 75.f, 90.f };
}

void AGothicBossArenaManager::BeginPlay()
{
    Super::BeginPlay();

    for (AGothicRotundaPillar* Pillar : Pillars)
    {
        if (Pillar)
        {
            Pillar->OnPillarDestroyed.AddDynamic(
                this, &AGothicBossArenaManager::OnPillarDestroyed);
        }
    }

    // Self-election. Every consumer of this actor reaches it through
    // GetActorOfClass, so whichever instance that call returns is the only one
    // anybody is actually talking to — and it is therefore the only one entitled
    // to run a timer. See bIsElectedManager in the header for why the level makes
    // this necessary.
    //
    // The bookkeeping above is deliberately NOT gated on this: every manager
    // keeps listening to its pillars, so GetPillarsRemaining and the aggression
    // curve stay correct on all four and a level cleanup that deletes the wrong
    // three changes nothing.
    const AActor* Elected = UGameplayStatics::GetActorOfClass(this, AGothicBossArenaManager::StaticClass());
    bIsElectedManager = (Elected == this);

    // Both outcomes, not just the abdication. "Which of the four managers is
    // running the clock" is unanswerable from a log that only speaks when the
    // answer is no — and the abdication was on LogTemp Verbose, which the
    // project's [Core.Log] clamp made invisible twice over.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|ELECTION|outcome=%s|elected=%s|pillars=%d"),
        ArenaTimelineNow(this), *GetName(),
        bIsElectedManager ? TEXT("elected") : TEXT("abdicated"),
        *GetNameSafe(Elected), GetPillarsRemaining());

    if (!bIsElectedManager)
    {
        return;
    }

    // An arena with no interval curve simply has no ambient clock. That is a
    // supported configuration — Wall Pound alone is a valid degradation model —
    // so there is nothing to say about it and nothing to poll for.
    if (CollapseIntervalByPillarCount.Num() == 0)
    {
        return;
    }

    if (!ResolveBoss())
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("BossArena[%s]: ambient collapse is configured but no boss could be found — ")
            TEXT("assign BossActor on the placed instance, or set BossClass on the Blueprint. ")
            TEXT("Without a combat signal the timer stays disarmed rather than collapsing the ")
            TEXT("arena while the player is walking through it."),
            *GetName());
        return;
    }

    LastPollWorldSeconds = ArenaTimelineNow(this);

    // The whole point of the poll: a BeginPlay-armed collapse clock would drop
    // the Rotunda's ceiling on a player who has not met the boss yet.
    GetWorldTimerManager().SetTimer(
        CombatPollTimerHandle, this,
        &AGothicBossArenaManager::PollCombatState,
        FMath::Max(0.1f, CombatPollInterval), true);
}

int32 AGothicBossArenaManager::GetPillarsRemaining() const
{
    int32 Count = 0;
    for (const AGothicRotundaPillar* Pillar : Pillars)
    {
        if (Pillar && !Pillar->IsDestroyed())
        {
            Count++;
        }
    }
    return Count;
}

float AGothicBossArenaManager::GetAggressionMultiplier() const
{
    const int32 Remaining = GetPillarsRemaining();

    if (AggressionByPillarCount.IsValidIndex(Remaining))
    {
        return AggressionByPillarCount[Remaining];
    }

    return 1.0f;
}

void AGothicBossArenaManager::ApplyCryDamage(float DamagePerPillar)
{
    int32 PillarsDamaged = 0;

    for (AGothicRotundaPillar* Pillar : Pillars)
    {
        if (Pillar && !Pillar->IsDestroyed())
        {
            Pillar->ApplyPillarDamage(DamagePerPillar);
            PillarsDamaged++;
        }
    }

}

AGothicRotundaPillar* AGothicBossArenaManager::GetNearestSurvivingPillar(FVector FromLocation) const
{
    AGothicRotundaPillar* Nearest = nullptr;
    float BestDist = TNumericLimits<float>::Max();

    for (AGothicRotundaPillar* Pillar : Pillars)
    {
        if (!Pillar || Pillar->IsDestroyed()) continue;

        const float Dist = FVector::Dist(FromLocation, Pillar->GetActorLocation());
        if (Dist < BestDist)
        {
            BestDist = Dist;
            Nearest = Pillar;
        }
    }

    return Nearest;
}

void AGothicBossArenaManager::OnPillarDestroyed(AGothicRotundaPillar* Pillar)
{
    const int32 Remaining  = GetPillarsRemaining();
    const float Aggression = GetAggressionMultiplier();

    // Both of these were once computed into locals and thrown away, and
    // GetAggressionMultiplier had no caller at all — so AggressionByPillarCount
    // ({2.0, 1.6, 1.35, 1.15, 1.0}) was entirely inert and knocking pillars down
    // changed nothing about the boss.
    //
    // The consumer now exists, and it POLLS rather than listening: the decision
    // pool (GothicBTService_WeightedActionSelect) and the reposition task both
    // call GetAggressionMultiplier directly. That is deliberate — BT nodes are
    // shared objects with per-instance NodeMemory, so a node cannot safely bind
    // a dynamic delegate. Two of the three candidates this comment used to list
    // are dead ends and are recorded here so they don't get re-proposed:
    //   - Cooldowns via AbilityHaste: no. Every enemy ability uses a
    //     fixed-duration cooldown GE; AbilityHaste has one consumer project-wide
    //     (the player's GA_Fire), so granting it to the boss does nothing.
    //   - A uniform multiplier over the action pool: mathematically a no-op. The
    //     roll is FRandRange(0, TotalWeight) against cumulative scores, so
    //     scaling every score by k scales TotalWeight by k and leaves the
    //     distribution identical. Aggression MUST be applied asymmetrically.
    // What landed is a per-entry AggressionWeightBonus (a bias, not a scale),
    // plus a shorter movement-commit window and a rarer menace-hold.
    //
    // The broadcast stays regardless: it is the hook a Blueprint can bind for
    // presentation (music, VFX, arena lighting) without touching C++.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|PILLAR-fell|pillar=%s|standing=%d|aggression=%.2f"),
        ArenaTimelineNow(this), *GetName(), *GetNameSafe(Pillar), Remaining, Aggression);

    OnArenaAggressionChanged.Broadcast(Aggression, Remaining);

    // Re-derive from here rather than only from the clock's own expiry, so a
    // pillar lost to ANY cause — Wall Pound, Cry damage, the ambient clock itself
    // — restarts the countdown at the new, shorter interval.
    //
    // This is what keeps the two pressures composing instead of stacking. Without
    // it, a Wall Pound landing ten seconds before an ambient expiry would take
    // two pillars in quick succession and the player would read the arena as
    // arbitrary. With it, taking a pillar yourself buys the same grace the clock
    // would have given, and the fight always has exactly one collapse pending.
    //
    // The progress reset is the load-bearing half of that promise and is stated
    // explicitly rather than left to fall out of a SetTimer call: each collapse
    // starts a FRESH countdown, not a carried remainder. The accumulator would
    // otherwise survive the re-derivation and a pillar lost at 80 banked seconds
    // would be followed instantly by another at the new 75s interval — the exact
    // double-collapse this re-derivation exists to prevent.
    //
    // Both guards matter. The abdicated managers are still bound to the same
    // pillars and still run this function, so the election check is what stops
    // them running a clock here that BeginPlay refused them; bFightActive is what
    // stops a pillar destroyed outside the fight starting one.
    if (bIsElectedManager && bFightActive)
    {
        ResetAmbientProgress(TEXT("pillar-fell"));
        ArmAmbientClock();
    }
}

AGothicEnemyBase* AGothicBossArenaManager::ResolveBoss()
{
    if (BossActor)
    {
        return BossActor;
    }

    if (BossClass)
    {
        BossActor = Cast<AGothicEnemyBase>(UGameplayStatics::GetActorOfClass(this, BossClass));
    }

    return BossActor;
}

void AGothicBossArenaManager::PollCombatState()
{
    AGothicEnemyBase* Boss = ResolveBoss();

    // The combat signal is the boss's own combat latch — the same one the AI
    // reads. It is set through SetCombatTarget, which every aggro source in the
    // project funnels through (perception, encounter volumes, pack propagation,
    // damage retaliation) and which refuses targets while the controller is
    // leashing home. So "she has a target" is exactly "the fight is on",
    // including the disengage: a boss that has leashed back to her anchor stops
    // the arena decaying, which is the behaviour a player who ran away should
    // get.
    //
    // Death is checked separately because the latch is not guaranteed to clear
    // on it, and a dead boss must not keep dropping the ceiling.
    const bool bNowActive = Boss && Boss->IsAlive() && Boss->GetCombatTarget() != nullptr;

    // Real elapsed time, not the nominal CombatPollInterval. Timer manager ticks
    // land where the frame lets them, and a clock that reports accumulated
    // seconds has to be accumulating seconds rather than counting its own ticks
    // and calling the product time.
    const float NowSeconds = ArenaTimelineNow(this);
    const float DeltaSeconds = FMath::Max(0.f, NowSeconds - LastPollWorldSeconds);
    LastPollWorldSeconds = NowSeconds;

    // ── The encounter reset ──────────────────────────────────────────────────
    //
    // OnLeashReset lives on AGothicEnemyAIController and broadcasts nothing, so
    // the manager infers it from its effect: the leash restores the boss to full
    // health. A living boss going from damaged back to full is a reset and
    // nothing else does that, which makes this read cheaper and less invasive
    // than a new delegate on the controller — and keeps the arena out of the
    // leash code, which is not this actor's business.
    //
    // Checked before the latch edge and unconditionally, because a leash reset
    // happens precisely while the latch is DOWN: she gives up, walks home, heals.
    // Gating it on the fight being active would make it unreachable.
    if (Boss && Boss->IsAlive())
    {
        const float MaxHealth = Boss->GetMaxHealth();
        const bool bAtFullHealth = MaxHealth > 0.f && Boss->GetHealth() >= MaxHealth - KINDA_SMALL_NUMBER;

        if (bAtFullHealth && bBossWasDamaged)
        {
            bBossWasDamaged = false;
            ResetAmbientProgress(TEXT("leash-reset"));
        }
        else if (!bAtFullHealth)
        {
            bBossWasDamaged = true;
        }
    }

    if (bNowActive == bFightActive)
    {
        // Steady state. The accumulator only advances while the latch is up —
        // this is the line that makes the clock measure COMBAT time rather than
        // wall time, and it sits after the early-return's condition rather than
        // inside the transition block below because most polls are steady state.
        if (bFightActive)
        {
            TickAmbientClock(DeltaSeconds);
        }
        return;
    }

    bFightActive = bNowActive;

    // TRANSITIONS ONLY — this line sits below the equality early-return above on
    // purpose. The poll runs on a repeating timer for the whole level's lifetime,
    // so a line per tick would bury every other BossArena event in the stream;
    // what a reader needs is the two instants the latch moved, and the boss state
    // that moved it.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|POLL|fightActive=%d|boss=%s|alive=%d|target=%s"),
        ArenaTimelineNow(this), *GetName(), bFightActive ? 1 : 0,
        *GetNameSafe(Boss), (Boss && Boss->IsAlive()) ? 1 : 0,
        *GetNameSafe(Boss ? Boss->GetCombatTarget() : nullptr));

    if (bFightActive)
    {
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|BossArena|FIGHT-started|standing=%d"),
            ArenaTimelineNow(this), *GetName(), GetPillarsRemaining());

        // Derive first (the pillar count may have changed while she was away),
        // then announce the carried progress.
        ArmAmbientClock();
        ResumeAmbientClock();
    }
    else
    {
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|BossArena|FIGHT-ended|reason=%s"),
            ArenaTimelineNow(this), *GetName(),
            (Boss && Boss->IsAlive()) ? TEXT("disengaged") : TEXT("boss-dead-or-missing"));
        PauseAmbientClock();
    }
}

void AGothicBossArenaManager::ArmAmbientClock()
{
    const int32 Remaining = GetPillarsRemaining();

    // Every early return below leaves the clock OFF rather than half-configured.
    // A stale CurrentCollapseInterval from a richer arena would keep the
    // accumulator racing a target that no longer exists.
    CurrentCollapseInterval = 0.f;

    // Nothing left to take. The endgame is four sealed zones and maximum
    // aggression, not a wipe timer — so the clock simply stops here and the
    // fight finishes on its own terms.
    if (Remaining <= 0)
    {
        return;
    }

    if (!CollapseIntervalByPillarCount.IsValidIndex(Remaining))
    {
        // Short or empty array. Silent by design: an unconfigured curve means
        // "this arena does not want an ambient clock", and warning about it on
        // every re-arm would be noise in the one configuration most arenas use.
        return;
    }

    const float Interval = CollapseIntervalByPillarCount[Remaining];
    if (Interval <= 0.f)
    {
        return;
    }

    CurrentCollapseInterval = Interval;

    // elapsed= is the whole point of this line now. TIMER-armed used to be
    // indistinguishable between "the fight just started" and "she re-engaged 40
    // seconds into a countdown", which is exactly the ambiguity that let the old
    // restart-on-every-drop bug hide in two full verification runs. Whatever is
    // banked is on the line that arms the clock.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|TIMER-armed|interval=%.0f|elapsed=%.1f|standing=%d"),
        ArenaTimelineNow(this), *GetName(), Interval, AccumulatedCombatSeconds, Remaining);
}

void AGothicBossArenaManager::PauseAmbientClock()
{
    // No state to change — the accumulator advances only while bFightActive, so
    // dropping the latch pauses it by construction. This exists to SAY so.
    //
    // It replaces the old TIMER-disarmed line, and the rename is the point: a
    // reader who sees "disarmed" reasonably concludes the countdown is gone, and
    // in the old build they were right. "paused" with the banked seconds beside
    // it is the semantics, printed.
    if (CurrentCollapseInterval <= 0.f)
    {
        return;
    }

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|TIMER-paused|elapsed=%.1f|interval=%.0f|standing=%d"),
        ArenaTimelineNow(this), *GetName(), AccumulatedCombatSeconds,
        CurrentCollapseInterval, GetPillarsRemaining());
}

void AGothicBossArenaManager::ResumeAmbientClock()
{
    if (CurrentCollapseInterval <= 0.f)
    {
        return;
    }

    // remaining= is derived rather than stored so it can never disagree with the
    // accumulator it is derived from.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|TIMER-resumed|elapsed=%.1f|remaining=%.1f|standing=%d"),
        ArenaTimelineNow(this), *GetName(), AccumulatedCombatSeconds,
        FMath::Max(0.f, CurrentCollapseInterval - AccumulatedCombatSeconds),
        GetPillarsRemaining());
}

void AGothicBossArenaManager::ResetAmbientProgress(const TCHAR* Reason)
{
    // The two callers are the only two events that mean "the countdown you were
    // watching is over": the boss healing to full (the encounter reset) and a
    // pillar falling (a new, shorter countdown at a new count). Death and
    // disengage are deliberately NOT among them — that is the entire behavioural
    // change, and adding a third caller here undoes it.
    const float Discarded = AccumulatedCombatSeconds;
    AccumulatedCombatSeconds = 0.f;

    if (Discarded <= 0.f)
    {
        // Nothing was banked. Silent: a reset that discarded nothing is not an
        // event, and a boss sitting at full health would otherwise print one per
        // poll forever.
        return;
    }

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|TIMER-reset|reason=%s|discarded=%.1f|standing=%d"),
        ArenaTimelineNow(this), *GetName(), Reason, Discarded, GetPillarsRemaining());
}

void AGothicBossArenaManager::TickAmbientClock(float DeltaSeconds)
{
    if (CurrentCollapseInterval <= 0.f)
    {
        return;
    }

    AccumulatedCombatSeconds += DeltaSeconds;

    if (AccumulatedCombatSeconds < CurrentCollapseInterval)
    {
        return;
    }

    // Crossing is not itself the reset. TriggerAmbientCollapse takes a pillar,
    // which reaches OnPillarDestroyed, which resets the progress and re-derives
    // the interval — one path for every cause of a pillar loss. The only case
    // that path does not cover is a collapse that found no survivors, which
    // TriggerAmbientCollapse handles by shutting the clock off.
    TriggerAmbientCollapse();
}

void AGothicBossArenaManager::TriggerAmbientCollapse()
{
    // Random rather than nearest or farthest. The boss-driven destroyer is
    // already positional — Wall Pound takes whatever she is standing next to —
    // so making the ambient one positional too would give the player a second
    // thing to steer. Random keeps it as weather: it says the building is
    // failing, not that anything in particular chose this pillar.
    TArray<AGothicRotundaPillar*> Survivors;
    for (AGothicRotundaPillar* Pillar : Pillars)
    {
        if (Pillar && !Pillar->IsDestroyed())
        {
            Survivors.Add(Pillar);
        }
    }

    if (Survivors.Num() == 0)
    {
        // The endgame. Nothing left to take, so the clock stops for good rather
        // than crossing its threshold on every subsequent poll and re-entering
        // here once a second for the rest of the fight.
        CurrentCollapseInterval = 0.f;
        AccumulatedCombatSeconds = 0.f;
        return;
    }

    AGothicRotundaPillar* Doomed = Survivors[FMath::RandRange(0, Survivors.Num() - 1)];

    // The firing token, and the only place it appears. It has correctly never
    // been seen in a verification log — under the old restart-on-drop clock it
    // was unreachable, not merely rare — so its first appearance is the proof
    // that this fix landed.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|AMBIENT-collapse|pillar=%s|survivors=%d|accumulated=%.1f|interval=%.0f"),
        ArenaTimelineNow(this), *GetName(), *GetNameSafe(Doomed), Survivors.Num(),
        AccumulatedCombatSeconds, CurrentCollapseInterval);

    // The same entry point Wall Pound uses, so the player gets the same read:
    // the full WarningDuration telegraph, then the slab. An ambient collapse
    // that skipped the warning would be an unavoidable hit from a system the
    // player cannot see coming, which is the one thing the telegraph exists to
    // prevent.
    Doomed->TriggerWallCollapse();

    // TriggerWallCollapse marks the pillar destroyed immediately (the slab lands
    // WarningDuration later), so OnPillarDestroyed has already fired by now and
    // has already zeroed the accumulator and re-derived the interval at the new
    // count. Nothing to do here — resetting again would be harmless but would
    // put two TIMER-reset lines against one collapse and make the log lie about
    // how many countdowns ended.
}
