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

    UE_LOG(LogTemp, Log, TEXT("BossArenaManager: Tracking %d pillars"), Pillars.Num());
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

    UE_LOG(LogTemp, Log, TEXT("BossArenaManager: Cry damaged %d pillars (%.1f each) | %d remaining"),
        PillarsDamaged, DamagePerPillar, GetPillarsRemaining());
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
    const int32 Remaining = GetPillarsRemaining();
    const float Aggression = GetAggressionMultiplier();

    UE_LOG(LogTemp, Log, TEXT("BossArenaManager: Pillar destroyed! %d remaining | Aggression: %.2f"),
        Remaining, Aggression);
}