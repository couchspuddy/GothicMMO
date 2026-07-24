// BTTask_BossCharge.cpp

#include "AI/BTTask_BossCharge.h"
#include "AI/GothicEnemyAIController.h"
#include "AI/GothicBossArenaManager.h"
#include "AI/GothicRotundaPillar.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "AIController.h"

UBTTask_BossCharge::UBTTask_BossCharge()
{
    NodeName = TEXT("Boss Charge");
    bNotifyTick = true;
    bCreateNodeInstance = false;
}

FString UBTTask_BossCharge::GetStaticDescription() const
{
    return FString::Printf(TEXT("Charge at %.0f speed, max %.0f cm\nPillar damage: %.0f | Stagger: %.1fs"),
        ChargeSpeed, MaxChargeDistance, PillarImpactDamage, StaggerDuration);
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