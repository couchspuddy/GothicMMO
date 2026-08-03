// GothicBTTask_FindNearestPillar.cpp

#include "AI/GothicBTTask_FindNearestPillar.h"
#include "AI/GothicRotundaPillar.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "AI/NavigationSystemBase.h"
#include "Kismet/KismetSystemLibrary.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "GameFramework/Pawn.h"

class UNavigationSystemV1;

UGothicBTTask_FindNearestPillar::UGothicBTTask_FindNearestPillar()
{
    NodeName = TEXT("Gothic Find Nearest Pillar");

    OutputLocationKey.AddVectorFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTTask_FindNearestPillar, OutputLocationKey));
}

void UGothicBTTask_FindNearestPillar::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);

    if (UBlackboardData* BBAsset = GetBlackboardAsset())
    {
        OutputLocationKey.ResolveSelectedKey(*BBAsset);
    }
}

EBTNodeResult::Type UGothicBTTask_FindNearestPillar::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    const AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* Pawn               = AIC ? AIC->GetPawn() : nullptr;

    if (!BB || !Pawn || OutputLocationKey.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("FindNearestPillar[%s]: missing BB/Pawn/OutputLocationKey binding"),
            *GetNameSafe(Pawn));
        return EBTNodeResult::Failed;
    }

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(Pawn);

    TArray<AActor*> Overlapping;
    UKismetSystemLibrary::SphereOverlapActors(
        Pawn,
        Pawn->GetActorLocation(),
        SearchRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_WorldStatic) },
        nullptr,
        IgnoreActors,
        Overlapping);

    AActor* Nearest = nullptr;
    float NearestDistSq = TNumericLimits<float>::Max();

    for (AActor* Candidate : Overlapping)
    {
        if (!Candidate || !Candidate->ActorHasTag(TerrainTag))
        {
            continue;
        }

        // Destroyed pillars were being excluded only by accident, and only
        // partially: FinishCeilingCollapse turns the collision off, so the
        // overlap stopped returning them AFTER the slab landed. For the whole
        // 1.75s telegraph between the pillar breaking and the ceiling arriving,
        // the collision is still live and this task would happily send her to
        // stand under a section of roof that is already falling. The state is
        // set at the start of that window, so testing it closes the gap.
        //
        // Non-pillar actors carrying the tag are still accepted: the tag is the
        // contract (a level can tag any smashable geometry), and only actors
        // that ARE pillars have a destroyed state to consult.
        const AGothicRotundaPillar* Pillar = Cast<AGothicRotundaPillar>(Candidate);
        if (Pillar && Pillar->IsDestroyed())
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Candidate->GetActorLocation());
        if (DistSq < NearestDistSq)
        {
            NearestDistSq = DistSq;
            Nearest = Candidate;
        }
    }

    if (!Nearest)
    {
        // ZERO-SURVIVOR FALLBACK.
        //
        // This used to return Failed, which aborted the phase-transition
        // Sequence and left the fight in Phase 1 forever. That was written as a
        // level-setup warning -- "tag at least one pillar" -- back when a
        // pillarless Rotunda could only mean somebody forgot. It cannot mean
        // that any more: with Wall Pound destroying pillars and the arena
        // manager's ambient clock destroying the rest, all four being gone is a
        // ROUTINE late-fight state, and hanging the fight on it is a shipping
        // bug rather than a diagnostic.
        //
        // So the ritual degrades instead of failing: she transitions where she
        // stands. The paired Move To (AcceptableRadius 60) completes immediately
        // against her own position and CompletePhaseTransition runs normally.
        // Losing the walk-to-a-pillar staging is a much smaller loss than losing
        // the phase.
        //
        // GetNavAgentLocation, NOT GetActorLocation. Her capsule origin sits
        // ~379.5uu above the navmesh -- past UE's 250uu query extent -- so a
        // Move To aimed at her actor location fails silently. That trap has
        // already cost this project two separate investigations; the nav agent
        // location is the foot position and is the only one Move To can accept.
        const FVector HereLocation = Pawn->GetNavAgentLocation();

        UE_LOG(LogTemp, Log,
            TEXT("FindNearestPillar[%s]: no surviving '%s'-tagged actor within %.0f -- ")
            TEXT("transitioning in place at %s."),
            *GetNameSafe(Pawn), *TerrainTag.ToString(), SearchRadius,
            *HereLocation.ToCompactString());

        BB->SetValue<UBlackboardKeyType_Vector>(OutputLocationKey.GetSelectedKeyID(), HereLocation);
        return EBTNodeResult::Succeeded;
    }

    // Offset back toward the pawn rather than using the pillar's bare origin --
    // see ApproachOffsetDistance's comment in the header. A raw origin sits
    // inside the pillar's own collision, off the navmesh, and Move To fails
    // there silently.
    // Offset back toward the pawn so Move To targets a reachable point,
    // not the pillar's own (likely inside-geometry) origin — then project
    // to navmesh so "reachable" is guaranteed rather than hoped.
    const FVector PillarLoc = Nearest->GetActorLocation();
    const FVector ToPawn = (Pawn->GetActorLocation() - PillarLoc).GetSafeNormal2D();
    FVector ApproachPoint = PillarLoc + ToPawn * ApproachOffsetDistance;

    // Drop the approach point to the pawn's own height before projecting. A
    // pillar tall enough to carry a ceiling has its origin at mid-height --
    // for the Rotunda columns that is ~2200uu above the floor -- and the
    // projection extent is a box around the query point, not a ray to the
    // ground. Projecting from the pillar's bare origin therefore missed the
    // navmesh entirely, ApproachPoint kept its mid-air Z, and the Move To that
    // follows failed silently: the Phase 2 sequence died one step after
    // FindNearestPillar reported success, which is a far more confusing
    // failure than the no-pillar-found case above.
    ApproachPoint.Z = Pawn->GetActorLocation().Z;

    if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn->GetWorld()))
    {
        FNavLocation Projected;
        // Vertical extent is generous for the same reason -- the floor under a
        // pillar can sit well below wherever the pawn happens to be standing.
        if (NavSys->ProjectPointToNavigation(ApproachPoint, Projected, FVector(300.f, 300.f, 2000.f)))
        {
            ApproachPoint = Projected.Location;
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("FindNearestPillar[%s]: approach point for %s did not project to navmesh ")
                TEXT("(%s) -- the following Move To will likely fail and stall the transition."),
                *GetNameSafe(Pawn), *GetNameSafe(Nearest), *ApproachPoint.ToCompactString());
        }
    }

    BB->SetValue<UBlackboardKeyType_Vector>(OutputLocationKey.GetSelectedKeyID(), ApproachPoint);
    

    return EBTNodeResult::Succeeded;
}

FString UGothicBTTask_FindNearestPillar::GetStaticDescription() const
{
    return FString::Printf(TEXT("%s\nTag: %s, Radius: %.0f"),
        *Super::GetStaticDescription(), *TerrainTag.ToString(), SearchRadius);
}