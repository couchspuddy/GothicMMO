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
#include "AbilitySystem/GothicGameplayTags.h"
#include "AI/GothicEnemySpawnPoint.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
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

    // Cross-volume breakout gate. With one set, this volume's reinforcement wave
    // waits for the referenced volume to break out (the Feral Retained leaping to
    // the next area). Self-reference is meaningless — a volume cannot gate on its
    // own breakout — so ignore it. If the gate has somehow already broken out by
    // the time we register (out-of-order BeginPlay, or a re-registered volume),
    // treat the gate as open from the start, mirroring the IsRewarded/IsBrokenOut
    // late-registration pattern the gates use.
    if (WaveBreakoutGateVolume && WaveBreakoutGateVolume != this)
    {
        if (WaveBreakoutGateVolume->IsBrokenOut())
        {
            bWaveGateOpen = true;
        }
        else
        {
            WaveBreakoutGateVolume->OnEncounterBreakout.AddDynamic(
                this, &AGothicEncounterVolume::HandleGateVolumeBreakout);
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|t=%.3f|%s|WaveGate|BOUND|gate=%s"),
                GetWorld()->GetTimeSeconds(), *GetName(),
                *GetNameSafe(WaveBreakoutGateVolume));
        }
    }
    else
    {
        // No gate (or a self-reference): nothing to wait on, reinforcements fire
        // on attrition exactly as before.
        bWaveGateOpen = true;
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
        // Cross-volume gate: attrition is met, but a gate volume that has not yet
        // broken out holds the wave. Remember the attrition was satisfied and bail
        // WITHOUT advancing WaveStage or falling through to the roster-cleared
        // branch below — the encounter must not raise its Selah prompt while its
        // reinforcement wave is still owed, or a player could collect and finalize
        // before the held wave ever arrives. HandleGateVolumeBreakout springs it.
        if (!bWaveGateOpen)
        {
            bWaveBreakoutPending = true;
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|t=%.3f|%s|WaveGate|HELD|living=%d|gate=%s"),
                GetWorld()->GetTimeSeconds(), *GetName(), RemainingEnemyCount,
                *GetNameSafe(WaveBreakoutGateVolume));
            OnEncounterMemberDied.Broadcast(DeadEnemy);
            return;
        }

        WaveStage = 2; // same stage the interrupt wave uses, so Wave 3 still follows
        UE_LOG(LogTemp, Verbose, TEXT("Selah[%s]: reinforcements at %d living"),
            *GetName(), RemainingEnemyCount);
        SpawnWaveFromPoints(PendingWaveSpawnPoints);
        OnEncounterMemberDied.Broadcast(DeadEnemy);
        return;
    }

    // Gate the completion decision on PendingSpawnCount: while a staggered wave
    // still has spawns queued, a roster that momentarily reads 0 (deaths racing
    // ahead of the trickle of arrivals) must NOT fire the prompt / next wave. The
    // last pending spawn to resolve re-runs this evaluation from ProcessWaveSpawnQueue.
    if (RemainingEnemyCount <= 0 && PendingSpawnCount <= 0)
    {
        EvaluateRosterCleared();
    }

    OnEncounterMemberDied.Broadcast(DeadEnemy);
}

void AGothicEncounterVolume::EvaluateRosterCleared()
{
    // Precondition the callers already satisfy, re-asserted so this is safe to call
    // from the stagger drain as well as from a death.
    if (RemainingEnemyCount > 0 || PendingSpawnCount > 0)
    {
        return;
    }

    // Interrupt wave (Wave 2) just fell -> the "one more after" spawns
    // automatically without a prompt, if configured.
    if (WaveStage == 2 && Wave3SpawnPoints.Num() > 0)
    {
        WaveStage = 3;
        SpawnWaveFromPoints(Wave3SpawnPoints);
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

AGothicEnemyBase* AGothicEncounterVolume::SpawnEnemyFromRequest(
    AGothicEnemySpawnPoint* Point, int32 IndexInPoint,
    int32 RetriesRemaining, int32 MaxRetries)
{
    UWorld* World = GetWorld();
    if (!World || !Point || !Point->EnemyClass)
    {
        return nullptr;
    }

    const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

    const FVector Origin = Point->GetActorLocation();
    const float Scatter = FMath::Max(0.f, Point->ScatterRadius);

    // The first enemy takes the point itself; the rest scatter around it.
    FVector SpawnLocation = Origin;
    if (IndexInPoint > 0 && Scatter > 0.f)
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

        // Projection lands SpawnLocation on the navmesh surface (floor + a small
        // nav offset), but SpawnActor places the capsule CENTRE at that point —
        // burying the enemy's lower half in the floor, where
        // AdjustIfPossibleButDontSpawnIfColliding refuses the spawn and burns a
        // retry. Lift the projected point by the enemy's capsule half-height so
        // the capsule bottom rests on the surface. Read it off the class CDO;
        // fall back to a sane player-height constant if the CDO/capsule can't be
        // resolved — never leave the spawn sitting at raw nav level.
        float CapsuleLift = 90.f;
        if (const AGothicEnemyBase* EnemyCDO = Point->EnemyClass->GetDefaultObject<AGothicEnemyBase>())
        {
            if (const UCapsuleComponent* Capsule = EnemyCDO->GetCapsuleComponent())
            {
                CapsuleLift = Capsule->GetScaledCapsuleHalfHeight();
            }
        }

        FNavLocation Projected;
        if (NavSys->ProjectPointToNavigation(
                SpawnLocation, Projected, FVector(QueryExtentXY, QueryExtentXY, 300.f)))
        {
            SpawnLocation = Projected.Location;
            SpawnLocation.Z += CapsuleLift;
        }
        // One WIDER retry before giving up: a point that just missed the nav
        // mesh (a spawn point authored a little off its floor, or a scatter
        // sample that overhung an edge) usually lands with a fatter box.
        // Extent xy x3, z 500.
        else if (NavSys->ProjectPointToNavigation(
                SpawnLocation, Projected, FVector(QueryExtentXY * 3.f, QueryExtentXY * 3.f, 500.f)))
        {
            SpawnLocation = Projected.Location;
            SpawnLocation.Z += CapsuleLift;
        }
        else if (RetriesRemaining > 0)
        {
            // Rejected, but a retry is still queued — the caller re-rolls this
            // request's scatter on a later tick. NOT a lost spawn, so log at
            // Verbose rather than raise a SKIP warning the retry would contradict.
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|t=%.3f|%s|WaveSpawn|RETRY|attempt=%d/%d|point=%s|loc=%s|class=%s"),
                World->GetTimeSeconds(), *GetName(), MaxRetries - RetriesRemaining + 1, MaxRetries,
                *GetNameSafe(Point), *SpawnLocation.ToCompactString(),
                *GetNameSafe(Point->EnemyClass));
            return nullptr;
        }
        else
        {
            // No walkable ground within reach of this point, retries exhausted —
            // SKIP it rather than fall back to the raw Origin (the old behaviour),
            // which may be inside geometry. An embedded enemy is unreachable and
            // can never die, so RemainingEnemyCount never falls to zero and the
            // Selah prompt / BleedGate never open. A wave that spawns fewer,
            // reachable enemies still completes; a wave with one embedded
            // enemy never does. The pending-intent count is decremented on a
            // skip by the caller, so a skip is completion-safe.
            UE_LOG(LogVigilCombat, Warning,
                TEXT("VigilTimeline|t=%.3f|%s|WaveSpawn|SKIP_NONAV|point=%s|origin=%s|loc=%s|class=%s"),
                World->GetTimeSeconds(), *GetName(), *GetNameSafe(Point),
                *Origin.ToCompactString(), *SpawnLocation.ToCompactString(),
                *GetNameSafe(Point->EnemyClass));
            return nullptr;
        }
    }

    FActorSpawnParameters SpawnParams;
    // Never embed: if collision can't be resolved, DON'T spawn (returns null)
    // and treat it exactly like a nav skip. The old
    // AdjustIfPossibleButAlwaysSpawn buried the pawn in geometry when
    // adjustment failed, which hung the encounter the same way.
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

    AGothicEnemyBase* NewEnemy = World->SpawnActor<AGothicEnemyBase>(
        Point->EnemyClass, SpawnLocation, Point->GetActorRotation(), SpawnParams);

    if (!NewEnemy)
    {
        if (RetriesRemaining > 0)
        {
            // Colliding spawn refused, but a retry is queued — the caller re-rolls
            // this request's scatter on a later tick, by which point the frame's
            // other spawns have depenetrated and settled and the point is usually
            // clear. NOT a lost spawn: Verbose, not a SKIP warning.
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|t=%.3f|%s|WaveSpawn|RETRY|attempt=%d/%d|point=%s|loc=%s|class=%s"),
                World->GetTimeSeconds(), *GetName(), MaxRetries - RetriesRemaining + 1, MaxRetries,
                *GetNameSafe(Point), *SpawnLocation.ToCompactString(),
                *GetNameSafe(Point->EnemyClass));
            return nullptr;
        }

        // Colliding spawn refused, retries exhausted. Same completion-safe
        // reasoning as the nav skip: a smaller reachable wave beats an embedded
        // blocker that keeps RemainingEnemyCount pinned above zero forever.
        UE_LOG(LogVigilCombat, Warning,
            TEXT("VigilTimeline|t=%.3f|%s|WaveSpawn|SKIP_COLLISION|point=%s|loc=%s|class=%s"),
            World->GetTimeSeconds(), *GetName(), *GetNameSafe(Point),
            *SpawnLocation.ToCompactString(), *GetNameSafe(Point->EnemyClass));
        return nullptr;
    }

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

    return NewEnemy;
}

void AGothicEncounterVolume::RegisterWaveEnemy(AGothicEnemyBase* Enemy)
{
    if (!HasAuthority() || !Enemy)
    {
        return;
    }

    // The per-enemy equivalent of AddWaveToEncounter's loop body (see that method):
    // fold into the roster, own it, bind its death, count it.
    EncounterEnemies.Add(Enemy);
    Enemy->OwningEncounter = this;
    Enemy->OnEnemyDied.AddDynamic(this, &AGothicEncounterVolume::HandleEnemyDied);
    ++RemainingEnemyCount;

    // Point the new arrival at the player. Without this a wave spawns and simply
    // stands there: measured on the plaza's 16-strong reinforcement wave, 12 of 16
    // never moved and never acquired a target. The placed roster gets its target
    // from HandleTriggerBeginOverlap; reinforcements arrive because the player is
    // already here.
    if (AActor* Target = FindNearestPlayerPawn())
    {
        Enemy->SetCombatTarget(Target, TEXT("wave-handoff"));
    }

    // A wave folding in retracts any pending meditation prompt — same as
    // AddWaveToEncounter. Under the stagger the prompt cannot actually be up
    // during a wave (PendingSpawnCount gates it), but keep parity so a corner
    // case can never strand a prompt over a spawning wave.
    if (AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr)
    {
        if (GS->IsPromptPending(this))
        {
            GS->SetSelahNames(TArray<FText>());
            GS->ClearEncounterPrompt(this);
        }
    }
}

void AGothicEncounterVolume::ProcessWaveSpawnQueue()
{
    if (!HasAuthority())
    {
        return;
    }
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (WaveSpawnQueue.Num() > 0)
    {
        const FGothicWaveSpawnRequest Req = WaveSpawnQueue[0];
        WaveSpawnQueue.RemoveAt(0);

        // Hand SpawnEnemyFromRequest this request's retry budget so its rejection
        // log matches the requeue decision below: while retries remain a rejection
        // logs Verbose WaveSpawn|RETRY, and only the final (RetriesRemaining==0)
        // rejection raises the SKIP warning. MaxRetries reconstructs attempt=N/M.
        AGothicEnemyBase* NewEnemy = SpawnEnemyFromRequest(
            Req.Point, Req.IndexInPoint, Req.RetriesRemaining, FMath::Max(0, WaveSpawnMaxRetries));
        if (NewEnemy)
        {
            // Resolve the intent BEFORE folding in, so RemainingEnemyCount and
            // PendingSpawnCount move as a matched pair and never both read the
            // enemy as absent.
            --PendingSpawnCount;
            RegisterWaveEnemy(NewEnemy);

            UE_LOG(LogTemp, Verbose,
                TEXT("Selah[%s]: staggered spawn folded in — roster=%d, RemainingEnemyCount=%d, PendingSpawnCount=%d"),
                *GetName(), EncounterEnemies.Num(), RemainingEnemyCount, PendingSpawnCount);
        }
        else if (Req.RetriesRemaining > 0)
        {
            // Re-roll on a later tick (fresh scatter). Pending intent unchanged —
            // this request has NOT resolved yet, so completion still waits on it.
            FGothicWaveSpawnRequest Retry = Req;
            --Retry.RetriesRemaining;
            WaveSpawnQueue.Add(Retry);
        }
        else
        {
            // Genuinely fouled point, retries exhausted. Resolve the intent so a
            // skip can NEVER wedge the encounter open via the pending counter —
            // the exact bug class PR #77 fixed must not return this way.
            --PendingSpawnCount;
        }
    }

    if (WaveSpawnQueue.Num() > 0)
    {
        // More to come — reschedule the trickle.
        World->GetTimerManager().SetTimer(
            WaveStaggerHandle, this, &AGothicEncounterVolume::ProcessWaveSpawnQueue,
            FMath::Max(0.01f, WaveSpawnStaggerInterval), false);
    }
    else
    {
        // Wave fully resolved (PendingSpawnCount is now 0). Invalidate BEFORE any
        // chain re-entry so a Wave 3 kicked off below arms a fresh timer cleanly.
        WaveStaggerHandle.Invalidate();

        // If the roster is already empty here, the death handler was gated by
        // PendingSpawnCount and never advanced the chain (every enemy of this
        // wave skipped, or all died faster than they spawned). Advance it now so
        // a skip can't leave the encounter hung open.
        if (RemainingEnemyCount <= 0)
        {
            EvaluateRosterCleared();
        }
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

    // ── Staggered path (WaveSpawnStaggerInterval > 0) ────────────────────────
    // Queue one request per intended enemy and register the wave's FULL intended
    // total in PendingSpawnCount up-front, THEN trickle the spawns out one per
    // interval. Because completion is gated on PendingSpawnCount, the counter can
    // never transiently hit 0 mid-wave and fire the prompt / next wave early.
    if (WaveSpawnStaggerInterval > 0.f)
    {
        int32 QueuedThisCall = 0;
        for (AGothicEnemySpawnPoint* Point : Points)
        {
            if (!Point || !Point->EnemyClass) continue;

            const int32 Count = FMath::Max(1, Point->SpawnCount);
            for (int32 i = 0; i < Count; ++i)
            {
                // Sequential per point (point A #0..#N, then point B #0..#N). Chosen
                // over interleaving points because it is the simpler expansion and the
                // temporal stagger — not spawn order — is what separates the capsules.
                FGothicWaveSpawnRequest Req;
                Req.Point = Point;
                Req.IndexInPoint = i;
                Req.RetriesRemaining = FMath::Max(0, WaveSpawnMaxRetries);
                WaveSpawnQueue.Add(Req);
                ++QueuedThisCall;
            }
        }

        PendingSpawnCount += QueuedThisCall;

        UE_LOG(LogTemp, Verbose,
            TEXT("Selah[%s]: wave queued for stagger — +%d intended @ %.2fs interval, PendingSpawnCount=%d, WaveStage=%d"),
            *GetName(), QueuedThisCall, WaveSpawnStaggerInterval, PendingSpawnCount, WaveStage);

        // Kick the trickle on a timer (first spawn one interval out, not this
        // frame): keeps pacing uniform and, critically, keeps EvaluateRosterCleared
        // off the current call stack (this can be reached from within
        // HandleEnemyDied / ProcessWaveSpawnQueue). If a stagger is already running
        // the appended entries are picked up by the in-flight loop.
        if (QueuedThisCall > 0 && !WaveStaggerHandle.IsValid())
        {
            World->GetTimerManager().SetTimer(
                WaveStaggerHandle, this, &AGothicEncounterVolume::ProcessWaveSpawnQueue,
                FMath::Max(0.01f, WaveSpawnStaggerInterval), false);
        }

        // Spawns resolve later over the timer, so the array cannot be returned
        // populated. Every caller uses only Num() to log — reinforcement/Wave 3
        // ignore it, SpawnInterruptWave logs it (now reads 0; noted in the PR).
        return Spawned;
    }

    // ── Legacy synchronous path (WaveSpawnStaggerInterval == 0) ──────────────
    // Byte-for-byte the original behaviour: spawn every enemy this frame, fold the
    // whole batch in at once (no pending window), target them.
    for (AGothicEnemySpawnPoint* Point : Points)
    {
        if (!Point || !Point->EnemyClass) continue;

        const int32 Count = FMath::Max(1, Point->SpawnCount);
        for (int32 i = 0; i < Count; ++i)
        {
            if (AGothicEnemyBase* NewEnemy = SpawnEnemyFromRequest(Point, i))
            {
                Spawned.Add(NewEnemy);
            }
        }
    }

    AddWaveToEncounter(Spawned); // registers deaths, bumps the count, retracts any prompt

    // Point the new arrivals at the player (see RegisterWaveEnemy's note for why
    // an untargeted wave just stands there).
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

void AGothicEncounterVolume::HandleGateVolumeBreakout(AGothicEncounterVolume* Gate)
{
    // Authority-only and idempotent — the gate's breakout is server-side, and a
    // re-fired delegate (a re-activated GA_FeralBreakout) must not spring the wave
    // twice.
    if (!HasAuthority() || bWaveGateOpen)
    {
        return;
    }

    bWaveGateOpen = true;

    UE_LOG(LogVigilCombat, Log,
        TEXT("VigilTimeline|t=%.3f|%s|WaveGate|OPEN|gate=%s|pending=%d"),
        GetWorld()->GetTimeSeconds(), *GetName(), *GetNameSafe(Gate),
        bWaveBreakoutPending ? 1 : 0);

    // Attrition was already satisfied while we waited (the gated roster is long
    // dead by the time she leaps): spring the held reinforcement wave now. If it
    // was NOT yet satisfied the gate is simply open, and the ordinary
    // attrition-driven branch in HandleEnemyDied fires the wave when the count drops.
    if (bWaveBreakoutPending && WaveStage == 0 && PendingWaveSpawnPoints.Num() > 0)
    {
        bWaveBreakoutPending = false;
        WaveStage = 2; // same stage the interrupt wave uses, so Wave 3 still follows
        SpawnWaveFromPoints(PendingWaveSpawnPoints);
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
                    GothicTags::Data_Selah, CachedTotalSelah);
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
