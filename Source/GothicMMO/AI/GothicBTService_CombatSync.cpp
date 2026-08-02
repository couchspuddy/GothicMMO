// GothicBTService_CombatSync.cpp

#include "AI/GothicBTService_CombatSync.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AI/GothicEnemyAIController.h"
#include "AI/GothicEnemyBase.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "GameFramework/Pawn.h"

UGothicBTService_CombatSync::UGothicBTService_CombatSync()
{
    NodeName = TEXT("Gothic Combat Sync");

    // 5Hz. Fast enough that Observer Aborts feels reactive, cheap enough that
    // it costs nothing at the enemy counts this project runs. RandomDeviation
    // staggers multiple enemies off the same frame.
    Interval        = 0.2f;
    RandomDeviation = 0.05f;

    bNotifyBecomeRelevant = true;

    // Editor dropdown filters. Array entries can't be filtered from a
    // constructor (they don't exist yet) — those selectors show all key
    // types; pick Bool keys.
    TargetActorKey.AddObjectFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTService_CombatSync, TargetActorKey),
        AActor::StaticClass());
    InAttackRangeKey.AddBoolFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTService_CombatSync, InAttackRangeKey));
    TargetLocationKey.AddVectorFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTService_CombatSync, TargetLocationKey));

    // The optional keys are optional — "None" means "don't write it."
    InAttackRangeKey.AllowNoneAsValue(true);
    TargetLocationKey.AllowNoneAsValue(true);
}

void UGothicBTService_CombatSync::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);

    if (UBlackboardData* BBAsset = GetBlackboardAsset())
    {
        TargetActorKey.ResolveSelectedKey(*BBAsset);
        InAttackRangeKey.ResolveSelectedKey(*BBAsset);
        TargetLocationKey.ResolveSelectedKey(*BBAsset);

        for (FGothicAbilityReadinessSync& Entry : AbilitiesToSync)
        {
            Entry.ReadyKey.ResolveSelectedKey(*BBAsset);
        }
    }
}

uint16 UGothicBTService_CombatSync::GetInstanceMemorySize() const
{
    return sizeof(FGothicCombatSyncMemory);
}

void UGothicBTService_CombatSync::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    Super::OnBecomeRelevant(OwnerComp, NodeMemory);

    // Arm the deferred check rather than running it here. At this moment the
    // pawn has not reached its own BeginPlay yet, so its StartupAbilities are
    // not granted and validating now reports every tag as missing.
    if (FGothicCombatSyncMemory* Memory = CastInstanceNodeMemory<FGothicCombatSyncMemory>(NodeMemory))
    {
        Memory->TimeSinceRelevant = 0.f;
        Memory->bValidated        = false;
        Memory->bTimelineSeeded   = false;
    }
}

void UGothicBTService_CombatSync::ValidateConfiguration(UBehaviorTreeComponent& OwnerComp) const
{
    APawn* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;

    // ── Nobody writes over a config input ────────────────────────────────────
    //
    // The keys the controller writes at OnPossess are the tree's INPUTS: the
    // creature's tuning, pushed into the Blackboard once. A service that writes
    // a computed value into one of them destroys the tuning silently and leaves
    // behind a key that looks authored and behaves like a sensor — which is
    // exactly how DistanceToTargetKey → AttackRange survived a full encounter.
    // The export is gone; this makes sure the remaining selectors cannot be
    // pointed at the same trap from the editor dropdown.
    static const FName ConfigOwnedKeys[] =
    {
        GothicBBKeys::AttackRange,
        GothicBBKeys::EngageDistance,
        GothicBBKeys::PatrolOrigin,
        GothicBBKeys::StaggerDelay,
    };

    auto WarnIfConfigKey = [Pawn](const FBlackboardKeySelector& Selector, const TCHAR* SelectorName)
    {
        if (Selector.IsNone())
        {
            return;
        }

        for (const FName& Reserved : ConfigOwnedKeys)
        {
            if (Selector.SelectedKeyName == Reserved)
            {
                UE_LOG(LogTemp, Error,
                    TEXT("CombatSync[%s]: %s is pointed at '%s', which AGothicEnemyAIController writes as "
                         "CONFIG at OnPossess. This service would overwrite that tuning value every tick — "
                         "repoint it at a key the tree owns, or set it to None."),
                    *GetNameSafe(Pawn), SelectorName, *Reserved.ToString());
                return;
            }
        }
    };

    WarnIfConfigKey(InAttackRangeKey,  TEXT("InAttackRangeKey"));
    WarnIfConfigKey(TargetLocationKey, TEXT("TargetLocationKey"));
    for (const FGothicAbilityReadinessSync& Entry : AbilitiesToSync)
    {
        WarnIfConfigKey(Entry.ReadyKey, TEXT("an ability ReadyKey"));
    }

    // Config validation, same philosophy as ValidateBlackboardKeys: turn silent
    // misconfiguration into a loud error. A tag that matches no granted ability
    // writes a permanently-false ReadyKey — which is indistinguishable from
    // "always on cooldown" without this line. Runs once, after the grace
    // period, so what it reports is the settled state rather than a race.
    UAbilitySystemComponent* ASC = ResolveASC(OwnerComp);
    if (!ASC)
    {
        UE_LOG(LogTemp, Error,
            TEXT("CombatSync[%s]: pawn has no AbilitySystemComponent — every ReadyKey will stay false"),
            *GetNameSafe(Pawn));
        return;
    }

    for (const FGothicAbilityReadinessSync& Entry : AbilitiesToSync)
    {
        if (!Entry.AbilityTag.IsValid())
        {
            continue;
        }

        bool bFound = false;
        for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
        {
            if (Spec.Ability && Spec.Ability->GetAssetTags().HasTag(Entry.AbilityTag))
            {
                bFound = true;
                break;
            }
        }

        if (!bFound)
        {
            // Two distinct causes, and the order matters — the second one is
            // what actually bit the Bestial Lucid's Wall Pound, and the first
            // is what everyone assumes it is.
            UE_LOG(LogTemp, Error,
                TEXT("CombatSync[%s]: after %.1fs, no granted ability carries AssetTag '%s'. "
                     "Either no Blueprint of that ability is in the pawn's StartupAbilities at all, "
                     "or one exists and its tag is in AbilityInputTag instead of AssetTags."),
                *GetNameSafe(Pawn), ValidationGraceSeconds, *Entry.AbilityTag.ToString());
        }
    }
}

void UGothicBTService_CombatSync::TickNode(UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    AAIController* AIC       = OwnerComp.GetAIOwner();
    APawn* Pawn              = AIC ? AIC->GetPawn() : nullptr;

    if (!BB || !Pawn)
    {
        return;
    }

    // ── Deferred config validation ───────────────────────────────────────────
    if (FGothicCombatSyncMemory* Memory = CastInstanceNodeMemory<FGothicCombatSyncMemory>(NodeMemory))
    {
        if (!Memory->bValidated)
        {
            Memory->TimeSinceRelevant += DeltaSeconds;
            if (Memory->TimeSinceRelevant >= ValidationGraceSeconds)
            {
                Memory->bValidated = true;
                ValidateConfiguration(OwnerComp);
            }
        }
    }

    // ── Ability readiness ────────────────────────────────────────────────────
    if (UAbilitySystemComponent* ASC = ResolveASC(OwnerComp))
    {
        // Diagnostic timeline (see the VigilTimeline block in
        // GothicBTService_WeightedActionSelect.cpp for the shared format).
        // Seeding is decided BEFORE the loop and applied AFTER it, so all
        // entries seed on the same tick rather than only the first one.
        FGothicCombatSyncMemory* TimelineMemory =
            CastInstanceNodeMemory<FGothicCombatSyncMemory>(NodeMemory);
        const bool bSeedTimeline = TimelineMemory && !TimelineMemory->bTimelineSeeded;
        const float Now = OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetTimeSeconds() : 0.f;

        for (const FGothicAbilityReadinessSync& Entry : AbilitiesToSync)
        {
            if (!Entry.AbilityTag.IsValid() || Entry.ReadyKey.IsNone())
            {
                continue;
            }

            bool bReady = false;

            // Diagnostic decomposition. bReady folds two completely different
            // causes into one bool — "the ability is already running" and "GAS
            // refused it (cooldown/cost/blocked tags)" — and a false key looks
            // identical either way from the Blackboard side. Captured
            // separately so the timeline can tell them apart. bSpecFound
            // distinguishes a third cause again: the tag matching nothing.
            bool bSpecFound            = false;
            bool bIsActive             = false;
            bool bCanActivate          = false;
            bool bCanActivateEvaluated = false;

            for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
            {
                if (!Spec.Ability || !Spec.Ability->GetAssetTags().HasTag(Entry.AbilityTag))
                {
                    continue;
                }

                // Ready means: not already running, and CanActivateAbility says
                // yes — which folds in cooldown GEs, costs, and blocked tags.
                //
                // Written as a short-circuiting if rather than the original
                // single expression ON PURPOSE: `!IsActive && CanActivate` does
                // not call CanActivateAbility while the ability is active, and
                // CanActivateAbility is overridable in Blueprint. Evaluating it
                // unconditionally to get a cleaner log line would change what
                // runs — so the skipped case is reported as skipped instead.
                bSpecFound = true;
                bIsActive  = Spec.IsActive();
                if (!bIsActive)
                {
                    bCanActivate          = Spec.Ability->CanActivateAbility(
                        Spec.Handle, ASC->AbilityActorInfo.Get());
                    bCanActivateEvaluated = true;
                }
                bReady = !bIsActive && bCanActivate;
                break;
            }

            // Read before write: this is the previous value of the key, which
            // is what makes "did this write change anything" answerable without
            // any extra per-entry state.
            const bool bPrev =
                BB->GetValue<UBlackboardKeyType_Bool>(Entry.ReadyKey.GetSelectedKeyID());

            BB->SetValue<UBlackboardKeyType_Bool>(Entry.ReadyKey.GetSelectedKeyID(), bReady);

            if (bSeedTimeline || bPrev != bReady)
            {
                UE_LOG(LogVigilCombat, Verbose,
                    TEXT("VigilTimeline|t=%.3f|%s|CombatSync|%s|%s|%d->%d|specFound=%d|isActive=%d|canActivate=%s|tag=%s"),
                    Now, *GetNameSafe(Pawn),
                    bSeedTimeline ? TEXT("SEED") : TEXT("CHANGE"),
                    *Entry.ReadyKey.SelectedKeyName.ToString(),
                    bPrev ? 1 : 0, bReady ? 1 : 0,
                    bSpecFound ? 1 : 0, bIsActive ? 1 : 0,
                    bCanActivateEvaluated ? (bCanActivate ? TEXT("1") : TEXT("0"))
                                          : TEXT("skipped-active"),
                    *Entry.AbilityTag.ToString());
            }
        }

        if (bSeedTimeline)
        {
            TimelineMemory->bTimelineSeeded = true;
        }
    }

    // ── Target-relative state ────────────────────────────────────────────────
    AActor* Target = nullptr;
    if (!TargetActorKey.IsNone())
    {
        Target = Cast<AActor>(
            BB->GetValue<UBlackboardKeyType_Object>(TargetActorKey.GetSelectedKeyID()));

        // ── Pawn → Blackboard target recovery ────────────────────────────────
        //
        // AGothicEnemyBase::SetCombatTarget writes the pawn's CombatTarget and
        // forwards to AGothicEnemyAIController::SetBlackboardTarget. That
        // forward used to early-return whenever the Blackboard did not exist
        // yet — and aggro routinely fires before the BT starts. The result was
        // a boss who knew who she was fighting (pawn side) while every BT task
        // that resolves TargetActor saw an empty key: reproduced as
        // ComputeRepositionPoint logging "TargetActorKey resolved but returned
        // no actor" on every single tick, for the whole encounter.
        //
        // The controller now caches and flushes that early call, but this is
        // the continuous net: the two sources of truth are reconciled every
        // service tick, so ANY path that sets the pawn's target without
        // reaching the Blackboard self-heals within one Interval instead of
        // failing for the rest of the fight.
        if (!Target)
        {
            if (const AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(Pawn))
            {
                if (AActor* PawnTarget = Enemy->GetCombatTarget())
                {
                    Target = PawnTarget;

                    // Route through the controller rather than writing the one
                    // key directly: bIsInCombat, bCanSeeTarget, TargetLocation,
                    // the focus lock and the stagger roll all belong to the
                    // same transition, and half a transition is its own bug.
                    if (AGothicEnemyAIController* GothicAIC = Cast<AGothicEnemyAIController>(AIC))
                    {
                        GothicAIC->SetBlackboardTarget(PawnTarget);
                    }
                    else
                    {
                        BB->SetValue<UBlackboardKeyType_Object>(
                            TargetActorKey.GetSelectedKeyID(), PawnTarget);
                    }

                    UE_LOG(LogTemp, Verbose,
                        TEXT("CombatSync[%s]: TargetActor key was empty while the pawn held combat target %s — synced"),
                        *GetNameSafe(Pawn), *PawnTarget->GetName());
                }
            }
        }
    }

    if (Target)
    {
        if (!TargetLocationKey.IsNone())
        {
            BB->SetValue<UBlackboardKeyType_Vector>(
                TargetLocationKey.GetSelectedKeyID(), Target->GetActorLocation());
        }

        if (!InAttackRangeKey.IsNone())
        {
            bool bInRange = false;
            if (const AGothicEnemyAIController* GothicAIC = Cast<AGothicEnemyAIController>(AIC))
            {
                bInRange = GothicAIC->IsTargetInAttackRange();
            }
            BB->SetValue<UBlackboardKeyType_Bool>(InAttackRangeKey.GetSelectedKeyID(), bInRange);
        }
    }
    else
    {
        // No target: range is definitively false. Distance/location are left
        // alone — stale numbers gated behind a false bIsInCombat are harmless,
        // and clearing them would fire observers for no decision-relevant reason.
        if (!InAttackRangeKey.IsNone())
        {
            BB->SetValue<UBlackboardKeyType_Bool>(InAttackRangeKey.GetSelectedKeyID(), false);
        }
    }
}

UAbilitySystemComponent* UGothicBTService_CombatSync::ResolveASC(
    UBehaviorTreeComponent& OwnerComp) const
{
    const AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* Pawn = AIC ? AIC->GetPawn() : nullptr;
    return Pawn ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn) : nullptr;
}

FString UGothicBTService_CombatSync::GetStaticDescription() const
{
    TArray<FString> Parts;
    for (const FGothicAbilityReadinessSync& Entry : AbilitiesToSync)
    {
        Parts.Add(FString::Printf(TEXT("%s → %s"),
            *Entry.AbilityTag.ToString(), *Entry.ReadyKey.SelectedKeyName.ToString()));
    }
    return FString::Printf(TEXT("%s\nSyncs: %s"),
        *Super::GetStaticDescription(),
        Parts.Num() > 0 ? *FString::Join(Parts, TEXT(", ")) : TEXT("(no abilities configured)"));
}