// GothicBTTask_MoveToOrbit.cpp

#include "AI/GothicBTTask_MoveToOrbit.h"
#include "AI/GothicEnemyAIController.h"
#include "Game/GothicDeterminism.h"
#include "GothicMMO.h"                          // LogVigilCombat
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "NavigationSystem.h"
#include "AI/NavigationSystemBase.h"
#include "Navigation/PathFollowingComponent.h"     // EPathFollowingRequestResult members

UGothicBTTask_MoveToOrbit::UGothicBTTask_MoveToOrbit()
{
    NodeName = TEXT("Gothic Move To Orbit");

    // Latent: the tick throttles re-picks and the task stays InProgress until the
    // tree aborts it.
    bNotifyTick = true;
}

EBTNodeResult::Type UGothicBTTask_MoveToOrbit::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    // Self-contained target resolution — the same route ApproachSpeed and the
    // rest of the engagement services take, no Blackboard key to wire.
    AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(OwnerComp.GetAIOwner());
    APawn* Pawn   = AIC ? AIC->GetPawn() : nullptr;
    AActor* Target = AIC ? AIC->GetTargetActor() : nullptr;

    if (!AIC || !Pawn || !Target)
    {
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|%s|Orbit|ABORT|reason=%s"),
            *GetNameSafe(Pawn),
            !AIC ? TEXT("notGothicEnemyController") : (!Target ? TEXT("noTarget") : TEXT("noPawn")));
        return EBTNodeResult::Failed;
    }

    FGothicOrbitMemory* Memory = reinterpret_cast<FGothicOrbitMemory*>(NodeMemory);
    Memory->TimeSinceRepick = 0.f;
    Memory->bMoveIssued     = false;

    IssueOrbitMove(OwnerComp, *Memory);

    // Persistent posture — never finishes on its own. A higher-priority branch
    // (attack once in range / holding a token) aborts it; AbortTask stops the walk.
    return EBTNodeResult::InProgress;
}

void UGothicBTTask_MoveToOrbit::TickTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    FGothicOrbitMemory* Memory = reinterpret_cast<FGothicOrbitMemory*>(NodeMemory);

    // If the target vanished mid-hold, drop out so the tree can re-select.
    const AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(OwnerComp.GetAIOwner());
    if (!AIC || !AIC->GetPawn() || !AIC->GetTargetActor())
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    Memory->TimeSinceRepick += DeltaSeconds;
    if (Memory->TimeSinceRepick >= RepickInterval)
    {
        Memory->TimeSinceRepick = 0.f;
        IssueOrbitMove(OwnerComp, *Memory);
    }
}

EBTNodeResult::Type UGothicBTTask_MoveToOrbit::AbortTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    // The ring hold owned the pawn's movement; stop it so whatever branch takes
    // over (usually the attack close) starts from a clean slate rather than
    // fighting a half-finished orbit path.
    if (AAIController* AIC = OwnerComp.GetAIOwner())
    {
        AIC->StopMovement();
    }
    return EBTNodeResult::Aborted;
}

void UGothicBTTask_MoveToOrbit::IssueOrbitMove(
    UBehaviorTreeComponent& OwnerComp, FGothicOrbitMemory& Memory) const
{
    AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(OwnerComp.GetAIOwner());
    APawn* Pawn   = AIC ? AIC->GetPawn() : nullptr;
    AActor* Target = AIC ? AIC->GetTargetActor() : nullptr;
    if (!AIC || !Pawn || !Target)
    {
        return;
    }

    FVector OrbitPoint;
    bool bProjected = false;
    if (!ComputeOrbitPoint(*Pawn, *Target, OrbitPoint, bProjected))
    {
        return;
    }

    // Already projected inside ComputeOrbitPoint when it could be; if projection
    // failed there, let MoveToLocation take its own crack at it rather than walk
    // to a point that may sit off-mesh (the wave-spawn / pillar-origin stall).
    const EPathFollowingRequestResult::Type Result = AIC->MoveToLocation(
        OrbitPoint,
        MoveAcceptanceRadius,
        /*bStopOnOverlap*/ true,
        /*bUsePathfinding*/ true,
        /*bProjectDestinationToNavigation*/ !bProjected,
        /*bCanStrafe*/ false,
        /*FilterClass*/ nullptr,
        /*bAllowPartialPath*/ true);

    Memory.bMoveIssued = (Result != EPathFollowingRequestResult::Failed);

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|Orbit|MOVE|engageDist=%.0f|point=%s|dist2D=%.1f|projected=%d|result=%d"),
        Pawn->GetWorld() ? Pawn->GetWorld()->GetTimeSeconds() : 0.f,
        *GetNameSafe(Pawn), EngagementDistance, *OrbitPoint.ToCompactString(),
        FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation()),
        bProjected ? 1 : 0, static_cast<int32>(Result));
}

bool UGothicBTTask_MoveToOrbit::ComputeOrbitPoint(
    const APawn& Pawn, const AActor& Target,
    FVector& OutPoint, bool& bOutProjected) const
{
    const FVector PawnLoc   = Pawn.GetActorLocation();
    const FVector TargetLoc = Target.GetActorLocation();

    // Current bearing from target to pawn — PRESERVED, never snapped. Each enemy
    // holds the angular sector it already occupies; the ring forms by everyone
    // sliding radially to EngagementDistance, not by circling to a slot.
    FVector ToPawn = (PawnLoc - TargetLoc).GetSafeNormal2D();
    if (ToPawn.IsNearlyZero())
    {
        // Pawn is standing on the target's exact 2D origin (the pile-up this task
        // exists to break). No bearing to preserve — roll one deterministically so
        // two coincident pawns do not pick the same direction and re-stack.
        const float Bearing = FMath::DegreesToRadians(FGothicDeterminism::FRandRange(0.f, 360.f));
        ToPawn = FVector(FMath::Cos(Bearing), FMath::Sin(Bearing), 0.f);
    }

    FVector Point = TargetLoc + ToPawn * EngagementDistance;

    // Lateral spacing nudge (2D). Excludes the target so it is purely enemy-vs-enemy.
    Point += ComputeSeparation(Pawn, Target);

    // Drop to the pawn's own height before projecting — the pawn's capsule origin
    // is the right Z to search from, and a generous vertical extent covers floor
    // height differences (same reasoning as FindNearestPillar's approach point).
    Point.Z = PawnLoc.Z;

    bOutProjected = false;
    if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn.GetWorld()))
    {
        FNavLocation Projected;
        if (NavSys->ProjectPointToNavigation(Point, Projected, FVector(150.f, 150.f, 500.f)))
        {
            Point = Projected.Location;
            bOutProjected = true;
        }
        else
        {
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|%s|Orbit|PROJECTFAIL|point=%s"),
                *GetNameSafe(&Pawn), *Point.ToCompactString());
        }
    }

    OutPoint = Point;
    return true;
}

FVector UGothicBTTask_MoveToOrbit::ComputeSeparation(
    const APawn& Self, const AActor& Target) const
{
    if (SeparationRadius <= KINDA_SMALL_NUMBER || SeparationPushStrength <= KINDA_SMALL_NUMBER)
    {
        return FVector::ZeroVector;
    }

    UWorld* World = Self.GetWorld();
    if (!World)
    {
        return FVector::ZeroVector;
    }

    const FVector SelfLoc = Self.GetActorLocation();

    TArray<FOverlapResult> Overlaps;
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GothicOrbitSeparation), /*bTraceComplex*/ false, &Self);

    FVector Accum = FVector::ZeroVector;
    if (World->OverlapMultiByObjectType(
            Overlaps, SelfLoc, FQuat::Identity, ObjParams,
            FCollisionShape::MakeSphere(SeparationRadius), Params))
    {
        for (const FOverlapResult& Result : Overlaps)
        {
            const AActor* Other = Result.GetActor();
            if (!Other || Other == &Self || Other == &Target)
            {
                continue;
            }
            if (!Cast<APawn>(Other))
            {
                continue;
            }

            FVector Away = (SelfLoc - Other->GetActorLocation()).GetSafeNormal2D();
            float Dist   = FVector::Dist2D(SelfLoc, Other->GetActorLocation());
            if (Away.IsNearlyZero())
            {
                // Exactly coincident — deterministic bearing so the two do not
                // shove along the same line and stay stacked.
                const float Bearing = FMath::DegreesToRadians(FGothicDeterminism::FRandRange(0.f, 360.f));
                Away = FVector(FMath::Cos(Bearing), FMath::Sin(Bearing), 0.f);
                Dist = 1.f;
            }

            // Closer packmates push harder; falls to zero at the radius edge.
            const float Weight = FMath::Clamp((SeparationRadius - Dist) / SeparationRadius, 0.f, 1.f);
            Accum += Away * Weight;
        }
    }

    // Normalize the crowd direction and cap the shove — N packmates cannot sum
    // into a launch. Magnitude saturates at SeparationPushStrength.
    return Accum.GetClampedToMaxSize(1.f) * SeparationPushStrength;
}

FString UGothicBTTask_MoveToOrbit::GetStaticDescription() const
{
    return FString::Printf(
        TEXT("%s\nRing: %.0fuu | Separation: %.0fuu push<=%.0f | Re-pick: %.2fs"),
        *Super::GetStaticDescription(), EngagementDistance,
        SeparationRadius, SeparationPushStrength, RepickInterval);
}
