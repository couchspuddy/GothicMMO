// GothicBTTask_ComputeRepositionPoint.cpp

#include "AI/GothicBTTask_ComputeRepositionPoint.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "GameFramework/Pawn.h"

UGothicBTTask_ComputeRepositionPoint::UGothicBTTask_ComputeRepositionPoint()
{
    NodeName = TEXT("Gothic Compute Reposition Point");

    // Needed so the menace hold can tick its timer down while InProgress.
    bNotifyTick = true;

    TargetActorKey.AddObjectFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTTask_ComputeRepositionPoint, TargetActorKey),
        AActor::StaticClass());
    OutputPointKey.AddVectorFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTTask_ComputeRepositionPoint, OutputPointKey));
}

void UGothicBTTask_ComputeRepositionPoint::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);

    if (UBlackboardData* BBAsset = GetBlackboardAsset())
    {
        TargetActorKey.ResolveSelectedKey(*BBAsset);
        OutputPointKey.ResolveSelectedKey(*BBAsset);
    }
}

EBTNodeResult::Type UGothicBTTask_ComputeRepositionPoint::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    const AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* Pawn               = AIC ? AIC->GetPawn() : nullptr;

    if (!BB || !Pawn || TargetActorKey.IsNone() || OutputPointKey.IsNone())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("ComputeRepositionPoint[%s]: missing BB/Pawn/key binding (TargetActorKey=%s, OutputPointKey=%s)"),
            *GetNameSafe(Pawn), TargetActorKey.IsNone() ? TEXT("NONE") : TEXT("set"),
            OutputPointKey.IsNone() ? TEXT("NONE") : TEXT("set"));
        return EBTNodeResult::Failed;
    }

    const AActor* Target = Cast<AActor>(
        BB->GetValue<UBlackboardKeyType_Object>(TargetActorKey.GetSelectedKeyID()));

    if (!Target)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("ComputeRepositionPoint[%s]: TargetActorKey resolved but returned no actor — is TargetActor actually populated on the Blackboard right now?"),
            *GetNameSafe(Pawn));
        return EBTNodeResult::Failed;
    }

    // Current bearing from target to pawn — the offset is applied relative to
    // THIS, not to the target's forward vector, so the reposition is always
    // meaningful from the pawn's actual current position rather than an
    // arbitrary world-space direction.
    const FVector ToPawn = (Pawn->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
    const float CurrentBearing = FMath::Atan2(ToPawn.Y, ToPawn.X);

    FGothicRepositionMemory* Memory = reinterpret_cast<FGothicRepositionMemory*>(NodeMemory);
    Memory->bHolding    = false;
    Memory->HoldElapsed = 0.f;

    // Roll the menacing hold. On a hold, point the paired Move To at where she
    // already stands (a no-op walk) and stay InProgress so she plants and stares
    // the player down for HoldDuration instead of strafing off. The focus lock
    // on the controller keeps her facing the target the whole beat.
    if (HoldChance > 0.f && FMath::FRand() < HoldChance)
    {
        BB->SetValue<UBlackboardKeyType_Vector>(
            OutputPointKey.GetSelectedKeyID(), Pawn->GetActorLocation());

        Memory->bHolding = true;
        return EBTNodeResult::InProgress;
    }

    // Random offset within the configured band, random left/right.
    const float OffsetDeg = FMath::FRandRange(MinAngleOffset, MaxAngleOffset)
        * (FMath::RandBool() ? 1.f : -1.f);
    const float NewBearing = CurrentBearing + FMath::DegreesToRadians(OffsetDeg);

    const FVector Direction(FMath::Cos(NewBearing), FMath::Sin(NewBearing), 0.f);
    const FVector ComputedPoint =
        Target->GetActorLocation() + Direction * RepositionRadius;

    BB->SetValue<UBlackboardKeyType_Vector>(OutputPointKey.GetSelectedKeyID(), ComputedPoint);

    return EBTNodeResult::Succeeded;
}

void UGothicBTTask_ComputeRepositionPoint::TickTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    FGothicRepositionMemory* Memory = reinterpret_cast<FGothicRepositionMemory*>(NodeMemory);
    if (!Memory->bHolding)
    {
        return;
    }

    Memory->HoldElapsed += DeltaSeconds;
    if (Memory->HoldElapsed >= HoldDuration)
    {
        Memory->bHolding = false;
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    }
}

FString UGothicBTTask_ComputeRepositionPoint::GetStaticDescription() const
{
    return FString::Printf(TEXT("%s\nOffset: %.0f-%.0f deg, Radius: %.0f"),
        *Super::GetStaticDescription(), MinAngleOffset, MaxAngleOffset, RepositionRadius);
}