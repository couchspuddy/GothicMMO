// GothicBTTask_EvadeStep.cpp

#include "AI/GothicBTTask_EvadeStep.h"
#include "Game/GothicDeterminism.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"

UGothicBTTask_EvadeStep::UGothicBTTask_EvadeStep()
{
	NodeName = TEXT("Gothic Evade Step");

	TargetActorKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UGothicBTTask_EvadeStep, TargetActorKey),
		AActor::StaticClass());
	OutputPointKey.AddVectorFilter(this,
		GET_MEMBER_NAME_CHECKED(UGothicBTTask_EvadeStep, OutputPointKey));
}

void UGothicBTTask_EvadeStep::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	if (UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		TargetActorKey.ResolveSelectedKey(*BBAsset);
		OutputPointKey.ResolveSelectedKey(*BBAsset);
	}
}

EBTNodeResult::Type UGothicBTTask_EvadeStep::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	const AAIController* AIC = OwnerComp.GetAIOwner();
	APawn* Pawn               = AIC ? AIC->GetPawn() : nullptr;

	if (!BB || !Pawn || TargetActorKey.IsNone() || OutputPointKey.IsNone())
	{
		UE_LOG(LogVigilCombat, Warning,
			TEXT("VigilTimeline|Task=EvadeStep|Event=BadBinding|pawn=%s|targetKey=%s|outKey=%s"),
			*GetNameSafe(Pawn), TargetActorKey.IsNone() ? TEXT("NONE") : TEXT("set"),
			OutputPointKey.IsNone() ? TEXT("NONE") : TEXT("set"));
		return EBTNodeResult::Failed;
	}

	const AActor* Target = Cast<AActor>(
		BB->GetValue<UBlackboardKeyType_Object>(TargetActorKey.GetSelectedKeyID()));
	if (!Target)
	{
		UE_LOG(LogVigilCombat, Verbose,
			TEXT("VigilTimeline|Task=EvadeStep|Event=NoTarget|pawn=%s"), *GetNameSafe(Pawn));
		return EBTNodeResult::Failed;
	}

	// Feet, not GetActorLocation(): the actor location is the capsule ORIGIN, which
	// on a scaled pawn sits well above the floor and can exceed UE's default nav
	// query extent, so a point built off it fails projection and the paired Move To
	// reports that failure as an instant finish. Nav agent location is the feet and
	// projects cleanly — the same trap ComputeRepositionPoint documents.
	const FVector PawnFeet = Pawn->GetNavAgentLocation();

	// Bearing from pawn toward the target. The sidestep is rolled off THIS, so the
	// weave is always lateral to the current engagement line rather than an
	// arbitrary world direction.
	const FVector ToTarget = (Target->GetActorLocation() - PawnFeet).GetSafeNormal2D();
	const float ToTargetBearing = FMath::Atan2(ToTarget.Y, ToTarget.X);

	// Side (left/right) and the angle within the lateral band, both seeded so a
	// vigil.Deterministic measurement run replays identically. Never FMath::Rand*.
	const float Side = (FGothicDeterminism::RandRange(0, 1) == 0) ? 1.f : -1.f;
	const float OffsetDeg = FGothicDeterminism::FRandRange(
		FMath::Min(MinAngleOffset, MaxAngleOffset),
		FMath::Max(MinAngleOffset, MaxAngleOffset)) * Side;

	const float StepBearing = ToTargetBearing + FMath::DegreesToRadians(OffsetDeg);
	const FVector StepDir(FMath::Cos(StepBearing), FMath::Sin(StepBearing), 0.f);

	FVector EvadePoint = PawnFeet + StepDir * StepDistance;

	// Project onto the navmesh so the written point is somewhere the paired Move To
	// can actually reach. On failure keep the raw point — a Move To to an
	// unreachable spot fails cleanly, which is no worse than not writing at all.
	if (const UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn->GetWorld()))
	{
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(EvadePoint, Projected))
		{
			EvadePoint = Projected.Location;
		}
	}

	BB->SetValue<UBlackboardKeyType_Vector>(OutputPointKey.GetSelectedKeyID(), EvadePoint);

	UE_LOG(LogVigilCombat, Verbose,
		TEXT("VigilTimeline|Task=EvadeStep|Event=Step|pawn=%s|point=%s|moveDist2D=%.1f|offsetDeg=%.1f"),
		*GetNameSafe(Pawn), *EvadePoint.ToCompactString(),
		FVector::Dist2D(PawnFeet, EvadePoint), OffsetDeg);

	return EBTNodeResult::Succeeded;
}

FString UGothicBTTask_EvadeStep::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("%s\nSidestep %.0fuu, %.0f-%.0f deg off the target bearing (seeded L/R)"),
		*Super::GetStaticDescription(), StepDistance, MinAngleOffset, MaxAngleOffset);
}
