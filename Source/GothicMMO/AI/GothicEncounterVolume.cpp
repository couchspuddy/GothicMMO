// GothicEncounterVolume.cpp

#include "AI/GothicEncounterVolume.h"
#include "AI/GothicEnemyBase.h"
#include "Game/GothicGameState.h"
#include "Game/GothicPlayerState.h"
#include "Character/GothicPlayerCharacter.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AI/GothicEnemySpawnPoint.h"
#include "Components/BoxComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "GothicEnemySpawnPoint.h"
#include "Engine/World.h"

AGothicEncounterVolume::AGothicEncounterVolume()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false; // effects go through GameState, this actor itself never needs to replicate

    TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
    SetRootComponent(TriggerBox);

    TriggerBox->SetBoxExtent(FVector(500.f, 500.f, 300.f));
    TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    TriggerBox->SetCollisionObjectType(ECC_WorldStatic);
    TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    TriggerBox->SetGenerateOverlapEvents(true);
    TriggerBox->SetHiddenInGame(true);
    TriggerBox->ShapeColor = FColor(255, 128, 0);
}

void AGothicEncounterVolume::BeginPlay()
{
    Super::BeginPlay();

    if (!HasAuthority())
    {
        return; // encounter tracking is server-only
    }
    if (bAggroEnemiesOnOverlap && TriggerBox)
    {
        TriggerBox->OnComponentBeginOverlap.AddDynamic(
            this, &AGothicEncounterVolume::HandleTriggerBeginOverlap);
    }

    RemainingEnemyCount = EncounterEnemies.Num();

    if (RemainingEnemyCount == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("AGothicEncounterVolume %s: No enemies assigned — this encounter will never complete"),
            *GetName());
        return;
    }

    for (AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        if (!Enemy)
        {
            UE_LOG(LogTemp, Warning, TEXT("AGothicEncounterVolume %s: Null entry in EncounterEnemies — check level placement"),
                *GetName());
            continue;
        }

        Enemy->OwningEncounter = this;
        Enemy->OnEnemyDied.AddDynamic(this, &AGothicEncounterVolume::HandleEnemyDied);
    }
}

void AGothicEncounterVolume::HandleTriggerBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    // bReplicates is false, so this actor exists independently on every machine and
    // this overlap fires locally on clients too. Aggro is a server decision.
    if (!HasAuthority() || bAggroTriggered)
    {
        return;
    }

    AGothicPlayerCharacter* Player = Cast<AGothicPlayerCharacter>(OtherActor);
    if (!Player)
    {
        return;
    }

    bAggroTriggered = true;

    int32 AggroedCount = 0;
    for (AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        if (Enemy)
        {
            Enemy->SetCombatTarget(Player);
            ++AggroedCount;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("AGothicEncounterVolume %s: %s entered — aggroed %d/%d enemies"),
        *GetName(), *Player->GetName(), AggroedCount, EncounterEnemies.Num());
}

// AGothicEncounterVolume.cpp — replace HandleEnemyDied's completion block
void AGothicEncounterVolume::HandleEnemyDied(AGothicEnemyBase* DeadEnemy)
{
    if (!HasAuthority())
    {
        return;
    }

    RemainingEnemyCount = FMath::Max(0, RemainingEnemyCount - 1);
    LastEnemyToDie = DeadEnemy;

    UE_LOG(LogTemp, Log, TEXT("AGothicEncounterVolume %s: %s died — %d remaining"),
        *GetName(), DeadEnemy ? *DeadEnemy->GetName() : TEXT("Unknown"), RemainingEnemyCount);

    if (RemainingEnemyCount <= 0)
    {
        CachedTotalSelah = 0.f;
        CachedGainEffect = nullptr;
        for (AGothicEnemyBase* Enemy : EncounterEnemies)
        {
            if (!Enemy) continue;
            CachedTotalSelah += Enemy->SelahAwardAmount;
            if (!CachedGainEffect && Enemy->SelahGainEffect)
            {
                CachedGainEffect = Enemy->SelahGainEffect;
            }
        }

        AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
        if (GS)
        {
            // Collect Accursed names — set BEFORE the prompt corpse so
            // they're available when OnEncounterPromptActivated fires.
            TArray<FText> Names;
            for (AGothicEnemyBase* Enemy : EncounterEnemies)
            {
                if (Enemy && !Enemy->GetAccursedName().IsEmpty())
                {
                    Names.Add(Enemy->GetAccursedName());
                }
            }
            GS->SetSelahNames(Names);

            GS->SetActivePromptCorpse(LastEnemyToDie);
            UE_LOG(LogTemp, Log, TEXT("AGothicEncounterVolume %s: Complete — prompt on %s, cached %.1f Selah"),
                *GetName(), LastEnemyToDie ? *LastEnemyToDie->GetName() : TEXT("Unknown"), CachedTotalSelah);
        }
    }
    OnEncounterMemberDied.Broadcast(DeadEnemy);
}

// AGothicEncounterVolume.cpp — replace CompleteCollection entirely
void AGothicEncounterVolume::CompleteCollection()
{
    if (!HasAuthority() || !IsComplete())
    {
        return;
    }

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
    if (!GS || GS->GetActivePromptCorpse() != LastEnemyToDie)
    {
        return;
    }
    // GothicEncounterVolume.cpp — insert at the top of CompleteCollection(),
    // right after the existing ActivePromptCorpse validity check, before Selah is awarded
    // GothicEncounterVolume.cpp — replace last message's interrupt branch in CompleteCollection
    if (!bPendingWaveTriggered && PendingWaveSpawnPoints.Num() > 0)
    {
        bPendingWaveTriggered = true;

        TArray<AGothicEnemyBase*> SpawnedWave;
        for (AGothicEnemySpawnPoint* Point : PendingWaveSpawnPoints)
        {
            if (!Point || !Point->EnemyClass) continue;

            AGothicEnemyBase* NewEnemy = GetWorld()->SpawnActor<AGothicEnemyBase>(
                Point->EnemyClass, Point->GetActorTransform());

            if (NewEnemy)
            {
                // Pack stamp AFTER spawn, through the setter — BeginPlay has
                // already run inside SpawnActor and saw NAME_None; SetPackID
                // is the convergence point for both assignment paths.
                if (!Point->PackID.IsNone())
                {
                    NewEnemy->SetPackID(Point->PackID);
                }

                SpawnedWave.Add(NewEnemy);
            }
        }

        UE_LOG(LogTemp, Log, TEXT("AGothicEncounterVolume %s: Spawned wave of %d on first collection attempt"),
            *GetName(), SpawnedWave.Num());

        AddWaveToEncounter(SpawnedWave); // retracts the prompt too
        return; // no reward yet — Wave 2 has to fall first
    }

    // ...existing reward/checkpoint/corpse-cleanup code continues unchanged below...
    if (CachedGainEffect)
    {
        for (APlayerState* PS : GS->PlayerArray)
        {
            AGothicPlayerState* GothicPS = Cast<AGothicPlayerState>(PS);
            UGothicAbilitySystemComponent* ASC = GothicPS ? GothicPS->GetGothicASC() : nullptr;
            if (!ASC) continue;

            FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
            Context.AddSourceObject(this);
            FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(CachedGainEffect, 1.f, Context);

            if (Spec.IsValid())
            {
                Spec.Data->SetSetByCallerMagnitude(
                    FGameplayTag::RequestGameplayTag(FName("Data.Selah")), CachedTotalSelah);
                ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
            }

            if (AGothicPlayerCharacter* PlayerChar = Cast<AGothicPlayerCharacter>(GothicPS->GetPawn()))
            {
                PlayerChar->TriggerSelahMoment();
            }
        }
    }

    GS->CheckpointLocation = GetActorLocation();
    GS->SetSelahNames(TArray<FText>());
    GS->SetActivePromptCorpse(nullptr);

    // Reward is secured — safe to clean up every corpse in this encounter now.
    for (AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        if (Enemy)
        {
            Enemy->Destroy();
        }
    }
}

void AGothicEncounterVolume::AddWaveToEncounter(const TArray<AGothicEnemyBase*>& NewWaveEnemies)
{
    if (!HasAuthority())
    {
        return;
    }

    int32 AddedCount = 0;
    for (AGothicEnemyBase* Enemy : NewWaveEnemies)
    {
        if (!Enemy) continue;

        EncounterEnemies.Add(Enemy);
        Enemy->OwningEncounter = this;
        Enemy->OnEnemyDied.AddDynamic(this, &AGothicEncounterVolume::HandleEnemyDied);
        ++AddedCount;
    }

    RemainingEnemyCount += AddedCount;

    UE_LOG(LogTemp, Log, TEXT("AGothicEncounterVolume %s: Added wave of %d — %d now remaining"),
        *GetName(), AddedCount, RemainingEnemyCount);

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
    if (GS && LastEnemyToDie && GS->GetActivePromptCorpse() == LastEnemyToDie)
    {
        GS->SetSelahNames(TArray<FText>());
        GS->SetActivePromptCorpse(nullptr);
        UE_LOG(LogTemp, Log, TEXT("AGothicEncounterVolume %s: Prompt retracted — new wave arrived"),
            *GetName());
    }
}