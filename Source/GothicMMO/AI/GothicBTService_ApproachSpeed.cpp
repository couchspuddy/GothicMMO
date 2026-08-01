// BTService_ApproachSpeed.cpp

#include "AI/GothicBTService_ApproachSpeed.h"
#include "AI/GothicEnemyAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"

UBTService_ApproachSpeed::UBTService_ApproachSpeed()
{
    NodeName = TEXT("Scale Approach Speed");

    // Tick frequently for smooth deceleration
    Interval = 0.1f;
    RandomDeviation = 0.02f;
}

FString UBTService_ApproachSpeed::GetStaticDescription() const
{
    return TEXT("Scales movement speed based on distance to target.\n"
               "Decelerates linearly within the controller's ApproachDecelDistance.");
}

void UBTService_ApproachSpeed::TickNode(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(OwnerComp.GetAIOwner());
    if (!AIC)
    {
        return;
    }

    ACharacter* OwnerChar = Cast<ACharacter>(AIC->GetPawn());
    if (!OwnerChar)
    {
        return;
    }

    AActor* Target = AIC->GetTargetActor();
    if (!Target)
    {
        // No target — restore full speed
        OwnerChar->GetCharacterMovement()->MaxWalkSpeed = AIC->GetDefaultWalkSpeed();
        return;
    }

    // HORIZONTAL, matching every other range in the engagement model
    // (IsTargetInAttackRange, WeightedActionSelect's bands). ApproachDecelDistance
    // and PreferredEngageDistance are authored as floor distances; measuring them
    // in 3D makes a tall creature think it is further out than it is and hold
    // full speed into contact, then never reach the "at engage distance" branch
    // at all — the boss's capsule half-height alone contributed 165uu here.
    const float DistToTarget = FVector::Dist2D(
        OwnerChar->GetActorLocation(),
        Target->GetActorLocation());

    const float DecelDist  = AIC->GetApproachDecelDistance();
    const float EngageDist = AIC->GetPreferredEngageDistance();
    const float FullSpeed  = AIC->GetDefaultWalkSpeed();
    const float MinMult    = AIC->GetDecelSpeedMultiplier();

    if (DistToTarget > DecelDist)
    {
        // Outside decel zone — full speed
        OwnerChar->GetCharacterMovement()->MaxWalkSpeed = FullSpeed;
    }
    else if (DistToTarget > EngageDist)
    {
        // Inside decel zone — interpolate linearly from full to min
        // At DecelDist: Alpha = 0 → full speed
        // At EngageDist: Alpha = 1 → min speed
        //
        // The band width is guarded because it is Blueprint data, not a constant.
        // Today's CDOs are safe (500 against 300 and 150), but a child that sets
        // ApproachDecelDistance equal to PreferredEngageDistance divides by zero
        // here and writes an inf/NaN MaxWalkSpeed straight into the movement
        // component — which does not throw, it just makes the pawn stop obeying
        // physics. A degenerate band means there is no ramp to interpolate along,
        // so the correct answer is the far end of it: full speed.
        const float BandWidth = DecelDist - EngageDist;
        if (BandWidth <= KINDA_SMALL_NUMBER)
        {
            OwnerChar->GetCharacterMovement()->MaxWalkSpeed = FullSpeed;
            return;
        }

        const float Alpha = 1.f - ((DistToTarget - EngageDist) / BandWidth);
        const float SpeedMult = FMath::Lerp(1.f, MinMult, Alpha);
        OwnerChar->GetCharacterMovement()->MaxWalkSpeed = FullSpeed * SpeedMult;
    }
    else
    {
        // At or past engage distance — hold at min speed
        OwnerChar->GetCharacterMovement()->MaxWalkSpeed = FullSpeed * MinMult;
    }
}

void UBTService_ApproachSpeed::OnCeaseRelevant(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    Super::OnCeaseRelevant(OwnerComp, NodeMemory);

    // Restore full speed when this service stops running
    // (branch exits — enemy transitions to attack, retreats, etc.)
    AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(OwnerComp.GetAIOwner());
    if (!AIC)
    {
        return;
    }

    ACharacter* OwnerChar = Cast<ACharacter>(AIC->GetPawn());
    if (OwnerChar)
    {
        OwnerChar->GetCharacterMovement()->MaxWalkSpeed = AIC->GetDefaultWalkSpeed();
    }
}