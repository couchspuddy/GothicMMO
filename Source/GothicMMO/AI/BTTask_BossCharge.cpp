// BTTask_BossCharge.cpp

#include "AI/BTTask_BossCharge.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AI/GothicEnemyAIController.h"
#include "AI/GothicBossArenaManager.h"
#include "AI/GothicRotundaPillar.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/GothicPlayerCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "AIController.h"

UBTTask_BossCharge::UBTTask_BossCharge()
{
    NodeName = TEXT("Boss Charge");
    bNotifyTick = true;
    bCreateNodeInstance = false;
}

FString UBTTask_BossCharge::GetStaticDescription() const
{
    return FString::Printf(
        TEXT("Charge at %.0f speed, max %.0f cm\nPillar damage: %.0f | Stagger: %.1fs\nPlayer damage: %.0f raw within %.0f cm"),
        ChargeSpeed, MaxChargeDistance, PillarImpactDamage, StaggerDuration,
        ChargeDamage, ChargeHitRadius);
}

uint16 UBTTask_BossCharge::GetInstanceMemorySize() const
{
    return sizeof(FBTBossChargeMemory);
}

EBTNodeResult::Type UBTTask_BossCharge::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(OwnerComp.GetAIOwner());
    if (!AIC)
    {
        return EBTNodeResult::Failed;
    }

    ACharacter* BossChar = Cast<ACharacter>(AIC->GetPawn());
    if (!BossChar)
    {
        return EBTNodeResult::Failed;
    }

    AActor* Target = AIC->GetTargetActor();
    if (!Target)
    {
        return EBTNodeResult::Failed;
    }

    // Initialize charge memory
    FBTBossChargeMemory* Memory = CastInstanceNodeMemory<FBTBossChargeMemory>(NodeMemory);
    Memory->ChargeStartLocation = BossChar->GetActorLocation();
    Memory->DistanceTraveled = 0.f;
    Memory->bImpacted = false;
    Memory->NumHitPawns = 0;
    FMemory::Memzero(Memory->HitPawns, sizeof(Memory->HitPawns));

    // Lock direction toward target — boss does NOT re-track mid-charge
    FVector ToTarget = Target->GetActorLocation() - BossChar->GetActorLocation();
    ToTarget.Z = 0.f;
    Memory->ChargeDirection = ToTarget.GetSafeNormal();

    // Cache and override walk speed
    Memory->DefaultWalkSpeed = BossChar->GetCharacterMovement()->MaxWalkSpeed;
    BossChar->GetCharacterMovement()->MaxWalkSpeed = ChargeSpeed;

    // Face the charge direction
    BossChar->SetActorRotation(Memory->ChargeDirection.Rotation());


    return EBTNodeResult::InProgress;
}

void UBTTask_BossCharge::TickTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
    float DeltaSeconds)
{
    FBTBossChargeMemory* Memory = CastInstanceNodeMemory<FBTBossChargeMemory>(NodeMemory);

    AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(OwnerComp.GetAIOwner());
    ACharacter* BossChar = AIC ? Cast<ACharacter>(AIC->GetPawn()) : nullptr;

    if (!BossChar)
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    // Move the boss along the locked direction
    const FVector Movement = Memory->ChargeDirection * ChargeSpeed * DeltaSeconds;
    BossChar->AddMovementInput(Memory->ChargeDirection, 1.f);
    Memory->DistanceTraveled = FVector::Dist(
        BossChar->GetActorLocation(), Memory->ChargeStartLocation);

    // Run over anyone standing in the lane. Checked every tick and before the
    // impact branch below, because the impact branch returns early — a player
    // pinned between the boss and the pillar she is about to hit has to take
    // the charge on the frame it arrives, not never.
    ApplyChargeDamage(BossChar, Memory);

    // Check for pillar impact
    AGothicBossArenaManager* ArenaManager = Cast<AGothicBossArenaManager>(
        UGameplayStatics::GetActorOfClass(BossChar->GetWorld(), AGothicBossArenaManager::StaticClass()));

    if (ArenaManager)
    {
        AGothicRotundaPillar* NearestPillar = ArenaManager->GetNearestSurvivingPillar(
            BossChar->GetActorLocation());

        if (NearestPillar)
        {
            float DistToPillar = FVector::Dist(
                BossChar->GetActorLocation(), NearestPillar->GetActorLocation());

            if (DistToPillar <= PillarImpactRadius)
            {

                NearestPillar->ApplyPillarDamage(PillarImpactDamage);
                Memory->bImpacted = true;
            }
        }
    }

    // Check for wall impact via forward trace
    if (!Memory->bImpacted)
    {
        FHitResult WallHit;
        FVector TraceStart = BossChar->GetActorLocation();
        FVector TraceEnd = TraceStart + (Memory->ChargeDirection * WallTraceDistance);
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(BossChar);

        if (BossChar->GetWorld()->LineTraceSingleByChannel(
            WallHit, TraceStart, TraceEnd, ECC_WorldStatic, Params))
        {

            Memory->bImpacted = true;
        }
    }

    // Handle impact — stop and stagger
    if (Memory->bImpacted)
    {
        BossChar->GetCharacterMovement()->StopMovementImmediately();
        BossChar->GetCharacterMovement()->MaxWalkSpeed = Memory->DefaultWalkSpeed;

        // Stagger timer — task finishes after the punish window
        FTimerHandle StaggerTimer;
        FTimerDelegate StaggerDelegate;
        StaggerDelegate.BindLambda([&OwnerComp, this]()
        {
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        });
        BossChar->GetWorldTimerManager().SetTimer(
            StaggerTimer, StaggerDelegate, StaggerDuration, false);

        return;
    }

    // Check max distance
    if (Memory->DistanceTraveled >= MaxChargeDistance)
    {

        BossChar->GetCharacterMovement()->StopMovementImmediately();
        BossChar->GetCharacterMovement()->MaxWalkSpeed = Memory->DefaultWalkSpeed;
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    }
}

void UBTTask_BossCharge::ApplyChargeDamage(
    ACharacter* BossChar, FBTBossChargeMemory* Memory) const
{
    if (!BossChar || !Memory || !ChargeDamageEffect || !BossChar->HasAuthority())
    {
        return;
    }

    UAbilitySystemComponent* BossASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(BossChar);
    if (!BossASC)
    {
        return;
    }

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(BossChar);

    TArray<AActor*> Overlapping;
    UKismetSystemLibrary::SphereOverlapActors(
        BossChar,
        BossChar->GetActorLocation(),
        ChargeHitRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
        AGothicPlayerCharacter::StaticClass(),
        IgnoreActors,
        Overlapping);

    for (AActor* Victim : Overlapping)
    {
        if (!Victim)
        {
            continue;
        }

        // Once per pawn per charge. A charge lasts ~1.2s at these speeds, so
        // without this the player would take a hit every frame they stayed in
        // contact — a wall of damage rather than a single body check.
        bool bAlreadyHit = false;
        for (int32 i = 0; i < Memory->NumHitPawns; ++i)
        {
            if (Memory->HitPawns[i] == Victim)
            {
                bAlreadyHit = true;
                break;
            }
        }
        if (bAlreadyHit)
        {
            continue;
        }

        UAbilitySystemComponent* TargetASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Victim);
        if (!TargetASC)
        {
            continue;
        }

        // Same construction as GothicMeleeHitboxComponent: the boss PAWN is the
        // instigator so its AttackPower reaches the pipeline, flat damage rides
        // Data.Damage, and the target's Defense is subtracted by the attribute
        // set. Nothing charge-specific about the pipeline.
        FGameplayEffectContextHandle Context =
            UGothicAbilitySystemComponent::MakeDamageContext(BossASC, BossChar);

        FGameplayEffectSpecHandle Spec = BossASC->MakeOutgoingSpec(
            ChargeDamageEffect, 1.f, Context);

        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(
                FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
                ChargeDamage);

            BossASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);

            if (Memory->NumHitPawns < FBTBossChargeMemory::MaxTrackedHits)
            {
                Memory->HitPawns[Memory->NumHitPawns++] = Victim;
            }

            UE_LOG(LogTemp, Log,
                TEXT("BossCharge[%s]: ran over %s for %.0f raw"),
                *BossChar->GetName(), *Victim->GetName(), ChargeDamage);
        }
    }
}