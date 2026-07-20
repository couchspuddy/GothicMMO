// GA_Slicer.cpp

#include "AbilitySystem/GA_Slicer.h"
#include "AI/GothicEnemyBase.h"
#include "AbilitySystem/AGothicSlicerProjectile.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameFramework/Character.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"

UGA_Slicer::UGA_Slicer()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Slicer::ActivateAbility(    
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
    if (!Character || !ProjectileClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_Slicer: Missing character or ProjectileClass — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
    if (!Camera)
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_Slicer: No camera found — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    if (!Character->HasAuthority())
    {
        // Client side of a predicted activation — the server owns the projectile.
        return;
    }

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_Slicer: CommitAbility failed — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    const FVector SpawnLocation = Camera->GetComponentLocation() + (Camera->GetForwardVector() * SpawnDistance);
    const FRotator SpawnRotation = Camera->GetComponentRotation();

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = Character;
    SpawnParams.Instigator = Character;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    


    SpawnedProjectile = GetWorld()->SpawnActor<AGothicSlicerProjectile>(
        ProjectileClass, SpawnLocation, SpawnRotation, SpawnParams);

    if (SpawnedProjectile)
    {
        SpawnedProjectile->InitializeProjectile(Character);
        SpawnedProjectile->OnSlicerHit.AddDynamic(this, &UGA_Slicer::HandleSlicerHit);
        SpawnedProjectile->OnSlicerExpired.AddDynamic(this, &UGA_Slicer::HandleSlicerExpired);

        UE_LOG(LogTemp, Log, TEXT("GA_Slicer: Projectile spawned"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_Slicer: Failed to spawn projectile"));
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
    }
}

void UGA_Slicer::HandleSlicerHit(AActor* HitActor, FVector HitLocation)
{
    if (!HitActor)
    {
        return;
    }

    UAbilitySystemComponent* TargetASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);

    if (TargetASC)
    {
        // Apply stagger
        if (StaggerEffectClass)
        {
            FGameplayEffectContextHandle StaggerContext = TargetASC->MakeEffectContext();
            FGameplayEffectSpecHandle StaggerSpec =
                TargetASC->MakeOutgoingSpec(StaggerEffectClass, 1.f, StaggerContext);

            if (StaggerSpec.IsValid())
            {
                TargetASC->ApplyGameplayEffectSpecToSelf(*StaggerSpec.Data.Get());
                UE_LOG(LogTemp, Log, TEXT("GA_Slicer: Stagger applied to %s"), *HitActor->GetName());
            }
        }

        // Apply damage — same centralized pattern as OnFire
        if (UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo())
        {
            if (DamageEffectClass)
            {
                FGameplayEffectContextHandle DamageContext = SourceASC->MakeEffectContext();
                FGameplayEffectSpecHandle DamageSpec =
                    SourceASC->MakeOutgoingSpec(DamageEffectClass, 1.f, DamageContext);

                if (DamageSpec.IsValid())
                {
                    DamageSpec.Data->SetSetByCallerMagnitude(
                        FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
                        SlicerDamage);

                    SourceASC->ApplyGameplayEffectSpecToTarget(*DamageSpec.Data.Get(), TargetASC);

                    // Fan the hit out to every client. GA_Fire has always done this;
                    // Slicer applied damage and told nobody, so on remote machines the
                    // impact was a silent number decrement. bWasVital is false — the
                    // Slicer doesn't resolve vitals today.
                    if (AGothicEnemyBase* HitEnemy = Cast<AGothicEnemyBase>(HitActor))
                    {
                        HitEnemy->MulticastOnHit(HitActor->GetActorLocation(), false, SlicerDamage);
                    }

                    UE_LOG(LogTemp, Log, TEXT("GA_Slicer: %.1f damage applied to %s"),
                        SlicerDamage, *HitActor->GetName());
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("GA_Slicer: DamageEffectClass not assigned in Blueprint"));
            }
        }

        EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
            GetCurrentActivationInfo(), true, false);
    }
}
void UGA_Slicer::HandleSlicerExpired()
{
    EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(),
        GetCurrentActivationInfo(), true, false);
}
void UGA_Slicer::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    if (SpawnedProjectile)
    {
        SpawnedProjectile->OnSlicerHit.RemoveDynamic(this, &UGA_Slicer::HandleSlicerHit);
        SpawnedProjectile->OnSlicerExpired.RemoveDynamic(this, &UGA_Slicer::HandleSlicerExpired); 
        SpawnedProjectile = nullptr;
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}