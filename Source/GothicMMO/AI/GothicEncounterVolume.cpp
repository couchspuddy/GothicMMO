// GothicEncounterVolume.cpp

#include "AI/GothicEncounterVolume.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AI/GothicEnemyBase.h"
#include "Game/GothicDeterminism.h"
#include "Game/GothicGameState.h"
#include "Game/GothicPlayerState.h"
#include "Game/GothicGameInstance.h"
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
#include "NavigationSystem.h"

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
    if (!HasAuthority())
    {
        return;
    }

    // The encounter is over — its roster is dead or its reward already paid. A
    // re-entry after that must not walk a list of corpses handing out targets.
    // (IsComplete is the same test that gates the Selah collect.)
    if (bRewarded || IsComplete())
    {
        return;
    }

    AGothicPlayerCharacter* Player = Cast<AGothicPlayerCharacter>(OtherActor);
    if (!Player)
    {
        return;
    }

    // The fight is already running: leave it alone. This is what the old
    // bAggroTriggered latch was protecting — the difference is that this answer
    // goes back to false once the roster disengages, so the volume re-arms.
    if (IsAnyMemberEngaged())
    {
        return;
    }

    int32 AggroedCount = 0;
    for (AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        // Living members only. SetCombatTarget on a corpse is harmless today but
        // would re-populate a Blackboard key on an actor mid-death.
        if (Enemy && Enemy->IsAlive())
        {
            Enemy->SetCombatTarget(Player, TEXT("encounter-roster"));
            ++AggroedCount;
        }
    }

    // Worth a line: a re-entry that aggroes 0 of a living roster means every
    // member refused the target, which in practice means they are all leashing
    // home (IsAcceptingCombatTargets) — not that the trigger failed to fire.
    UE_LOG(LogTemp, Verbose,
        TEXT("Selah[%s]: trigger aggro — %d of %d roster member(s) took the target"),
        *GetName(), AggroedCount, EncounterEnemies.Num());
}

bool AGothicEncounterVolume::IsAnyMemberEngaged() const
{
    for (const AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        if (Enemy && Enemy->IsAlive() && Enemy->GetCombatTarget())
        {
            return true;
        }
    }
    return false;
}

void AGothicEncounterVolume::HandleEnemyDied(AGothicEnemyBase* DeadEnemy)
{
    if (!HasAuthority())
    {
        return;
    }

    RemainingEnemyCount = FMath::Max(0, RemainingEnemyCount - 1);
    LastEnemyToDie = DeadEnemy;

    // Bank the reward at the moment of death — see AccumulatedSelah's note. This
    // is the only point at which the enemy is guaranteed to still exist.
    if (DeadEnemy)
    {
        AccumulatedSelah += DeadEnemy->GetSelahAwardAmount();
        if (!CachedGainEffect && DeadEnemy->GetSelahGainEffect())
        {
            CachedGainEffect = DeadEnemy->GetSelahGainEffect();
        }
        if (!DeadEnemy->GetAccursedName().IsEmpty())
        {
            AccumulatedNames.Add(DeadEnemy->GetAccursedName());
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: %s died — %d remaining, WaveStage=%d"),
        *GetName(), *GetNameSafe(DeadEnemy), RemainingEnemyCount, WaveStage);

    // REINFORCEMENT mode: the wave arrives mid-fight, off the living count, and
    // never touches the Selah. Checked before the RemainingEnemyCount <= 0 branch
    // so a threshold of 0 is impossible here — that value selects interrupt mode.
    if (ReinforceAtLivingCount > 0 && WaveStage == 0 && PendingWaveSpawnPoints.Num() > 0
        && RemainingEnemyCount <= ReinforceAtLivingCount)
    {
        WaveStage = 2; // same stage the interrupt wave uses, so Wave 3 still follows
        UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: reinforcements at %d living"),
            *GetName(), RemainingEnemyCount);
        SpawnWaveFromPoints(PendingWaveSpawnPoints);
        OnEncounterMemberDied.Broadcast(DeadEnemy);
        return;
    }

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
    // Read the running totals banked in HandleEnemyDied rather than re-walking
    // EncounterEnemies. Walking the roster here counted only the members whose
    // corpses had not yet expired, so the payout shrank the longer an encounter
    // ran; CachedGainEffect is likewise banked on the first death that carries one.
    CachedTotalSelah = AccumulatedSelah;

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
    if (GS)
    {
        // Names are set BEFORE the prompt corpse so they're available when
        // OnEncounterPromptActivated fires.
        const TArray<FText>& Names = AccumulatedNames;
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

AActor* AGothicEncounterVolume::FindNearestPlayerPawn() const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    // Nearest rather than first: in co-op the wave should come for whoever is
    // actually standing in the encounter, not for player 1 wherever they are.
    AActor* Best = nullptr;
    float BestDistSq = TNumericLimits<float>::Max();
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        APawn* Pawn = PC ? PC->GetPawn() : nullptr;
        if (!Pawn)
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation());
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            Best = Pawn;
        }
    }
    return Best;
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

    const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

    for (AGothicEnemySpawnPoint* Point : Points)
    {
        if (!Point || !Point->EnemyClass) continue;

        const FVector Origin = Point->GetActorLocation();
        const int32 Count = FMath::Max(1, Point->SpawnCount);
        const float Scatter = FMath::Max(0.f, Point->ScatterRadius);

        for (int32 i = 0; i < Count; ++i)
        {
            // The first enemy takes the point itself; the rest scatter around it.
            FVector SpawnLocation = Origin;
            if (i > 0 && Scatter > 0.f)
            {
                // Uniform point in the scatter disc, drawn through FGothicDeterminism
                // so a seeded measurement run reproduces wave placement (project
                // invariant: no raw FMath::Rand*). r = R*sqrt(u) keeps the sample
                // uniform-over-area, matching FMath::RandPointInCircle's distribution.
                const float Angle = FGothicDeterminism::FRandRange(0.f, 2.f * PI);
                const float Dist = Scatter * FMath::Sqrt(FGothicDeterminism::FRandRange(0.f, 1.f));
                SpawnLocation = Origin + FVector(Dist * FMath::Cos(Angle), Dist * FMath::Sin(Angle), 0.f);
            }

            // Project back to walkable ground. Without this, a scatter disc
            // that overhangs the Encounter 2 balcony or a plaza wall drops
            // enemies into geometry, where they either fall out of the world
            // or stand unreachable and the encounter can never complete.
            //
            // Index 0 is projected too, and used to not be. A spawn point
            // authored slightly off the floor it belongs to — above a mezzanine
            // it should be under, or floating over a stairwell — placed its
            // first enemy verbatim, on the wrong storey, fighting a player it
            // could never reach. The projection is the only thing that decides
            // which floor a wave lands on, so it must run for every enemy.
            if (NavSys)
            {
                // Extent must not collapse when Scatter is 0 (the index-0 /
                // no-scatter case): a zero-width query box finds nothing and
                // every spawn would silently fall back to the raw Origin.
                const float QueryExtentXY = FMath::Max(Scatter, 100.f);

                FNavLocation Projected;
                if (NavSys->ProjectPointToNavigation(
                        SpawnLocation, Projected, FVector(QueryExtentXY, QueryExtentXY, 300.f)))
                {
                    SpawnLocation = Projected.Location;
                }
                // One WIDER retry before giving up: a point that just missed the nav
                // mesh (a spawn point authored a little off its floor, or a scatter
                // sample that overhung an edge) usually lands with a fatter box.
                // Extent xy x3, z 500.
                else if (NavSys->ProjectPointToNavigation(
                        SpawnLocation, Projected, FVector(QueryExtentXY * 3.f, QueryExtentXY * 3.f, 500.f)))
                {
                    SpawnLocation = Projected.Location;
                }
                else
                {
                    // No walkable ground within reach of this point — SKIP it rather
                    // than fall back to the raw Origin (the old behaviour), which may
                    // be inside geometry. An embedded enemy is unreachable and can
                    // never die, so RemainingEnemyCount never falls to zero and the
                    // Selah prompt / BleedGate never open. A wave that spawns fewer,
                    // reachable enemies still completes; a wave with one embedded
                    // enemy never does. RemainingEnemyCount counts only the members
                    // AddWaveToEncounter actually folds in, so a skip is completion-safe.
                    UE_LOG(LogVigilCombat, Warning,
                        TEXT("VigilTimeline|t=%.3f|%s|WaveSpawn|SKIP_NONAV|point=%s|origin=%s|loc=%s|class=%s"),
                        World->GetTimeSeconds(), *GetName(), *GetNameSafe(Point),
                        *Origin.ToCompactString(), *SpawnLocation.ToCompactString(),
                        *GetNameSafe(Point->EnemyClass));
                    continue;
                }
            }

            FActorSpawnParameters SpawnParams;
            // Never embed: if collision can't be resolved, DON'T spawn (returns null)
            // and treat it exactly like a nav skip below. The old
            // AdjustIfPossibleButAlwaysSpawn buried the pawn in geometry when
            // adjustment failed, which hung the encounter the same way.
            SpawnParams.SpawnCollisionHandlingOverride =
                ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

            AGothicEnemyBase* NewEnemy = World->SpawnActor<AGothicEnemyBase>(
                Point->EnemyClass, SpawnLocation, Point->GetActorRotation(), SpawnParams);

            if (!NewEnemy)
            {
                // Colliding spawn refused. Same completion-safe reasoning as the nav
                // skip: a smaller reachable wave beats an embedded blocker that keeps
                // RemainingEnemyCount pinned above zero forever.
                UE_LOG(LogVigilCombat, Warning,
                    TEXT("VigilTimeline|t=%.3f|%s|WaveSpawn|SKIP_COLLISION|point=%s|loc=%s|class=%s"),
                    World->GetTimeSeconds(), *GetName(), *GetNameSafe(Point),
                    *SpawnLocation.ToCompactString(), *GetNameSafe(Point->EnemyClass));
                continue;
            }

            {
                // Pack stamp AFTER spawn, through the setter — BeginPlay has
                // already run inside SpawnActor and saw NAME_None; SetPackID
                // is the convergence point for both assignment paths.
                if (!Point->PackID.IsNone())
                {
                    NewEnemy->SetPackID(Point->PackID);
                }
                if (Point->bSuppressLootDrop)
                {
                    NewEnemy->SetSuppressLootDrop(true);
                }
                Spawned.Add(NewEnemy);
            }
        }
    }

    AddWaveToEncounter(Spawned); // registers deaths, bumps the count, retracts any prompt

    // Point the new arrivals at the player. Without this a wave spawns and simply
    // stands there: measured on the plaza's 16-strong reinforcement wave, 12 of 16
    // never moved and never acquired a target, because nothing sets one and their
    // spawn points sit outside perception range of wherever the player is fighting.
    // Only the three that happened to land within ~500uu engaged at all.
    //
    // The placed roster gets its target from HandleTriggerBeginOverlap, which fires
    // once per encounter and is opt-in — so it can never cover enemies that did not
    // exist when the player crossed the trigger. Reinforcements are, by definition,
    // arriving because the player is already here.
    if (AActor* Target = FindNearestPlayerPawn())
    {
        for (AGothicEnemyBase* Enemy : Spawned)
        {
            if (Enemy)
            {
                Enemy->SetCombatTarget(Target, TEXT("wave-handoff"));
            }
        }
    }

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
    // ReinforceAtLivingCount is what selects the mode, so it also has to veto the
    // fake-out here. Testing WaveStage alone would still fake out a reinforcement
    // encounter whose threshold was mis-set and whose wave therefore never fired.
    const bool bInterruptCollect = (WaveStage == 0 && ReinforceAtLivingCount <= 0
        && PendingWaveSpawnPoints.Num() > 0);

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

void AGothicEncounterVolume::NotifyBreakout()
{
    // Authority-only like the rest of this actor's state. The gate that listens
    // is the replicated half of the pair, so clients learn about the opening
    // through bOpen rather than through this delegate.
    if (!HasAuthority() || bBrokenOut)
    {
        return;
    }

    bBrokenOut = true;
    UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: BREAK-OUT announced"), *GetName());
    OnEncounterBreakout.Broadcast(this);
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

    // Mark and announce BEFORE the award loop. Anything gating forward progress
    // on this encounter (AGothicBleedGate) should open on the collection landing,
    // not after the Selah moment and name reveal have finished playing — the
    // player is standing still through that beat and should not also be walled in.
    bRewarded = true;
    OnEncounterRewarded.Broadcast(this);

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

    // The volume's own location is a trigger box CENTRE, not a floor — EV4's sits
    // 340 uu above the ground and the boss volume's 740. Let the game state
    // project it down, or "respawn at the checkpoint" means falling out of the sky.
    GS->SetCheckpointFromLocation(GetActorLocation(), this);

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

    // Park the player's holdings on the GameInstance BEFORE the world goes away.
    // OpenLevel destroys every actor including the PlayerState the inventory
    // lives on, so this is the last frame the gear exists; without it the run's
    // entire haul is lost on the trip home.
    //
    // Player 0 rather than a per-player loop, matching the single snapshot on the
    // GameInstance — OpenLevel is a local travel and this path is single-player /
    // listen-server-host only for now. See UGothicGameInstance's class comment.
    if (UGothicGameInstance* GothicGI = GetGameInstance<UGothicGameInstance>())
    {
        GothicGI->CaptureTravelSnapshot(UGameplayStatics::GetPlayerState(this, 0));
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
