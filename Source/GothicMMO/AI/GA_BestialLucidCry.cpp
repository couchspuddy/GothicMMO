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

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    if (PlayOptionalMontage())
    {
        // Stun + spawn fires at the montage hit window
        return;
    }

    // No montage — instant fallback
    if (GetOwningActorFromActorInfo()->HasAuthority())
    {
        PerformCry();
    }

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
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


    // ── Spawn Thralls ────────────────────────────────────────────────

    SpawnCryThralls();
}

void UGA_BestialLucidCry::SpawnCryThralls()
{
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