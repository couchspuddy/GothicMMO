// GothicBossArenaManager.cpp

#include "AI/GothicBossArenaManager.h"
#include "AI/GothicRotundaPillar.h"

AGothicBossArenaManager::AGothicBossArenaManager()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;

    // Default aggression curve: index = pillars remaining
    // [0 pillars, 1 pillar, 2 pillars, 3 pillars, 4 pillars]
    AggressionByPillarCount = { 2.0f, 1.6f, 1.35f, 1.15f, 1.0f };
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

    // Both of these were computed into locals and thrown away, and
    // GetAggressionMultiplier had no other caller — so AggressionByPillarCount
    // ({2.0, 1.6, 1.35, 1.15, 1.0}) was entirely inert and knocking pillars down
    // changed nothing about the boss. The escalation the whole pillar mechanic
    // exists to drive never happened.
    //
    // WHAT the multiplier should scale is an open design decision and is
    // deliberately NOT chosen here. The three candidates, all defensible:
    //   1. Movement — scale the boss's DefaultWalkSpeed, so a pillarless arena
    //      gives her less recovery time between attacks. Most legible to a player.
    //   2. Decision weights — fold it into GothicBTService_WeightedActionSelect's
    //      RecklessFactor, so she picks aggressive actions more often. Closest to
    //      the word "aggression", but it changes the tuned action pool.
    //   3. Cooldowns — scale AbilityHaste, so her whole kit comes back faster.
    //      Largest DPS swing and the hardest to tune safely.
    // Each is a combat-feel change that wants a measurement, not a guess.
    //
    // Broadcasting is the part that is unambiguously correct regardless of which
    // one lands: the value reaches a BlueprintAssignable delegate the boss BP can
    // bind today, and whichever option is chosen becomes a listener rather than a
    // rewrite of this function.
    UE_LOG(LogTemp, Log,
        TEXT("BossArena[%s]: %s fell — %d pillar(s) standing, aggression x%.2f"),
        *GetName(), *GetNameSafe(Pillar), Remaining, Aggression);

    OnArenaAggressionChanged.Broadcast(Aggression, Remaining);
}