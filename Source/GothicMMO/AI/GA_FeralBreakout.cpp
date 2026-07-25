// GA_FeralBreakout.cpp

#include "AI/GA_FeralBreakout.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AI/GothicEnemyBase.h"
#include "AI/GothicEnemySpawnPoint.h"
#include "AI/GothicEncounterVolume.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"

UGA_FeralBreakout::UGA_FeralBreakout()
{
    AbilitySlot = EGothicAbilitySlot::Ability3;
}

void UGA_FeralBreakout::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    if (PlayOptionalMontage())
    {
        // Rally + spawn fire at the montage hit window.
        return;
    }

    // No montage — instant fallback.
    if (GetOwningActorFromActorInfo()->HasAuthority())
    {
        PerformBreakout();
    }

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_FeralBreakout::OnMontageHitWindow(FGameplayEventData Payload)
{
    if (GetOwningActorFromActorInfo()->HasAuthority())
    {
        PerformBreakout();
    }
}

void UGA_FeralBreakout::PerformBreakout()
{
    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar)
    {
        return;
    }

    // ── Leap: break out of the upstairs perch down to the arena floor ────
    // Teleport for now (montage/animated leap is the deferred moveset pass).
    // Done first so the rally overlap and the reinforcement spawns all resolve
    // around her NEW arena position, not her old perch.
    if (!LeapTargetTag.IsNone())
    {
        TArray<AActor*> LeapTargets;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), LeapTargetTag, LeapTargets);
        if (LeapTargets.Num() > 0 && LeapTargets[0])
        {
            Avatar->SetActorLocation(
                LeapTargets[0]->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
        }
    }

    AGothicEnemyBase* Me = Cast<AGothicEnemyBase>(Avatar);
    AActor* AggroTarget = Me ? Me->GetCombatTarget() : nullptr;

    // ── Rally already-placed ferals around her ───────────────────────────
    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(Avatar);

    TArray<AActor*> Nearby;
    UKismetSystemLibrary::SphereOverlapActors(
        Avatar,
        Avatar->GetActorLocation(),
        RallyRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
        AGothicEnemyBase::StaticClass(),
        IgnoreActors,
        Nearby);

    for (AActor* NearbyActor : Nearby)
    {
        RallyEnemy(Cast<AGothicEnemyBase>(NearbyActor), AggroTarget);
    }

    // ── Call in the reinforcement wave ───────────────────────────────────
    TArray<AGothicEnemyBase*> SpawnedWave;
    SpawnRallyWave(AggroTarget, SpawnedWave);

    // ── Fold reinforcements into the arena encounter ─────────────────────
    // Her and the pre-placed ferals are already the encounter's roster; only
    // the freshly spawned reinforcements need adding, or they'd never be
    // counted toward completion (and the arena would finish one wave early).
    if (Me && Me->OwningEncounter && SpawnedWave.Num() > 0)
    {
        Me->OwningEncounter->AddWaveToEncounter(SpawnedWave);
    }
}

void UGA_FeralBreakout::RallyEnemy(AGothicEnemyBase* Enemy, AActor* AggroTarget)
{
    if (!Enemy || !Enemy->IsAlive())
    {
        return;
    }

    if (RallyBuffEffect)
    {
        if (UAbilitySystemComponent* EnemyASC =
                UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Enemy))
        {
            UGothicAbilitySystemComponent::ApplyEffectToASC(
                EnemyASC, RallyBuffEffect, GetAvatarActorFromActorInfo());
        }
    }

    if (AggroTarget)
    {
        Enemy->SetCombatTarget(AggroTarget);
    }
}

void UGA_FeralBreakout::SpawnRallyWave(AActor* AggroTarget, TArray<AGothicEnemyBase*>& OutWave)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    TArray<AActor*> AllSpawnPoints;
    UGameplayStatics::GetAllActorsOfClassWithTag(
        World,
        AGothicEnemySpawnPoint::StaticClass(),
        RallySpawnTag,
        AllSpawnPoints);

    if (AllSpawnPoints.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("FeralBreakout: no spawn points tagged '%s' found in level — no reinforcements"),
            *RallySpawnTag.ToString());
        return;
    }

    // Fisher-Yates shuffle so the same points don't always fire first.
    TArray<int32> Indices;
    for (int32 i = 0; i < AllSpawnPoints.Num(); ++i)
    {
        Indices.Add(i);
    }
    for (int32 i = Indices.Num() - 1; i > 0; --i)
    {
        const int32 j = FMath::RandRange(0, i);
        Indices.Swap(i, j);
    }

    int32 Spawned = 0;
    for (int32 Idx : Indices)
    {
        if (Spawned >= MaxWaveThralls)
        {
            break;
        }

        AGothicEnemySpawnPoint* SpawnPoint = Cast<AGothicEnemySpawnPoint>(AllSpawnPoints[Idx]);
        if (!SpawnPoint || !SpawnPoint->EnemyClass)
        {
            continue;
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        AGothicEnemyBase* NewEnemy = World->SpawnActor<AGothicEnemyBase>(
            SpawnPoint->EnemyClass,
            SpawnPoint->GetActorLocation(),
            SpawnPoint->GetActorRotation(),
            SpawnParams);

        if (!NewEnemy)
        {
            continue;
        }

        // Pack stamp AFTER spawn — BeginPlay ran inside SpawnActor and saw
        // NAME_None; the setter is the convergence point (same as the
        // encounter volume's pending-wave path).
        if (!SpawnPoint->PackID.IsNone())
        {
            NewEnemy->SetPackID(SpawnPoint->PackID);
        }

        RallyEnemy(NewEnemy, AggroTarget);
        OutWave.Add(NewEnemy);
        ++Spawned;
    }
}
