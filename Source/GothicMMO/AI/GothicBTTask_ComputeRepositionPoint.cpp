// GothicBTTask_ComputeRepositionPoint.cpp

#include "AI/GothicBTTask_ComputeRepositionPoint.h"
#include "AI/GothicBossArenaManager.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
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

    // Rotunda pillar escalation, applied to the hold and nothing else.
    //
    // The menace-hold is the single most direct expression of "not aggressive
    // yet": it writes the boss's OWN location as her reposition point, so a hold
    // is a beat where she is deliberately not closing. Dividing the chance by
    // the arena aggression multiplier means a stripped arena buys those beats
    // back as movement — 0.35 at four pillars, 0.175 at zero. That is the
    // multiplier used as itself, not an invented curve, and it is exactly inert
    // at the baseline 1.0, so levels with no arena manager (every non-boss
    // encounter) roll the authored chance unchanged.
    //
    // HoldDuration is deliberately NOT scaled: a hold that fires should still
    // read as a full, legible beat of her sizing the player up. Aggression makes
    // holds RARER, not clipped — a half-length stare is just a stutter.
    //
    // Looked up per activation rather than cached: a Reposition activates on the
    // order of once per second at worst, against one manager in the level.
    //
    // The division is CLAMPED to the authored chance, and that clamp is the
    // whole safety of this block rather than a formality. A bare divide runs the
    // wrong way for any multiplier below 1.0 — it INFLATES the hold chance,
    // which is the opposite of the stated intent — and below 0.35 it drives the
    // result past 1.0, at which point FRand() < EffectiveHoldChance is always
    // true and every single Reposition becomes a hold, forever, with the boss
    // never taking a step again. GetAggressionMultiplier() indexes an
    // AggressionByPillarCount array that is authored per placed instance, so
    // nothing in C++ guarantees it stays >= 1.0; the clamp makes that
    // irrelevant. Aggression may only ever buy holds back as movement.
    float Aggression           = 1.f;
    float EffectiveHoldChance  = HoldChance;
    if (const AGothicBossArenaManager* Arena = Cast<AGothicBossArenaManager>(
            UGameplayStatics::GetActorOfClass(Pawn->GetWorld(), AGothicBossArenaManager::StaticClass())))
    {
        Aggression = Arena->GetAggressionMultiplier();
        if (Aggression > KINDA_SMALL_NUMBER)
        {
            EffectiveHoldChance = FMath::Clamp(HoldChance / Aggression, 0.f, HoldChance);
        }
    }

    const float Now = Pawn->GetWorld() ? Pawn->GetWorld()->GetTimeSeconds() : 0.f;

    // Roll the menacing hold. On a hold, point the paired Move To at where she
    // already stands (a no-op walk) and stay InProgress so she plants and stares
    // the player down for HoldDuration instead of strafing off. The focus lock
    // on the controller keeps her facing the target the whole beat.
    if (EffectiveHoldChance > 0.f && FMath::FRand() < EffectiveHoldChance)
    {
        // GetNavAgentLocation(), not GetActorLocation(). The hold itself is a
        // genuine stall and stays one — but the point written has to be a point
        // the navmesh can actually accept. GetActorLocation() on this pawn is
        // the capsule ORIGIN, ~379.5uu above the floor (253 half-height x 1.5
        // scale), which is past UE's 250uu DEFAULT_NAV_QUERY_EXTENT_VERTICAL
        // and therefore unprojectable: the post-hold Move To fails its
        // projection and reports that failure as an instant finish, which is
        // indistinguishable in a log from an honest no-op. Nav agent location
        // is the feet, projects cleanly, and makes the no-op honest.
        const FVector HoldPoint = Pawn->GetNavAgentLocation();

        BB->SetValue<UBlackboardKeyType_Vector>(
            OutputPointKey.GetSelectedKeyID(), HoldPoint);

        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|Reposition|HOLD|aggression=%.3f|holdChance=%.3f|point=%s|moveDist2D=%.1f|holdDur=%.2f"),
            Now, *GetNameSafe(Pawn), Aggression, EffectiveHoldChance,
            *HoldPoint.ToCompactString(),
            FVector::Dist2D(Pawn->GetActorLocation(), HoldPoint), HoldDuration);

        Memory->bHolding = true;
        return EBTNodeResult::InProgress;
    }

    // Random offset within the configured band, random left/right.
    const float OffsetDeg = FMath::FRandRange(MinAngleOffset, MaxAngleOffset)
        * (FMath::RandBool() ? 1.f : -1.f);
    float NewBearing = CurrentBearing + FMath::DegreesToRadians(OffsetDeg);

    // Orbit at the distance the pawn is ALREADY at, not at a fixed radius.
    //
    // This used to seat the point at RepositionRadius from the target
    // unconditionally, which throws away the pawn's current separation and
    // makes every Reposition a retreat: the Bestial Lucid in capsule contact
    // at ~150uu was sent to 350uu, and a Thrall at a measured 199uu was sent
    // to 450uu — a reposition point FARTHER from the player than the pawn
    // already stood. The bearing offset is what makes a reposition a strafe;
    // the radius change was never part of the intent (see the header: the
    // offset is meant to be meaningful "from the pawn's actual current
    // position", and only the bearing ever was).
    //
    // RepositionRadius keeps its authored values and its purpose — it is now
    // a CEILING, so a pawn still closing from long range strafes on a sane arc
    // instead of a huge one, but a pawn already in its attack band strafes
    // inside that band and stays swingable. Nothing new is tuned here: the
    // lower bound is whatever distance the approach logic already converged
    // on (measured floor 125uu, capsule contact), and min(150, 350) = 150 sits
    // inside the Claw band's 160uu reach where the old 350 did not.
    //
    // If the two are effectively stacked, there is no current separation worth
    // preserving (and no meaningful bearing either — GetSafeNormal2D returned
    // zero above, so CurrentBearing is an arbitrary 0). Fall back to the
    // authored radius so the pawn steps out rather than writing the target's
    // own location as the destination.
    const float CurrentDistance2D =
        FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation());
    const float OrbitRadius = CurrentDistance2D > KINDA_SMALL_NUMBER
        ? FMath::Min(CurrentDistance2D, RepositionRadius)
        : RepositionRadius;

    // Minimum-displacement floor — the strafe has to be a WALK, not a shuffle.
    //
    // Everything above decides where on the arc to stand; none of it controls
    // how far the pawn actually travels to get there, and at close range those
    // are very different numbers. Both the pawn and the point sit on circles
    // around the target, so the displacement is a chord, and the chord shrinks
    // with the separation: at the boss's measured 125uu parking distance a
    // 45-110 degree offset moves her only 96-205uu. A stock Move To's reach
    // test folds the agent radius into its acceptance distance, and her capsule
    // is 90uu (60 x 1.5 scale), so a chord that short can be swallowed whole —
    // AAIController::MoveTo takes the AlreadyAtGoal branch and returns
    // instantly, with no path request and no log line to say so. That is the
    // one explanation that accounts for an EXACT 0.0uu across 299 samples on a
    // navmesh proven healthy by a 1,095uu walk in the same build.
    //
    // Rather than tune against the paired node's AcceptableRadius (which is
    // authored in the Behavior Tree asset and can change without this file
    // knowing), enforce the floor here so the fix holds for any value of it.
    //
    // Order of attack matters. Step the point OUTWARD first, keeping the
    // authored bearing: solving R2 = R1*cos(t) + sqrt(d^2 - R1^2*sin^2(t)) for
    // the law of cosines gives the smallest radius on the chosen bearing that
    // clears the floor, and it preserves the strafe's shape — a lateral arc,
    // just a wider one. Only if RepositionRadius runs out before the floor is
    // met do we widen the bearing instead, which is the uglier lever because a
    // very wide offset starts reading as a circle-around rather than a strafe.
    // The point never moves INWARD: R2 is floored at OrbitRadius, so a
    // reposition can still never become a retreat toward the player, which was
    // the whole point of the orbit-at-current-separation rule above.
    const float R1 = CurrentDistance2D;
    auto Displacement = [R1](float RadiusOut, float AngleRad)
    {
        return FMath::Sqrt(FMath::Max(0.f,
            R1 * R1 + RadiusOut * RadiusOut - 2.f * R1 * RadiusOut * FMath::Cos(AngleRad)));
    };

    float AngleRad    = FMath::DegreesToRadians(FMath::Abs(OffsetDeg));
    float PointRadius = OrbitRadius;

    if (Displacement(PointRadius, AngleRad) < MinRepositionDistance)
    {
        const float CosT = FMath::Cos(AngleRad);
        const float SinT = FMath::Sin(AngleRad);
        const float Disc = MinRepositionDistance * MinRepositionDistance - R1 * R1 * SinT * SinT;
        if (Disc >= 0.f)
        {
            PointRadius = FMath::Max(PointRadius, R1 * CosT + FMath::Sqrt(Disc));
        }
        // Never inside the current separation, never outside the authored ceiling.
        PointRadius = FMath::Clamp(PointRadius, OrbitRadius, FMath::Max(OrbitRadius, RepositionRadius));
    }

    if (Displacement(PointRadius, AngleRad) < MinRepositionDistance
        && R1 > KINDA_SMALL_NUMBER && PointRadius > KINDA_SMALL_NUMBER)
    {
        // Ceiling reached and still short — buy the rest with bearing. 170 deg
        // is the hard stop: past that the "offset" is just a flip to the far
        // side of the target and the arc stops reading as a strafe at all.
        const float CosNeeded =
            (R1 * R1 + PointRadius * PointRadius - MinRepositionDistance * MinRepositionDistance)
            / (2.f * R1 * PointRadius);
        AngleRad = CosNeeded <= -1.f
            ? PI
            : FMath::Max(AngleRad, FMath::Acos(FMath::Clamp(CosNeeded, -1.f, 1.f)));
        AngleRad = FMath::Min(AngleRad, FMath::DegreesToRadians(170.f));
    }

    // Re-derive the bearing from the (possibly widened) angle, keeping the side
    // the original roll picked.
    NewBearing = CurrentBearing + AngleRad * FMath::Sign(OffsetDeg);

    const FVector Direction(FMath::Cos(NewBearing), FMath::Sin(NewBearing), 0.f);
    const FVector ComputedPoint =
        Target->GetActorLocation() + Direction * PointRadius;

    BB->SetValue<UBlackboardKeyType_Vector>(OutputPointKey.GetSelectedKeyID(), ComputedPoint);

    // moveDist2D is the number that settles it: if this reads well above the
    // floor and the boss still measures 0.0uu of travel, the goal point is not
    // the problem and the Move To node itself is.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|Reposition|STRAFE|aggression=%.3f|holdChance=%.3f|point=%s|moveDist2D=%.1f|offsetDeg=%.1f|sepFrom=%.1f|sepTo=%.1f"),
        Now, *GetNameSafe(Pawn), Aggression, EffectiveHoldChance,
        *ComputedPoint.ToCompactString(),
        FVector::Dist2D(Pawn->GetActorLocation(), ComputedPoint),
        FMath::RadiansToDegrees(AngleRad) * FMath::Sign(OffsetDeg),
        CurrentDistance2D, PointRadius);

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
    return FString::Printf(TEXT("%s\nOffset: %.0f-%.0f deg, Radius: %.0f, Min move: %.0f"),
        *Super::GetStaticDescription(), MinAngleOffset, MaxAngleOffset,
        RepositionRadius, MinRepositionDistance);
}