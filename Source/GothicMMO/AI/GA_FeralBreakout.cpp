// GA_FeralBreakout.cpp

#include "AI/GA_FeralBreakout.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AI/GothicEnemyBase.h"
#include "AI/GothicEnemySpawnPoint.h"
#include "AI/GothicEncounterVolume.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
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
    // A real ballistic launch, not a teleport. She is airborne for the descent,
    // which is what sells the break-out; a hard SetActorLocation read as a pop.
    //
    // RallyCenter is the LANDING point, deliberately not her live location. The
    // rally overlap and the reinforcement spawns have to resolve around the
    // arena floor, and unlike the old teleport she has not arrived yet when
    // they run -- she is still mid-arc. Measuring from her actual position here
    // would centre the whole rally on the perch she just left.
    FVector RallyCenter = Avatar->GetActorLocation();

    if (!LeapTargetTag.IsNone())
    {
        TArray<AActor*> LeapTargets;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), LeapTargetTag, LeapTargets);
        if (LeapTargets.Num() > 0 && LeapTargets[0])
        {
            const FVector TargetLoc = LeapTargets[0]->GetActorLocation();
            RallyCenter = TargetLoc;

            ACharacter* MeChar = Cast<ACharacter>(Avatar);
            FVector LaunchVelocity = FVector::ZeroVector;
            const bool bArcSolved = MeChar && UGameplayStatics::SuggestProjectileVelocity_CustomArc(
                Avatar, LaunchVelocity, Avatar->GetActorLocation(), TargetLoc,
                0.f /*use world gravity*/, LeapArcParam);

            if (bArcSolved)
            {
                // Two things have to happen BEFORE the launch or it dies on the
                // spot, both observed in PIE:
                //
                // 1. Path following keeps steering her. The behaviour tree's
                //    Move To is still active and immediately fights the launch
                //    velocity.
                // 2. She stays in Walking mode. The arc to a target BELOW her
                //    has little or no upward component, so LaunchCharacter never
                //    puts her airborne and ground friction bleeds the velocity
                //    off within a couple of hundred uu.
                //
                // Measured before this: she travelled ~200uu of a ~650uu leap
                // and her Z never moved off 642 -- a shove, not a leap.
                if (AAIController* AIC = Cast<AAIController>(MeChar->GetController()))
                {
                    AIC->StopMovement();
                }
                if (UCharacterMovementComponent* Move = MeChar->GetCharacterMovement())
                {
                    Move->SetMovementMode(MOVE_Falling);
                }

                // Override both axes: whatever she was doing (a MoveTo, a stagger
                // slide) must not blend with the launch or she lands short.
                MeChar->LaunchCharacter(LaunchVelocity, true, true);
            }
            else
            {
                // No arc solves for this pair of points -- fall back to the old
                // teleport rather than leaving her stranded on the perch with
                // the encounter already counting her reinforcements.
                UE_LOG(LogTemp, Warning,
                    TEXT("FeralBreakout[%s]: no launch arc from %s to %s (ArcParam %.2f) -- "
                         "falling back to teleport."),
                    *GetNameSafe(Avatar), *Avatar->GetActorLocation().ToCompactString(),
                    *TargetLoc.ToCompactString(), LeapArcParam);

                Avatar->SetActorLocation(
                    TargetLoc, false, nullptr, ETeleportType::TeleportPhysics);
            }
        }
    }

    AGothicEnemyBase* Me = Cast<AGothicEnemyBase>(Avatar);
    AActor* AggroTarget = Me ? Me->GetCombatTarget() : nullptr;

    // ── Open the way she just made ───────────────────────────────────────
    // Announced here, at the launch and not at the landing: the exit is the hole
    // she leaves on the way through, so it should be open while she is still in
    // the air. Any AGothicBleedGate listing this encounter under
    // BreakoutEncounters dissolves now, with the fight still running — which is
    // the point, since the arena below is where the player follows her.
    if (Me && Me->OwningEncounter)
    {
        Me->OwningEncounter->NotifyBreakout();
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("FeralBreakout[%s]: no OwningEncounter — any break-out gate stays shut. "
                 "Is she inside her encounter volume's roster?"),
            *GetNameSafe(Avatar));
    }

    // ── Rally already-placed ferals around her ───────────────────────────
    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(Avatar);

    TArray<AActor*> Nearby;
    UKismetSystemLibrary::SphereOverlapActors(
        Avatar,
        RallyCenter,
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
