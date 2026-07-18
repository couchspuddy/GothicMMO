// GothicEnemyAIController.cpp

#include "AI/GothicEnemyAIController.h"
#include "AI/GothicEnemyBase.h"
#include "AI/GothicCombatStateComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "TimerManager.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BrainComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"

AGothicEnemyAIController::AGothicEnemyAIController()
{
    bWantsPlayerState = false;
}

void AGothicEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    PatrolOrigin = InPawn->GetActorLocation();

    if (BehaviorTreeAsset)
    {
        RunBehaviorTree(BehaviorTreeAsset);

        if (Blackboard)
        {
            // Validate before writing. Every SetValueAs* below is a silent no-op
            // if its key is missing or the wrong type — this turns that into an error.
            ValidateBlackboardKeys();

            Blackboard->SetValueAsVector(GothicBBKeys::PatrolOrigin, PatrolOrigin);
            Blackboard->SetValueAsFloat(GothicBBKeys::AttackRange, MeleeAttackRange);
            Blackboard->SetValueAsBool(GothicBBKeys::bIsInCombat, false);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("BBValidate[%s]: RunBehaviorTree succeeded but Blackboard is null — check that %s has a Blackboard Asset assigned"),
                *GetNameSafe(InPawn), *GetNameSafe(BehaviorTreeAsset));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("BBValidate[%s]: No BehaviorTreeAsset assigned on %s — this enemy will never act"),
            *GetNameSafe(InPawn), *GetClass()->GetName());
    }

    GetWorldTimerManager().SetTimer(
        LeashCheckTimer,
        this,
        &AGothicEnemyAIController::CheckLeash,
        2.0f,
        true);
}

void AGothicEnemyAIController::OnUnPossess()
{
    Super::OnUnPossess();
    GetWorldTimerManager().ClearTimer(LeashCheckTimer);
    GetWorldTimerManager().ClearTimer(RegroupPauseTimer);
}

void AGothicEnemyAIController::SetBlackboardTarget(AActor* NewTarget)
{
    if (!Blackboard || !NewTarget)
    {
        return;
    }

    Blackboard->SetValueAsObject(GothicBBKeys::TargetActor,    NewTarget);
    Blackboard->SetValueAsVector(GothicBBKeys::TargetLocation, NewTarget->GetActorLocation());
    Blackboard->SetValueAsBool(GothicBBKeys::bIsInCombat,      true);
    Blackboard->SetValueAsBool(GothicBBKeys::bCanSeeTarget,    true);
}

void AGothicEnemyAIController::ClearCombatTarget()
{
    // This function has exactly two callers: CheckLeash (which logs "Leash broken"
    // immediately before) and AGothicPlayerCharacter's death path. If the target is
    // ever cleared and neither of those lines is above this one in the log, a third
    // caller exists that isn't in the C++ — i.e. a Blueprint or BT task.
    UE_LOG(LogTemp, Warning, TEXT("AIState[%s]: ClearCombatTarget CALLED — previous target was %s"),
        GetPawn() ? *GetPawn()->GetName() : TEXT("NoPawn"),
        *GetNameSafe(GetTargetActor()));

    if (!Blackboard)
    {
        return;
    }

    Blackboard->ClearValue(GothicBBKeys::TargetActor);
    Blackboard->SetValueAsBool(GothicBBKeys::bIsInCombat,   false);
    Blackboard->SetValueAsBool(GothicBBKeys::bCanSeeTarget, false);
}

void AGothicEnemyAIController::EnterRegroupPause(float Duration)
{
    if (!Blackboard)
    {
        return;
    }

    Blackboard->SetValueAsBool(GothicBBKeys::bPackRegroup, true);

    // SetTimer on an in-use handle resets it — a defensive property here,
    // not a bug: overlapping pauses extend rather than stack.
    GetWorldTimerManager().SetTimer(RegroupPauseTimer, [this]()
    {
        if (Blackboard)
        {
            Blackboard->SetValueAsBool(GothicBBKeys::bPackRegroup, false);
        }
    }, Duration, false);
}

AActor* AGothicEnemyAIController::GetTargetActor() const
{
    if (Blackboard)
    {
        return Cast<AActor>(Blackboard->GetValueAsObject(GothicBBKeys::TargetActor));
    }
    return nullptr;
}

bool AGothicEnemyAIController::IsTargetInAttackRange() const
{
    AActor* Target  = GetTargetActor();
    APawn*  OwnerPawn = GetPawn();  // renamed from Owner to avoid hiding AActor::Owner

    if (!Target || !OwnerPawn)
    {
        return false;
    }

    const float DistanceToTarget = FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation());
    return DistanceToTarget <= MeleeAttackRange;
}

void AGothicEnemyAIController::CheckLeash()
{
    APawn* OwnerPawn = GetPawn();  // renamed from Owner to avoid hiding AActor::Owner
    if (!OwnerPawn)
    {
        return;
    }

    if (bDebugAIState)
    {
        LogAIState(OwnerPawn);
    }

    if (Blackboard && Blackboard->GetValueAsBool(GothicBBKeys::bIsInCombat))
    {
        const float DistFromOrigin = FVector::Dist(OwnerPawn->GetActorLocation(), PatrolOrigin);

        if (DistFromOrigin > LeashRange)
        {
            UE_LOG(LogTemp, Log, TEXT("%s: Leash broken - returning to patrol origin."), *OwnerPawn->GetName());
            ClearCombatTarget();
            MoveToLocation(PatrolOrigin, 50.f);
        }
    }

    if (AActor* Target = GetTargetActor())
    {
        if (Blackboard)
        {
            Blackboard->SetValueAsVector(GothicBBKeys::TargetLocation, Target->GetActorLocation());
        }
    }
}

// =============================================================================
// Diagnostic — added July 16 to root-cause the boss disengagement defect.
//
// Filed cause was "perception drops the target." That cannot be what happens:
// OnPerceptionUpdated contains no clearing path, and the cited evidence line
// ("CombatState: X left combat") comes from UGothicCombatStateComponent's
// CombatExitGracePeriod timer — five seconds of no damage dealt or received —
// which strips State.InCombat, a tag no AI code in this project reads.
// MaxAge and CombatExitGracePeriod are both 5.0f, which is why the two stories
// were indistinguishable from the log.
//
// This dump samples every variable that could gate the BT, from every source
// that writes one, on the leash timer that already runs at 2s. One PIE pass
// where she stops answers it. Read the last line before she froze:
//
//   BB.Target=None                  -> something called ClearCombatTarget.
//                                      The ClearCombatTarget log names the caller;
//                                      if no leash/death line precedes it, the
//                                      caller is Blueprint or a BT task.
//   BB.Target valid, BB.CanSee=0    -> a BT service is writing bCanSeeTarget.
//                                      No C++ sets it false except ClearCombatTarget,
//                                      so the BT owns it. Fix lives in the asset.
//   BB.Target valid, CS.InCombat=0  -> a BT decorator is gating on State.InCombat.
//                                      Most likely candidate. Fix: drop the decorator,
//                                      or call NotifyCombatAction on pursuit, not
//                                      just on damage.
//   Brain=STOPPED                   -> something called StopLogic. Only OnDeath does
//                                      that today, so she is being killed or a BT
//                                      task is aborting the tree.
//   Percept.Sensed=0, everything    -> perception went stale and it genuinely does
//   else healthy, and she moves        not matter. Exonerated. Look elsewhere.
// =============================================================================
void AGothicEnemyAIController::LogAIState(APawn* OwnerPawn)
{
    AActor* Target = GetTargetActor();

    // Blackboard state — what the BT actually reads.
    const int32 BBInCombat = Blackboard ? (Blackboard->GetValueAsBool(GothicBBKeys::bIsInCombat) ? 1 : 0) : -1;
    const int32 BBCanSee   = Blackboard ? (Blackboard->GetValueAsBool(GothicBBKeys::bCanSeeTarget) ? 1 : 0) : -1;

    // Combat state component — the thing that emits "left combat".
    // Distinct from BB.InCombat. Nothing in the AI reads this; if the BT is
    // gating on it via a tag decorator, that is the defect.
    int32 CSInCombat = -1;
    if (const UGothicCombatStateComponent* CS = OwnerPawn->FindComponentByClass<UGothicCombatStateComponent>())
    {
        CSInCombat = CS->IsInCombat() ? 1 : 0;
    }

    // Perception — the accused. Is the target still actively sensed?
    int32 PerceptSensed = -1;
    if (UAIPerceptionComponent* Percept = OwnerPawn->FindComponentByClass<UAIPerceptionComponent>())
    {
        if (Target)
        {
            FActorPerceptionBlueprintInfo Info;
            if (Percept->GetActorsPerception(Target, Info))
            {
                PerceptSensed = 0;
                for (const FAIStimulus& Stim : Info.LastSensedStimuli)
                {
                    if (Stim.WasSuccessfullySensed())
                    {
                        PerceptSensed = 1;
                        break;
                    }
                }
            }
        }
    }

    const float DistToTarget = Target
        ? FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation())
        : -1.f;

    const bool bBrainRunning = GetBrainComponent() && GetBrainComponent()->IsRunning();

    UE_LOG(LogTemp, Warning,
        TEXT("AIState[%s]: BB.Target=%s BB.InCombat=%d BB.CanSee=%d CS.InCombat=%d Percept.Sensed=%d Dist=%.0f Brain=%s Encounter=%s"),
        *OwnerPawn->GetName(),
        *GetNameSafe(Target),
        BBInCombat,
        BBCanSee,
        CSInCombat,
        PerceptSensed,
        DistToTarget,
        bBrainRunning ? TEXT("Running") : TEXT("STOPPED"),
        Cast<AGothicEnemyBase>(OwnerPawn) && Cast<AGothicEnemyBase>(OwnerPawn)->OwningEncounter
            ? TEXT("Yes") : TEXT("No"));
}

// =============================================================================
// Blackboard key validation — added July 16.
//
// UBlackboardComponent::SetValueAs*/GetValueAs* resolve keys by FName at runtime.
// A missing key is not an error: the write is discarded and the read returns the
// type default. No warning, no log, no compile failure. The BT keeps running.
//
// BB_BestialLucid was authored separately from BB_Enemy and shipped without
// bIsInCombat and bCanSeeTarget. SetBlackboardTarget wrote all three keys; the
// object landed, both bools evaporated. The boss held a valid target, sensed the
// player at 103cm, ran her tree, and never entered a combat branch — because the
// two bools her BT gates on were permanently false and nothing could say so.
//
// Type matters as much as existence: a key named bIsInCombat authored as a Float
// discards SetValueAsBool just as silently. Both are checked.
// =============================================================================
void AGothicEnemyAIController::ValidateBlackboardKeys()
{
    if (!Blackboard)
    {
        return;
    }

    struct FExpectedKey
    {
        FName Name;
        UClass* Type;
        const TCHAR* TypeLabel;
    };

    const FExpectedKey Expected[] =
    {
        { GothicBBKeys::TargetActor,    UBlackboardKeyType_Object::StaticClass(), TEXT("Object") },
        { GothicBBKeys::TargetLocation, UBlackboardKeyType_Vector::StaticClass(), TEXT("Vector") },
        { GothicBBKeys::bCanSeeTarget,  UBlackboardKeyType_Bool::StaticClass(),   TEXT("Bool")   },
        { GothicBBKeys::bIsInCombat,    UBlackboardKeyType_Bool::StaticClass(),   TEXT("Bool")   },
        { GothicBBKeys::PatrolOrigin,    UBlackboardKeyType_Vector::StaticClass(), TEXT("Vector") },
        { GothicBBKeys::AttackRange,     UBlackboardKeyType_Float::StaticClass(),  TEXT("Float")  },
        { GothicBBKeys::ChosenAction,    UBlackboardKeyType_Name::StaticClass(),   TEXT("Name")   },
        { GothicBBKeys::RepositionPoint, UBlackboardKeyType_Vector::StaticClass(), TEXT("Vector") },
        { GothicBBKeys::bPackRegroup,    UBlackboardKeyType_Bool::StaticClass(),   TEXT("Bool")   },
};
    

    const UBlackboardData* Asset = Blackboard->GetBlackboardAsset();
    const FString AssetName = GetNameSafe(Asset);
    const FString PawnName  = GetNameSafe(GetPawn());

    int32 Problems = 0;

    for (const FExpectedKey& Key : Expected)
    {
        const FBlackboard::FKey KeyID = Blackboard->GetKeyID(Key.Name);

        if (KeyID == FBlackboard::InvalidKey)
        {
            UE_LOG(LogTemp, Error,
                TEXT("BBValidate[%s]: '%s' MISSING from %s (expected %s). Every read and write to this key is a silent no-op."),
                *PawnName, *Key.Name.ToString(), *AssetName, Key.TypeLabel);
            ++Problems;
            continue;
        }

        const TSubclassOf<UBlackboardKeyType> ActualType = Blackboard->GetKeyType(KeyID);

        if (*ActualType != Key.Type)
        {
            UE_LOG(LogTemp, Error,
                TEXT("BBValidate[%s]: '%s' on %s is type '%s', expected %s. Writes of the expected type are silently discarded."),
                *PawnName, *Key.Name.ToString(), *AssetName,
                *GetNameSafe(*ActualType), Key.TypeLabel);
            ++Problems;
        }
    }

    if (Problems > 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("BBValidate[%s]: %s has %d key problem(s) — this enemy will misbehave in ways that produce no other error. Fix the asset."),
            *PawnName, *AssetName, Problems);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("BBValidate[%s]: %s OK — all %d keys present and correctly typed."),
            *PawnName, *AssetName, (int32)UE_ARRAY_COUNT(Expected));
    }
}