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
    UE_LOG(LogTemp, Log,
        TEXT("BossArena[%s]: %s fell — %d pillar(s) standing, aggression x%.2f"),
        *GetName(), *GetNameSafe(Pillar), Remaining, Aggression);

    OnArenaAggressionChanged.Broadcast(Aggression, Remaining);
}