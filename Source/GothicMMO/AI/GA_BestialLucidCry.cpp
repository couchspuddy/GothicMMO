// GA_BestialLucidCry.cpp

#include "AI/GA_BestialLucidCry.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Character/GothicPlayerCharacter.h"
#include "AI/GothicEnemySpawnPoint.h"
#include "AI/GothicEnemyBase.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Sound/SoundBase.h"

UGA_BestialLucidCry::UGA_BestialLucidCry()
{
    AbilitySlot = EGothicAbilitySlot::Ability3;
}

void UGA_BestialLucidCry::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    ActivationTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    if (PlayOptionalMontage())
    {
        // Stun + spawn fires at the montage hit window — the montage is the
        // windup on that path.
        return;
    }

    if (!GetOwningActorFromActorInfo()->HasAuthority())
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    // No montage — cry out first, stun after. Same structure as Roar: the
    // ability stays active across the delay because InstancedPerExecution
    // instances only live as long as the activation, and a timer outliving its
    // own ability object fires into nothing.
    if (AActor* Avatar = GetAvatarActorFromActorInfo())
    {
        if (CryCueSound)
        {
            UGameplayStatics::PlaySoundAtLocation(
                Avatar, CryCueSound, Avatar->GetActorLocation());
        }
    }

    OnStunWindup();

    UWorld* World = GetWorld();
    if (WindupDelay <= 0.f || !World)
    {
        PerformCry();
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    World->GetTimerManager().SetTimer(
        WindupTimerHandle, this,
        &UGA_BestialLucidCry::OnWindupElapsed,
        WindupDelay, false);
}

void UGA_BestialLucidCry::OnWindupElapsed()
{
    PerformCry();

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_BestialLucidCry::EndAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    // Interrupted mid-windup — she never finished the cry, so nothing lands.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(WindupTimerHandle);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BestialLucidCry::OnMontageHitWindow(FGameplayEventData Payload)
{
    if (GetOwningActorFromActorInfo()->HasAuthority())
    {
        PerformCry();
    }
}

void UGA_BestialLucidCry::PerformCry()
{
    AActor* Avatar = GetAvatarActorFromActorInfo();

    // How long the player actually had — same measurement as the Roar's, so
    // the two stuns can be compared from one log. See PerformRoarStun.
    const double TelegraphSeconds =
        GetWorld() ? GetWorld()->GetTimeSeconds() - ActivationTimeSeconds : 0.0;

    // ── AOE Stun (same as Roar) ──────────────────────────────────────

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(Avatar);

    TArray<AActor*> Overlapping;
    UKismetSystemLibrary::SphereOverlapActors(
        Avatar,
        Avatar->GetActorLocation(),
        StunRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
        AGothicPlayerCharacter::StaticClass(),
        IgnoreActors,
        Overlapping);

    int32 PlayersHit = 0;
    for (AActor* PlayerActor : Overlapping)
    {
        UAbilitySystemComponent* PlayerASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PlayerActor);

        if (PlayerASC && StunEffectClass)
        {
            UGothicAbilitySystemComponent::ApplyEffectToASC(PlayerASC, StunEffectClass, Avatar);
            ++PlayersHit;
        }
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("Cry[%s]: stun landed %.2fs after activation (%s path, WindupDelay %.2f) — "
             "%d of %d players in %.0fuu radius"),
        *Avatar->GetName(), TelegraphSeconds,
        MontageToPlay ? TEXT("montage hit-window") : TEXT("windup timer"),
        WindupDelay, PlayersHit, Overlapping.Num(), StunRadius);

    // ── Spawn Thralls ────────────────────────────────────────────────
    // Off by configuration (MaxCryThralls defaults to 0), not by deletion.
    SpawnCryThralls();
}

void UGA_BestialLucidCry::SpawnCryThralls()
{
    // The single gate. At 0 the Cry is a pure stun and this returns before
    // touching spawn points, before the "no spawn points tagged" warning, and
    // before any allocation — nothing about the disabled path costs anything
    // or complains about level setup that intentionally doesn't exist.
    if (MaxCryThralls <= 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World) return;

    // Clean up dead references
    SpawnedCryThralls.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr)
    {
        return !Ptr.IsValid() || !Ptr->IsValidLowLevel();
    });

    // Count living Thralls from previous cries
    int32 LivingThralls = 0;
    for (const TWeakObjectPtr<AActor>& Ptr : SpawnedCryThralls)
    {
        if (AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(Ptr.Get()))
        {
            if (Enemy->IsAlive())
            {
                ++LivingThralls;
            }
        }
    }

    int32 SlotsAvailable = MaxCryThralls - LivingThralls;
    if (SlotsAvailable <= 0)
    {
        return;
    }

    // Find all spawn points tagged for Cry
    TArray<AActor*> AllSpawnPoints;
    UGameplayStatics::GetAllActorsOfClassWithTag(
        World,
        AGothicEnemySpawnPoint::StaticClass(),
        CrySpawnTag,
        AllSpawnPoints);

    if (AllSpawnPoints.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cry: No spawn points with tag '%s' found in level"),
            *CrySpawnTag.ToString());
        return;
    }

    // Shuffle and spawn up to SlotsAvailable
    int32 Spawned = 0;
    TArray<int32> Indices;
    for (int32 i = 0; i < AllSpawnPoints.Num(); ++i) Indices.Add(i);

    // Fisher-Yates shuffle
    for (int32 i = Indices.Num() - 1; i > 0; --i)
    {
        int32 j = FMath::RandRange(0, i);
        Indices.Swap(i, j);
    }

    for (int32 Idx : Indices)
    {
        if (Spawned >= SlotsAvailable) break;

        AGothicEnemySpawnPoint* SpawnPoint = Cast<AGothicEnemySpawnPoint>(AllSpawnPoints[Idx]);
        if (!SpawnPoint || !SpawnPoint->EnemyClass) continue;

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        AGothicEnemyBase* NewEnemy = World->SpawnActor<AGothicEnemyBase>(
            SpawnPoint->EnemyClass,
            SpawnPoint->GetActorLocation(),
            SpawnPoint->GetActorRotation(),
            SpawnParams);

        if (NewEnemy)
        {
            SpawnedCryThralls.Add(NewEnemy);
            ++Spawned;

        }
    }

}