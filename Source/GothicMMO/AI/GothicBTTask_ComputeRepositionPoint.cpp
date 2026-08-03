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

    // Hoisted above the branch rolls because the give-ground gate needs it to
    // decide ELIGIBILITY, not just to place a point. The strafe math below still
    // reads exactly the same value; nothing about its meaning changed.
    const float CurrentDistance2D =
        FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation());

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

    // -------------------------------------------------------------------------
    // Branch one: give ground. Rolled FIRST, ahead of the hold and the strafe.
    //
    // This is the only branch in the whole action pool that can INCREASE
    // separation. Approach closes, Charge closes, Claw and Roar fire in place,
    // and the strafe below is explicitly capped at the separation the pawn
    // already has, so without this the distance to the player only ever ratchets
    // down and the boss parks on top of them permanently — the measured ~125uu
    // with no punish window anywhere in the fight.
    //
    // It rolls first because it is the rarest and most deliberate beat: put it
    // after the hold and its effective rate becomes GiveGroundChance * (1 -
    // EffectiveHoldChance), which is both quieter than the authored number says
    // and a moving target as arena aggression scales the hold. Rolling first
    // makes the authored chance mean what it reads as.
    //
    // Gated on proximity, because a disengage is a response to being crowded,
    // not a general movement option — see GiveGroundTriggerRange.
    if (GiveGroundChance > 0.f
        && CurrentDistance2D < GiveGroundTriggerRange
        && FMath::FRand() < GiveGroundChance)
    {
        // Straight AWAY from the target: the target-to-pawn bearing, i.e. the
        // side of the arena she is already on, extended. Not a fresh random
        // bearing and not a cut across the target's front — either of those
        // would walk her through the player's firing line for the length of the
        // retreat, which is the opposite of buying a punish window.
        //
        // Jitter is a small dedicated spread rather than the strafe's 45-110
        // band (see GiveGroundAngleJitter): enough that repeated disengages do
        // not wear one groove into the arena, small enough that each still reads
        // unmistakably as backing off.
        const float JitterDeg = FMath::FRandRange(-GiveGroundAngleJitter, GiveGroundAngleJitter);
        const float AwayBearing = CurrentBearing + FMath::DegreesToRadians(JitterDeg);

        // Distance is measured FROM THE TARGET, not travelled from the pawn:
        // the point of the beat is the separation she ends at, and a
        // displacement-based retreat would land somewhere different every time
        // depending on how close she had crept.
        const float GiveGroundDistance = FMath::FRandRange(
            FMath::Min(GiveGroundMinDistance, GiveGroundMaxDistance),
            FMath::Max(GiveGroundMinDistance, GiveGroundMaxDistance));

        // Based on the TARGET's location, exactly as the strafe below is. The
        // pawn's own actor location is the capsule ORIGIN, ~379.5uu above the
        // floor at her scale, which is past UE's 250uu default nav query extent
        // and unprojectable — a point built off it fails projection and the
        // paired Move To reports that failure as an instant finish.
        const FVector AwayDirection(FMath::Cos(AwayBearing), FMath::Sin(AwayBearing), 0.f);
        const FVector GiveGroundPoint =
            Target->GetActorLocation() + AwayDirection * GiveGroundDistance;

        BB->SetValue<UBlackboardKeyType_Vector>(
            OutputPointKey.GetSelectedKeyID(), GiveGroundPoint);

        // MinRepositionDistance is deliberately not consulted here, and the two
        // cannot fight. That floor exists so a CHORD around the target does not
        // collapse below the paired Move To's reach test at close range; a
        // disengage is radial, not tangential, and its displacement is at least
        // (GiveGroundMinDistance - GiveGroundTriggerRange) = 350uu by
        // construction under the defaults — comfortably above the 200uu floor
        // and above any plausible re-tuning of it. Running the widen-then-step-
        // out solver on this point could only push it FARTHER out, which is
        // spending separation the branch has already decided how to spend.
        //
        // sepFrom/sepTo are the pair that make the beat readable on a timeline:
        // sepTo is the whole point of the branch, and the gap between them is
        // the punish window this exists to manufacture.
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|Reposition|GIVEGROUND|aggression=%.3f|holdChance=%.3f|point=%s|moveDist2D=%.1f|jitterDeg=%.1f|sepFrom=%.1f|sepTo=%.1f"),
            Now, *GetNameSafe(Pawn), Aggression, EffectiveHoldChance,
            *GiveGroundPoint.ToCompactString(),
            FVector::Dist2D(Pawn->GetActorLocation(), GiveGroundPoint),
            JitterDeg, CurrentDistance2D, GiveGroundDistance);

        // A real move, not a stall — Succeeded, like the strafe, so the
        // Sequence's Move To runs for real.
        return EBTNodeResult::Succeeded;
    }

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
    // That fix briefly kept RepositionRadius on as a CEILING —
    // min(CurrentDistance2D, RepositionRadius) — on the reasoning that a pawn
    // closing from long range should strafe on a sane arc rather than a huge
    // one. That reasoning was only ever safe while she was permanently pinned
    // in contact: with the pawn parked at 125uu the ceiling never bound, so it
    // could only stop a reposition becoming a retreat. It does not survive the
    // give-ground branch above, which legitimately seats her at 600-750uu. From
    // out there the same min() is not a ceiling at all — it is an APPROACH
    // wearing a strafe's name. Measured over 26 strafes in one session, 22
    // collapsed to exactly sepTo=350.0 (from 1217, 878, 686, 481...); the other
    // four "held" only because they were already inside 350. She gave ground
    // for exactly one action and the next reposition walked it back, which is
    // the whole of the user-facing "she never backs off".
    //
    // So the ceiling is gone: a strafe PRESERVES separation, full stop. The
    // clamp's stated purpose — a reposition must never seat the point farther
    // from the target than the pawn already stands — is served exactly by
    // orbiting at CurrentDistance2D itself, which by construction moves her
    // neither outward nor inward. And the "sane arc from long range" worry is
    // already covered elsewhere and better: Reposition is authored maxRange 800
    // on both phases, so the action pool cannot even SELECT a reposition beyond
    // 800uu. The widest orbit this task can be asked for is bounded by that
    // gate, where a designer can see it, rather than by this task's own
    // property.
    //
    // RepositionRadius survives only as the degenerate fallback below. If the
    // two are effectively stacked there is no current separation worth
    // preserving (and no meaningful bearing either — GetSafeNormal2D returned
    // zero above, so CurrentBearing is an arbitrary 0). Fall back to the
    // authored radius so the pawn steps out rather than writing the target's
    // own location as the destination.
    const float OrbitRadius = CurrentDistance2D > KINDA_SMALL_NUMBER
        ? CurrentDistance2D
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
    // Order of attack matters, and the arc goes FIRST. The separation is the
    // one thing this task is not allowed to spend: the previous fix
    // (orbit-at-current-separation, above) exists precisely because seating the
    // point at a fixed radius sent an enemy in contact at ~150uu out to 350uu —
    // a reposition point FARTHER from the player than the pawn already stood,
    // which drops her out of Claw's 160uu reach and turns every cooldown beat
    // into a walk back in. That fix is what took her from 0.51 to 1.168 DPS.
    // Buying displacement by pushing the radius outward would hand it straight
    // back, so the radius is the lever of LAST resort, not first.
    //
    // Widening the bearing costs nothing from that budget: at constant
    // separation the chord is 2*R*sin(t/2), so at her measured 125uu, 106 deg
    // buys 200uu and 170 deg buys 249uu — the same displacement the radius
    // solve produced, with the pawn still standing exactly as close as she was.
    // 170 deg is the hard stop; past that the "offset" is a flip to the far
    // side of the target and stops reading as a strafe at all.
    //
    // Only when the capped arc still cannot clear the floor — the genuinely
    // stacked or very-close case, where 2*R is itself under the floor and no
    // arc exists that would do it — does the radius step out. That solve is
    // R2 = R1*cos(t) + sqrt(d^2 - R1^2*sin^2(t)) from the law of cosines: the
    // smallest radius on the already-widened bearing that reaches the floor,
    // never below the current separation and never past the wider of it and the
    // authored RepositionRadius. It is a concession, taken only where the
    // alternative is another 0.0uu no-op walk.
    const float R1 = CurrentDistance2D;
    auto Displacement = [R1](float RadiusOut, float AngleRad)
    {
        return FMath::Sqrt(FMath::Max(0.f,
            R1 * R1 + RadiusOut * RadiusOut - 2.f * R1 * RadiusOut * FMath::Cos(AngleRad)));
    };

    const float MaxWidenedAngleRad = FMath::DegreesToRadians(170.f);

    float AngleRad    = FMath::DegreesToRadians(FMath::Abs(OffsetDeg));
    float PointRadius = OrbitRadius;

    // Lever one: widen the arc, holding the separation.
    if (Displacement(PointRadius, AngleRad) < MinRepositionDistance
        && R1 > KINDA_SMALL_NUMBER && PointRadius > KINDA_SMALL_NUMBER)
    {
        const float CosNeeded =
            (R1 * R1 + PointRadius * PointRadius - MinRepositionDistance * MinRepositionDistance)
            / (2.f * R1 * PointRadius);
        AngleRad = CosNeeded <= -1.f
            ? MaxWidenedAngleRad
            : FMath::Max(AngleRad, FMath::Acos(FMath::Clamp(CosNeeded, -1.f, 1.f)));
        AngleRad = FMath::Min(AngleRad, MaxWidenedAngleRad);
    }

    // Lever two, only if the widest legible arc still falls short.
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
    return FString::Printf(
        TEXT("%s\nOffset: %.0f-%.0f deg, Radius: %.0f, Min move: %.0f\nGive ground: %.0f%% under %.0fuu, out to %.0f-%.0f"),
        *Super::GetStaticDescription(), MinAngleOffset, MaxAngleOffset,
        RepositionRadius, MinRepositionDistance,
        GiveGroundChance * 100.f, GiveGroundTriggerRange,
        GiveGroundMinDistance, GiveGroundMaxDistance);
}