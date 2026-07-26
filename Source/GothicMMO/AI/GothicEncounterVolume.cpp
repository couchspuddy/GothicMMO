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
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

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

    int32 BoundCount = 0;

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
        ++BoundCount;
    }

    // Roster trace. `bound` is the useful half: it counts enemies that actually
    // got an OnEnemyDied binding, so a bound lower than RemainingEnemyCount means
    // deaths can never drive the counter to zero and the encounter would hang.
    //
    // Expect the roster to GROW later on some encounters — GA_FeralBreakout folds
    // the Retained's reinforcements in, and the interrupt wave adds its own. Both
    // log through AddWaveToEncounter, so a count that rises without one of those
    // lines is the thing worth investigating.
    UE_LOG(LogTemp, Verbose,
        TEXT("Selah[%s]: BeginPlay — roster=%d, bound=%d, RemainingEnemyCount=%d"),
        *GetName(), EncounterEnemies.Num(), BoundCount, RemainingEnemyCount);
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

    for (AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        if (Enemy)
        {
            Enemy->SetCombatTarget(Player);
        }
    }
}

void AGothicEncounterVolume::HandleEnemyDied(AGothicEnemyBase* DeadEnemy)
{
    if (!HasAuthority())
    {
        return;
    }

    RemainingEnemyCount = FMath::Max(0, RemainingEnemyCount - 1);
    LastEnemyToDie = DeadEnemy;

    UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: %s died — %d remaining, WaveStage=%d"),
        *GetName(), *GetNameSafe(DeadEnemy), RemainingEnemyCount, WaveStage);

    if (RemainingEnemyCount <= 0)
    {
        // Interrupt wave (Wave 2) just fell -> the "one more after" spawns
        // automatically without a prompt, if configured.
        if (WaveStage == 2 && Wave3SpawnPoints.Num() > 0)
        {
            WaveStage = 3;
            SpawnWaveFromPoints(Wave3SpawnPoints);
            OnEncounterMemberDied.Broadcast(DeadEnemy);
            return;
        }

        // Interrupt wave fell with no Wave 3, or Wave 3 itself fell -> the next
        // collection is the real one. (WaveStage 0 stays 0: the first collect
        // still has to trigger the interrupt.)
        if (WaveStage == 2 || WaveStage == 3)
        {
            WaveStage = 4;
        }

        ActivateSelahPrompt();
    }

    OnEncounterMemberDied.Broadcast(DeadEnemy);
}

void AGothicEncounterVolume::ActivateSelahPrompt()
{
    CachedTotalSelah = 0.f;
    CachedGainEffect = nullptr;
    for (AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        if (!Enemy) continue;
        CachedTotalSelah += Enemy->GetSelahAwardAmount();
        if (!CachedGainEffect && Enemy->GetSelahGainEffect())
        {
            CachedGainEffect = Enemy->GetSelahGainEffect();
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
        // Anchor the prompt to THIS encounter (the area), not the corpse — the
        // corpse may despawn while the meditation prompt is still up.
        GS->AddEncounterPrompt(this, LastEnemyToDie);

        UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: prompt raised — %.1f Selah pooled, %d names, GainEffect=%s"),
            *GetName(), CachedTotalSelah, Names.Num(), *GetNameSafe(CachedGainEffect));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Selah[%s]: prompt NOT raised — no AGothicGameState"), *GetName());
    }
}

TArray<AGothicEnemyBase*> AGothicEncounterVolume::SpawnWaveFromPoints(
    const TArray<TObjectPtr<AGothicEnemySpawnPoint>>& Points)
{
    TArray<AGothicEnemyBase*> Spawned;
    UWorld* World = GetWorld();
    if (!World)
    {
        return Spawned;
    }

    for (AGothicEnemySpawnPoint* Point : Points)
    {
        if (!Point || !Point->EnemyClass) continue;

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        AGothicEnemyBase* NewEnemy = World->SpawnActor<AGothicEnemyBase>(
            Point->EnemyClass, Point->GetActorLocation(), Point->GetActorRotation(), SpawnParams);

        if (NewEnemy)
        {
            // Pack stamp AFTER spawn, through the setter — BeginPlay has
            // already run inside SpawnActor and saw NAME_None; SetPackID
            // is the convergence point for both assignment paths.
            if (!Point->PackID.IsNone())
            {
                NewEnemy->SetPackID(Point->PackID);
            }
            Spawned.Add(NewEnemy);
        }
    }

    AddWaveToEncounter(Spawned); // registers deaths, bumps the count, retracts any prompt
    return Spawned;
}

void AGothicEncounterVolume::SpawnInterruptWave()
{
    if (!HasAuthority())
    {
        return;
    }

    WaveStage = 2;

    // Break the collection bar — the fake-out lands.
    if (AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr)
    {
        GS->SetSelahCollectPhase(2 /*interrupted*/, 0.f);
    }

    const TArray<AGothicEnemyBase*> Spawned = SpawnWaveFromPoints(PendingWaveSpawnPoints);
    UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: interrupt wave fired — %d/%d spawn points produced enemies"),
        *GetName(), Spawned.Num(), PendingWaveSpawnPoints.Num());
}

void AGothicEncounterVolume::CompleteCollection()
{
    if (!HasAuthority() || !IsComplete())
    {
        UE_LOG(LogTemp, Warning, TEXT("Selah[%s]: CompleteCollection refused — Authority=%d Remaining=%d"),
            *GetName(), HasAuthority() ? 1 : 0, RemainingEnemyCount);
        return;
    }

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
    if (!GS || !GS->IsPromptPending(this))
    {
        UE_LOG(LogTemp, Warning, TEXT("Selah[%s]: CompleteCollection refused — prompt owner is %s, not this volume"),
            *GetName(), GS ? TEXT("<not pending>") : TEXT("<no GameState>"));
        return;
    }

    // The first collection of a waved encounter is the fake-out; any other
    // collection (the final one, or a plain encounter) fills to full and rewards.
    const bool bInterruptCollect = (WaveStage == 0 && PendingWaveSpawnPoints.Num() > 0);

    UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: collect started — WaveStage=%d PendingWavePoints=%d -> %s"),
        *GetName(), WaveStage, PendingWaveSpawnPoints.Num(),
        bInterruptCollect ? TEXT("INTERRUPT (fake-out)") : TEXT("FINALIZE (reward)"));

    // Begin the shared collection channel (the fill-bar) and retract the meditation
    // prompt — collection has started.
    GS->ClearEncounterPrompt(this);
    GS->SetSelahCollectPhase(1 /*collecting*/, SelahCollectDuration);

    if (bInterruptCollect)
    {
        WaveStage = 1;
        GetWorld()->GetTimerManager().SetTimer(
            InterruptTimerHandle, this, &AGothicEncounterVolume::SpawnInterruptWave,
            FMath::Max(0.01f, InterruptDelay), false);
    }
    else
    {
        GetWorld()->GetTimerManager().SetTimer(
            CollectFinishHandle, this, &AGothicEncounterVolume::FinalizeCollection,
            FMath::Max(0.01f, SelahCollectDuration), false);
    }
}

void AGothicEncounterVolume::FinalizeCollection()
{
    if (!HasAuthority())
    {
        return;
    }

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
    if (!GS)
    {
        return;
    }

    // Fill the bar to full — the collection landed.
    GS->SetSelahCollectPhase(3 /*completed*/, 0.f);

    UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: FINALIZED — awarding %.1f Selah to %d player(s), WaveStage=%d"),
        *GetName(), CachedTotalSelah, GS->PlayerArray.Num(), WaveStage);

    // CachedGainEffect gates the Selah *award* only. It used to gate this whole
    // block, so an encounter whose enemies all left SelahGainEffect unset — the
    // Feral Retained's default — filled the bar and then silently did nothing: no
    // moment, no checkpoint, no corpse cleanup. The moment is the payoff for
    // clearing the encounter and must play whether or not the currency lands.
    if (!CachedGainEffect)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("AGothicEncounterVolume %s: no enemy in this encounter has a SelahGainEffect — "
                 "the moment plays but no Selah is awarded. Assign GE_SelahGain on the enemy Blueprints."),
            *GetName());
    }

    for (APlayerState* PS : GS->PlayerArray)
    {
        AGothicPlayerState* GothicPS = Cast<AGothicPlayerState>(PS);
        UGothicAbilitySystemComponent* ASC = GothicPS ? GothicPS->GetGothicASC() : nullptr;
        if (!ASC) continue;

        if (CachedGainEffect)
        {
            FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
            Context.AddSourceObject(this);
            FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(CachedGainEffect, 1.f, Context);

            if (Spec.IsValid())
            {
                Spec.Data->SetSetByCallerMagnitude(
                    FGameplayTag::RequestGameplayTag(FName("Data.Selah")), CachedTotalSelah);
                ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
            }
        }

        if (AGothicPlayerCharacter* PlayerChar = Cast<AGothicPlayerCharacter>(GothicPS->GetPawn()))
        {
            PlayerChar->TriggerSelahMoment();
        }
    }

    // The name reveal belongs to the reward, not to the prompt. Blueprint drives
    // the cycle off SelahNames from here — which is why FinalizeCollection no
    // longer clears that array before this point.
    GS->OnSelahMomentStarted();

    // Run complete — start the trip home, timed to outlast the name reveal.
    if (bReturnToHubOnComplete)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: run complete — returning to %s in %.1fs"),
            *GetName(), *ReturnHubLevelName.ToString(), ReturnDelaySeconds);

        GetWorld()->GetTimerManager().SetTimer(
            ReturnHubHandle, this, &AGothicEncounterVolume::ReturnToHub,
            FMath::Max(0.1f, ReturnDelaySeconds), false);
    }

    GS->CheckpointLocation = GetActorLocation();

    // SelahNames is deliberately NOT cleared here. The moment that just started
    // cycles those names over several seconds, and wiping the replicated array in
    // the same frame left the reveal blank. ActivateSelahPrompt overwrites the
    // array wholesale on the next encounter, and AddWaveToEncounter clears it when
    // a wave retracts the prompt — so nothing leaks.

    // Reward is secured — safe to clean up every corpse in this encounter now.
    for (AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        if (Enemy)
        {
            Enemy->Destroy();
        }
    }
}

void AGothicEncounterVolume::ReturnToHub()
{
    if (!HasAuthority() || ReturnHubLevelName.IsNone())
    {
        return;
    }

    UGameplayStatics::OpenLevel(this, ReturnHubLevelName);
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

    // The only path that can grow a roster after BeginPlay, and it has two
    // legitimate callers: the interrupt wave, and GA_FeralBreakout folding in the
    // Retained's reinforcements. Pairing the added count with the new totals is
    // what distinguishes those from a roster growing for no reason.
    UE_LOG(LogTemp, Verbose,
        TEXT("Selah[%s]: wave folded in — +%d member(s), roster=%d, RemainingEnemyCount=%d"),
        *GetName(), AddedCount, EncounterEnemies.Num(), RemainingEnemyCount);

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
    if (GS && GS->IsPromptPending(this))
    {
        GS->SetSelahNames(TArray<FText>());
        GS->ClearEncounterPrompt(this);
    }
}

// ---------------------------------------------------------------------------
// gothic.collect — debug console command
//
// The Selah collect is only reachable through the player's
// ServerCollectEncounterSelah RPC, which takes the encounter as a parameter.
// The `ke` console command cannot pass parameters, so before this there was no
// way to drive a collect from a script: the wave chain (roster clears ->
// prompt -> collect spawns Wave 2 -> Wave 2 clears -> Wave 3) could only be
// exercised by a human pressing the interact key, which meant the half of the
// encounter past the prompt went untested.
//
// Picks the pending encounter nearest the local pawn so it behaves like an
// actual interact rather than firing every prompt in the level at once.
// ---------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
static void GothicCollectConsoleCommand(UWorld* World)
{
    if (!World)
    {
        return;
    }

    AGothicGameState* GS = World->GetGameState<AGothicGameState>();
    if (!GS)
    {
        UE_LOG(LogTemp, Warning, TEXT("gothic.collect: no AGothicGameState in this world."));
        return;
    }

    // Measure from the pawn so "nearest" means nearest to the player, matching
    // what an interact would have collected.
    FVector From = FVector::ZeroVector;
    if (const APlayerController* PC = World->GetFirstPlayerController())
    {
        if (const APawn* Pawn = PC->GetPawn())
        {
            From = Pawn->GetActorLocation();
        }
    }

    AGothicEncounterVolume* Best = nullptr;
    float BestDistSq = TNumericLimits<float>::Max();

    for (TActorIterator<AGothicEncounterVolume> It(World); It; ++It)
    {
        AGothicEncounterVolume* Volume = *It;
        if (!GS->IsPromptPending(Volume))
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(From, Volume->GetActorLocation());
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            Best = Volume;
        }
    }

    if (!Best)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("gothic.collect: no encounter currently has a prompt pending. Clear a roster first."));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("gothic.collect: collecting %s (%.0fuu away)."),
        *Best->GetName(), FMath::Sqrt(BestDistSq));

    // Deliberately the same entry point the RPC uses, so this exercises the real
    // path -- including its own authority and prompt-ownership guards -- rather
    // than a debug shortcut that could pass while the real one is broken.
    Best->CompleteCollection();
}

static FAutoConsoleCommandWithWorld GGothicCollectCmd(
    TEXT("gothic.collect"),
    TEXT("Collect the pending Selah prompt nearest the player. Debug only."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&GothicCollectConsoleCommand));
#endif
