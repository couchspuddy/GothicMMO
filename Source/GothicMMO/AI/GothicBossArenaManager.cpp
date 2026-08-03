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

    // The whole point of the poll: a BeginPlay-armed collapse timer would drop
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

    // Re-arm from here rather than only from the timer's own expiry, so a pillar
    // lost to ANY cause — Wall Pound, Cry damage, the ambient timer itself —
    // restarts the clock at the new, shorter interval.
    //
    // This is what keeps the two pressures composing instead of stacking. Without
    // it, a Wall Pound landing ten seconds before an ambient expiry would take
    // two pillars in quick succession and the player would read the arena as
    // arbitrary. With it, taking a pillar yourself buys the same grace the timer
    // would have given, and the fight always has exactly one collapse pending.
    //
    // Both guards matter. The abdicated managers are still bound to the same
    // pillars and still run this function, so the election check is what stops
    // them arming a timer here that BeginPlay refused them; bFightActive is what
    // stops a pillar destroyed outside the fight starting a clock.
    if (bIsElectedManager && bFightActive)
    {
        ArmAmbientCollapseTimer();
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

    if (bNowActive == bFightActive)
    {
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
        ArmAmbientCollapseTimer();
    }
    else
    {
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|BossArena|FIGHT-ended|reason=%s"),
            ArenaTimelineNow(this), *GetName(),
            (Boss && Boss->IsAlive()) ? TEXT("disengaged") : TEXT("boss-dead-or-missing"));
        DisarmAmbientCollapseTimer();
    }
}

void AGothicBossArenaManager::ArmAmbientCollapseTimer()
{
    DisarmAmbientCollapseTimer();

    const int32 Remaining = GetPillarsRemaining();

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

    GetWorldTimerManager().SetTimer(
        AmbientCollapseTimerHandle, this,
        &AGothicBossArenaManager::TriggerAmbientCollapse,
        Interval, false);

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|TIMER-armed|interval=%.0f|standing=%d"),
        ArenaTimelineNow(this), *GetName(), Interval, Remaining);
}

void AGothicBossArenaManager::DisarmAmbientCollapseTimer()
{
    // Only speak when there was actually a clock to stop. ArmAmbientCollapseTimer
    // opens by disarming, so an unconditional line would print a disarm in front
    // of every arm and read as a clock thrashing on and off.
    const bool bWasArmed = GetWorldTimerManager().IsTimerActive(AmbientCollapseTimerHandle);

    GetWorldTimerManager().ClearTimer(AmbientCollapseTimerHandle);

    if (bWasArmed)
    {
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|BossArena|TIMER-disarmed|standing=%d"),
            ArenaTimelineNow(this), *GetName(), GetPillarsRemaining());
    }
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
        return;
    }

    AGothicRotundaPillar* Doomed = Survivors[FMath::RandRange(0, Survivors.Num() - 1)];

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|BossArena|AMBIENT-collapse|pillar=%s|survivors=%d"),
        ArenaTimelineNow(this), *GetName(), *GetNameSafe(Doomed), Survivors.Num());

    // The same entry point Wall Pound uses, so the player gets the same read:
    // the full WarningDuration telegraph, then the slab. An ambient collapse
    // that skipped the warning would be an unavoidable hit from a system the
    // player cannot see coming, which is the one thing the telegraph exists to
    // prevent.
    Doomed->TriggerWallCollapse();

    // TriggerWallCollapse marks the pillar destroyed immediately (the slab lands
    // WarningDuration later), so OnPillarDestroyed has already fired by now and
    // has already re-armed this timer at the new count. Nothing to do here —
    // re-arming again would double-book the clock.
}
