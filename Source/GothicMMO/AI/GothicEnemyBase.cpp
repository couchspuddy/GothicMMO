// GothicEnemyBase.cpp

#include "AI/GothicEnemyBase.h"

#include "GothicMMO.h"                          // LogVigilCombat
#include "TimerManager.h"
#include "AI/GothicEnemyAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AI/GothicMeleeHitboxComponent.h"
#include "AI/GothicVitalPointComponent.h"
#include "AI/GothicPackSubsystem.h"
#include "UI/GothicHUD.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Components/WidgetComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Character/GothicPlayerCharacter.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AI/GothicEnemyAnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/GothicItemDefinition.h"
#include "Items/GothicLootTable.h"
#include "Items/GothicWorldPickup.h"

AGothicEnemyBase::AGothicEnemyBase()
{
    AbilitySystemComponent = CreateDefaultSubobject<UGothicAbilitySystemComponent>(
        TEXT("AbilitySystemComponent"));
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

    AttributeSet = CreateDefaultSubobject<UGothicAttributeSet>(TEXT("AttributeSet"));

    PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

    UAISenseConfig_Sight* SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    SightConfig->SightRadius                = 1500.f;
    SightConfig->LoseSightRadius            = 2000.f;
    SightConfig->PeripheralVisionAngleDegrees = 90.f;
    SightConfig->SetMaxAge(5.f);
    SightConfig->DetectionByAffiliation.bDetectEnemies    = true;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
    SightConfig->DetectionByAffiliation.bDetectNeutrals   = false;
    PerceptionComponent->ConfigureSense(*SightConfig);

    UAISenseConfig_Hearing* HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
    HearingConfig->HearingRange = 800.f;
    PerceptionComponent->ConfigureSense(*HearingConfig);

    PerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());

    HealthBarWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarWidget"));
    HealthBarWidget->SetupAttachment(RootComponent);
    HealthBarWidget->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
    HealthBarWidget->SetWidgetSpace(EWidgetSpace::World);
    HealthBarWidget->SetDrawSize(FVector2D(200.f, 20.f));
    HealthBarWidget->SetVisibility(false);

    // Melee hitbox — created here, attached to bone in BeginPlay
    // (skeleton isn't available yet in the constructor)
    MeleeHitbox = CreateDefaultSubobject<UGothicMeleeHitboxComponent>(TEXT("MeleeHitbox"));
    MeleeHitbox->SetupAttachment(GetMesh());

    // Vital point system — lives on the base so every enemy has vitals + the
    // amber overlay, not just the one Blueprint that happened to add it by hand.
    // Bone locations and the overlay material stay per-Blueprint data (the
    // component is rig-agnostic), configured on the enemy Blueprints.
    VitalPointComponent = CreateDefaultSubobject<UGothicVitalPointComponent>(TEXT("VitalPoint"));

    bReplicates = true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Accursed names
//
// Eagle's Landing is Philadelphia as it was around the turn of the century, so
// the pool is drawn from what that city's rolls actually looked like: old
// Anglo-Dutch family names alongside the Irish, German and Italian surnames of
// the wards that did the work. The point of the Selah moment is that these were
// people, and "Thrall 14" is not a person.
//
// Kept as file-static tables rather than a DataTable on purpose. A DataTable
// would be the tidier answer if designers needed to curate these, but nothing
// about the list is tuning — it never wants balancing, only more entries — and
// an asset means one more thing to cook, load and forget to reference.
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
    const TCHAR* const GAccursedFirstNames[] = {
        TEXT("Albert"),   TEXT("Clarence"), TEXT("Ernest"),   TEXT("Horace"),
        TEXT("Walter"),   TEXT("Harold"),   TEXT("Chester"),  TEXT("Silas"),
        TEXT("Leland"),   TEXT("Rufus"),    TEXT("Everett"),  TEXT("Alonzo"),
        TEXT("Virgil"),   TEXT("Emmett"),   TEXT("Lyman"),    TEXT("Ambrose"),
        TEXT("Thaddeus"), TEXT("Cornelius"),TEXT("Otto"),     TEXT("August"),
        TEXT("Adelaide"), TEXT("Cordelia"), TEXT("Henrietta"),TEXT("Prudence"),
        TEXT("Millicent"),TEXT("Augusta"),  TEXT("Beatrix"),  TEXT("Clementine"),
        TEXT("Lavinia"),  TEXT("Rosalind"), TEXT("Theodora"), TEXT("Winifred"),
        TEXT("Hester"),   TEXT("Euphemia"), TEXT("Ottilie"),  TEXT("Maud"),
        TEXT("Bridget"),  TEXT("Etta"),     TEXT("Lottie"),   TEXT("Nell"),
    };

    const TCHAR* const GAccursedSurnames[] = {
        TEXT("Rittenhouse"), TEXT("Lippincott"), TEXT("Pemberton"), TEXT("Wistar"),
        TEXT("Shippen"),     TEXT("Ridgway"),    TEXT("Norris"),    TEXT("Biddle"),
        TEXT("Chew"),        TEXT("Peale"),      TEXT("Bingham"),   TEXT("Meade"),
        TEXT("Gallagher"),   TEXT("Devlin"),     TEXT("Quigley"),   TEXT("Hanrahan"),
        TEXT("Mullen"),      TEXT("Tobin"),      TEXT("Boyle"),     TEXT("Fagan"),
        TEXT("Keeler"),      TEXT("Sheridan"),   TEXT("Doyle"),     TEXT("Rafferty"),
        TEXT("Vogel"),       TEXT("Weimer"),     TEXT("Zeller"),    TEXT("Kraus"),
        TEXT("Brauer"),      TEXT("Hesse"),      TEXT("Marchetti"), TEXT("Ferrara"),
        TEXT("Costa"),       TEXT("Lombardo"),   TEXT("Abernathy"), TEXT("Crowder"),
        TEXT("Ashcombe"),    TEXT("Thorne"),     TEXT("Waverly"),   TEXT("Selby"),
    };
}

FText AGothicEnemyBase::MakeAccursedName(int32 Seed)
{
    const int32 FirstCount = UE_ARRAY_COUNT(GAccursedFirstNames);
    const int32 LastCount  = UE_ARRAY_COUNT(GAccursedSurnames);

    // FRandomStream rather than FMath::Rand: seeded, reproducible, and it does
    // not disturb the global stream that gameplay randomness draws from.
    FRandomStream Stream(Seed);
    const int32 FirstIdx = Stream.RandHelper(FirstCount);
    const int32 LastIdx  = Stream.RandHelper(LastCount);

    return FText::FromString(FString::Printf(TEXT("%s %s"),
        GAccursedFirstNames[FirstIdx], GAccursedSurnames[LastIdx]));
}

void AGothicEnemyBase::BeginPlay()
{
    Super::BeginPlay();

    // Name the nameless. Seeded from the actor's own name so the result is stable
    // for a given Accursed: placed enemies keep the same name across runs, and
    // server and client agree without this needing to replicate.
    //
    // Read the flag off the CLASS DEFAULT, not off this instance. bGenerateAccursedName
    // is EditDefaultsOnly — a decision about a kind of Accursed, not about one of
    // them — and a placed actor serialises whatever the default was on the day it
    // was dropped in the level, then keeps it forever. Clearing the flag on
    // BP_Enemy_BestialLucid did not reach the boss already standing in Eagle's
    // Landing: she was placed while the default was true, so she generated a name
    // and overwrote her own. Asking the class sidesteps every stale instance.
    const AGothicEnemyBase* ClassDefaults = GetClass()
        ? GetClass()->GetDefaultObject<AGothicEnemyBase>() : nullptr;
    const bool bMayGenerate = ClassDefaults ? ClassDefaults->bGenerateAccursedName
                                            : bGenerateAccursedName;

    if (bMayGenerate && AccursedName.IsEmpty())
    {
        AccursedName = MakeAccursedName(GetTypeHash(GetFName()));
    }

    InitializeGAS();

    // Make State.Stunned actually stop this enemy. See HandleStunTagChanged.
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->RegisterGameplayTagEvent(
            FGameplayTag::RequestGameplayTag(FName("State.Stunned")),
            EGameplayTagEventType::NewOrRemoved)
            .AddUObject(this, &AGothicEnemyBase::HandleStunTagChanged);
    }

    // Face the target, turn smoothly. Set in BeginPlay rather than the
    // constructor so a Blueprint's serialized bOrientRotationToMovement can't
    // silently win (the collision-override gotcha, same shape).
    //
    // The old behavior — orient to movement — is exactly why the boss "turned
    // its back": every strafe or reposition rotated her to face where she was
    // walking. With bUseControllerDesiredRotation the pawn instead rotates toward
    // the AI controller's desired rotation, and the controller keeps its focus on
    // the combat target (see AGothicEnemyAIController::SetBlackboardTarget). So
    // she strafes and repositions while continuously facing the player, turning
    // at RotationRate rather than snapping — a boss that tracks you, not one that
    // wanders. Flankable by design: the turn rate is finite, so getting behind
    // her is real counterplay.
    if (UCharacterMovementComponent* Move = GetCharacterMovement())
    {
        Move->bOrientRotationToMovement    = false;
        Move->bUseControllerDesiredRotation = true;
        Move->RotationRate = FRotator(0.f, TurnRateDegrees, 0.f);
    }
    bUseControllerRotationYaw   = false; // desired-rotation path handles yaw smoothly
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll  = false;

    // Attach hitbox to the correct bone now that the skeleton is loaded
    if (MeleeHitbox && GetMesh())
    {
        MeleeHitbox->AttachToComponent(
            GetMesh(),
            FAttachmentTransformRules::SnapToTargetNotIncludingScale,
            HitboxAttachBone);

    }

    if (PerceptionComponent)
    {
        PerceptionComponent->OnPerceptionUpdated.AddDynamic(
            this, &AGothicEnemyBase::OnPerceptionUpdated);
    }

    // Level-placed enemies carry a serialized PackID and never pass through
    // SetPackID, so register them here. Spawned enemies are stamped after spawn
    // via SetPackID, which registers on its own — RegisterMember is idempotent,
    // so an enemy that takes both paths is added once.
    if (HasAuthority() && !PackID.IsNone())
    {
        if (UGothicPackSubsystem* Packs = GetWorld() ? GetWorld()->GetSubsystem<UGothicPackSubsystem>() : nullptr)
        {
            Packs->RegisterMember(PackID, this);
        }
    }

    // A Health attribute-change delegate was registered here with an empty lambda
    // body ("Blueprint widget can poll this"). It cost a delegate binding and a
    // callback on every damage instance to do nothing, and it read as if health
    // changes were being handled. The health bar is driven from
    // MulticastOnHit -> RegisterEnemyHealthBar; nothing needed this. Removed
    // rather than filled in — there is no second consumer to write for.
}

void AGothicEnemyBase::OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors)
{
    for (AActor* Actor : UpdatedActors)
    {
        FActorPerceptionBlueprintInfo PerceptionInfo;
        if (PerceptionComponent->GetActorsPerception(Actor, PerceptionInfo))
        {
            bool bSensed = false;
            for (const FAIStimulus& Stimulus : PerceptionInfo.LastSensedStimuli)
            {
                if (Stimulus.WasSuccessfullySensed())
                {
                    bSensed = true;
                    break;
                }
            }

            if (bSensed)
            {
                // Re-sensing the current target cancels a forget already counting
                // down — stepping back out from behind a pillar must not cost the
                // engagement.
                if (Actor == CombatTarget)
                {
                    GetWorldTimerManager().ClearTimer(ForgetTargetTimer);
                }

                // Health bars are now the HUD's canvas system (screen-projected,
                // player-facing), driven by RegisterEnemyHealthBar on hit — not
                // the old world-space HealthBarWidget, which never faced the
                // camera and whose widget was never bound to this enemy's health.
                SetCombatTarget(Actor);
                break;
            }

            // Stimulus expired (SetMaxAge 5) or the actor left LoseSightRadius.
            // Before this, OnPerceptionUpdated ONLY ever set a target — there was
            // no lost-perception path at all, and the leash timer was the single
            // route out of combat in the whole AI stack.
            if (Actor == CombatTarget)
            {
                HandleTargetPerceptionLost();
            }
        }
    }
}

void AGothicEnemyBase::HandleTargetPerceptionLost()
{
    if (!bForgetTargetOnPerceptionLoss || !HasAuthority())
    {
        return;
    }

    // Already counting down — do not restart the clock on every expiring stimulus,
    // or an enemy that keeps half-sensing the player never reaches the deadline.
    if (GetWorldTimerManager().IsTimerActive(ForgetTargetTimer))
    {
        return;
    }

    GetWorldTimerManager().SetTimer(
        ForgetTargetTimer,
        this,
        &AGothicEnemyBase::ForgetTargetIfStillUnseen,
        FMath::Max(0.1f, TargetForgetSeconds),
        false);
}

void AGothicEnemyBase::ForgetTargetIfStillUnseen()
{
    if (!CombatTarget)
    {
        return;
    }

    // Re-ask perception rather than trusting the timer to have been cancelled.
    // The cancel above only fires on an OnPerceptionUpdated that names THIS actor;
    // asking directly is the answer that cannot go stale.
    FActorPerceptionBlueprintInfo Info;
    if (PerceptionComponent && PerceptionComponent->GetActorsPerception(CombatTarget, Info))
    {
        for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
        {
            if (Stimulus.WasSuccessfullySensed())
            {
                return;
            }
        }
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("GothicEnemyBase[%s]: target %s unseen for %.1fs — forgetting"),
        *GetName(), *GetNameSafe(CombatTarget), TargetForgetSeconds);

    if (AGothicEnemyAIController* GothicAIC = Cast<AGothicEnemyAIController>(GetController()))
    {
        GothicAIC->ClearCombatTarget();
    }
    else
    {
        ClearCombatTarget();
    }
}

void AGothicEnemyBase::SetPackID(FName NewPackID)
{
    if (PackID == NewPackID)
    {
        return;
    }

    // Registration is server-side state; clients never need the pack map.
    if (!HasAuthority())
    {
        PackID = NewPackID;
        return;
    }

    UGothicPackSubsystem* Packs = GetWorld() ? GetWorld()->GetSubsystem<UGothicPackSubsystem>() : nullptr;

    if (Packs && !PackID.IsNone())
    {
        Packs->UnregisterMember(PackID, this);
    }

    PackID = NewPackID;

    if (Packs && !PackID.IsNone())
    {
        Packs->RegisterMember(PackID, this);
    }
}

void AGothicEnemyBase::SetCombatTarget(AActor* NewTarget)
{
    AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(GetController());

    // Refuse aggro while leashing home, and refuse it BEFORE the latch is set —
    // writing CombatTarget and relying on the controller to ignore it is not
    // enough, because CombatSync would read the latch back out and re-aggro the
    // moment the leash suppression lifts.
    //
    // Perception is the case that matters: OnPerceptionUpdated fires every time
    // the player is sensed, so a boss walking home in plain sight would otherwise
    // re-target on the next perception update and never reach her anchor.
    if (AIC && !AIC->IsAcceptingCombatTargets())
    {
        return;
    }

    CombatTarget = NewTarget;

    if (AIC)
    {
        AIC->SetBlackboardTarget(NewTarget);
    }
}

void AGothicEnemyBase::NotifyDamagedBy(AActor* DamageInstigator)
{
    // Aggro is a server decision, like every other write to CombatTarget.
    if (!HasAuthority() || !DamageInstigator || DamageInstigator == this)
    {
        return;
    }

    // Health has already been written by the time this is called, so a lethal
    // hit reads as dead here — a corpse must not acquire a target on the way
    // down, or OnDeath's roster bookkeeping runs against a live-looking pawn.
    if (!IsAlive())
    {
        return;
    }

    AGothicCharacterBase* InstigatorChar = Cast<AGothicCharacterBase>(DamageInstigator);
    if (!InstigatorChar || !InstigatorChar->IsAlive())
    {
        return;
    }

    // Friendly fire. The check is deliberately "another Accursed on my own team"
    // rather than a plain TeamId comparison against the instigator: TeamId is
    // EditDefaultsOnly and defaults to 0 on AGothicCharacterBase, so nothing in
    // C++ distinguishes the player (0) from an enemy (0), and a bare
    // "same TeamId -> ignore" test would have silently refused EVERY retaliation
    // — the exact bug this function exists to fix, reintroduced by its own guard.
    // Narrowing to enemy-on-enemy keeps TeamId meaningful where it is actually
    // differentiated (between AI factions) without depending on Blueprint
    // defaults this code cannot see.
    if (const AGothicEnemyBase* InstigatorEnemy = Cast<AGothicEnemyBase>(InstigatorChar))
    {
        if (InstigatorEnemy->TeamId == TeamId)
        {
            return;
        }
    }

    // Don't thrash an engagement that is already running — see
    // bRetaliationOverridesCurrentTarget. A target that has stopped being fightable
    // since it was set — died, was destroyed, or went DOWNED — is treated as no
    // target at all, so retaliation is free to move onto whoever is still standing.
    //
    // Through the shared predicate, not a local copy of it. This line used to read
    // "!CurrentChar || CurrentChar->IsAlive()", a second spelling of the AI
    // controller's IsFightableTarget; teaching one about downed and not the other
    // would have had the leash tick release a downed player and this function hand
    // them straight back on the next tick of damage.
    if (!bRetaliationOverridesCurrentTarget)
    {
        if (AActor* Current = GetCombatTarget())
        {
            const bool bCurrentStillFightable = AGothicCharacterBase::IsFightableActor(Current);
            if (Current != DamageInstigator && bCurrentStillFightable)
            {
                // The other half of the drop story. DropTargetIfNoLongerFightable
                // says when an enemy let a target go; this says when it refused to
                // take a new one, which is the answer to "I shot it and it ignored
                // me" — and the line that distinguishes a refusal from damage that
                // never arrived at all.
                //
                // Edge-triggered on the attacker, like the leash's return latch:
                // an unbroken burst from the same shooter prints once. It re-arms
                // the moment the attacker changes or a target is actually latched,
                // so the next genuine refusal is not swallowed. Without this a
                // rapid-fire weapon writes a line per round.
                if (LastRetaliationRejectAttacker.Get() != DamageInstigator)
                {
                    LastRetaliationRejectAttacker = DamageInstigator;

                    const UWorld* World = GetWorld();
                    UE_LOG(LogVigilCombat, Verbose,
                        TEXT("VigilTimeline|t=%.3f|%s|RetaliationReject|attacker=%s|")
                        TEXT("reason=holding-fightable-target|current=%s"),
                        World ? World->GetTimeSeconds() : 0.f, *GetNameSafe(this),
                        *GetNameSafe(DamageInstigator), *GetNameSafe(Current));
                }

                return;
            }
        }
    }

    // Re-arm the reject latch: this attacker was accepted, so the next refusal of
    // them is news again.
    LastRetaliationRejectAttacker = nullptr;

    // Through SetCombatTarget, never around it: that is the choke point that
    // consults IsAcceptingCombatTargets, and bypassing it here would break the
    // leash's no-re-aggro-during-return property for every damage source at once.
    SetCombatTarget(DamageInstigator);
}

void AGothicEnemyBase::OnDeath_Implementation(AActor* Killer)
{
    // Re-entry guard. Super also early-outs on State.Dead, but it returns void —
    // so without this check the rest of this function still ran on a second call,
    // dropping loot twice and (now) decrementing the encounter roster twice. Two
    // damage instances landing in the same frame is enough to trigger it.
    if (AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Dead"))))
    {
        return;
    }

    Super::OnDeath_Implementation(Killer);


    // Force-disable hitbox so a dying enemy can't damage players mid-death anim
    if (MeleeHitbox)
    {
        MeleeHitbox->DisableHitbox();
    }

    // Selah is NOT awarded per-kill. It belongs entirely to the encounter system:
    // clearing an AGothicEncounterVolume raises the on-screen prompt, and the
    // player's hold-to-collect fires the Selah moment (name cycle → award) via
    // CompleteCollection. A per-kill award here both double-paid encounter enemies
    // and auto-triggered the moment on any nearby kill — the exact bug reported.
    // (This AwardSelahToNearbyEmbers path was deleted once before for the same
    // reason; left the function in place but no longer called.)
    SpawnLootDrop();

    if (HealthBarWidget)
    {
        HealthBarWidget->SetVisibility(false);
    }

    if (GetController())
    {
        GetController()->StopMovement();
    }

    // Kill the attack that was already in flight, or a corpse keeps swinging.
    //
    // StopMovement above only stops the pawn WALKING. An attack ability activated a
    // frame before death carries on: its montage plays out, and the behaviour tree —
    // still ticking — is free to start another one. That is the "dead enemy keeps
    // playing attack montages" case, and it is intermittent precisely because it
    // depends on dying inside that window.
    //
    // All three are needed. Cancelling abilities alone leaves the montage running,
    // stopping montages alone lets the tree start a fresh attack, and stopping the
    // tree alone leaves the current ability mid-swing.
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->CancelAllAbilities();
    }

    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        if (UAnimInstance* Anim = MeshComp->GetAnimInstance())
        {
            // Short blend rather than 0: a hard cut out of a swing reads as a
            // glitch, and the death state is entered from bIsDead in the ABP, so
            // stopping montages here cannot interrupt the death animation itself.
            Anim->StopAllMontages(0.15f);
        }
    }

    // Drop the combat latch on the way down.
    //
    // Nothing but AGothicEnemyAIController::BreakLeash ever called
    // ClearCombatTarget, so a corpse kept pointing at whoever it had been fighting
    // for its whole CorpseLifetime — and CombatSync, which reconciles the pawn
    // latch back into the Blackboard at 5Hz, was free to re-publish that target
    // into a tree that had not stopped ticking yet. Routed through the controller
    // so BOTH halves go (pawn latch, Blackboard keys, focus lock, walk speed);
    // the pawn-only fallback covers an enemy that died unpossessed.
    if (AGothicEnemyAIController* GothicAIC = Cast<AGothicEnemyAIController>(GetController()))
    {
        GothicAIC->ClearCombatTarget();
    }
    else
    {
        ClearCombatTarget();
    }

    // Kill the vital shimmer and the amber overlay glow, and stop the shift timer.
    //
    // UGothicVitalPointComponent::HandleOwnerDeath had ZERO callers. Its own header
    // says "call from PlayDeathCosmetics" — a function that does not exist in this
    // project — so corpses kept glowing and kept shifting their vital point for the
    // full CorpseLifetime. This is the death path that does exist.
    //
    // Cosmetic-only work, but called unconditionally: the component itself already
    // decides what it owns per net mode (a dedicated server never spawned a
    // shimmer, and the timer it clears is server-side).
    if (VitalPointComponent)
    {
        VitalPointComponent->HandleOwnerDeath();
    }

    if (AAIController* AIC = Cast<AAIController>(GetController()))
    {
        if (UBrainComponent* Brain = AIC->GetBrainComponent())
        {
            Brain->StopLogic(TEXT("Died"));
        }
    }

    FTimerHandle CorpseTimer;
    GetWorldTimerManager().SetTimer(
        CorpseTimer,
        this,
        &AGothicEnemyBase::DestroyCorpse,
        CorpseLifetime,
        false);

    // Roster notification — the encounter volume decrements RemainingEnemyCount
    // here and raises the Selah prompt when the last member falls. This is the
    // only broadcast site: without it HandleEnemyDied never ran, so the count
    // never reached zero and the entire prompt → collect → award chain was dead.
    //
    // Server-only: the encounter is server-authoritative and its handler early-outs
    // on clients anyway. The State.Dead guard at the top of this function keeps it
    // to one broadcast per enemy.
    if (HasAuthority())
    {
        OnEnemyDied.Broadcast(this);

        // Pack reaction — survivors in the same PackID pause and play the guard
        // pose. Like OnEnemyDied above, the subsystem documented this call site
        // but nothing ever made the call, so no pack ever regrouped.
        if (!PackID.IsNone())
        {
            if (UGothicPackSubsystem* Packs = GetWorld() ? GetWorld()->GetSubsystem<UGothicPackSubsystem>() : nullptr)
            {
                Packs->NotifyMemberDeath(PackID, this);
            }
        }
    }
}

void AGothicEnemyBase::DestroyCorpse()
{
    Destroy();
}

void AGothicEnemyBase::SpawnLootDrop()
{
    if (!HasAuthority() || !LootTable)
    {
        return;
    }

    FGothicItemInstance RolledItem;
    if (!LootTable->RollDrop(RolledItem))
    {
        return;
    }

    // Spawn the pickup slightly above the corpse so it doesn't clip into the floor
    FVector SpawnLocation = GetActorLocation() + FVector(0.f, 0.f, 50.f);
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AGothicWorldPickup* Pickup = GetWorld()->SpawnActor<AGothicWorldPickup>(
        AGothicWorldPickup::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParams);

    if (Pickup)
    {
        Pickup->InitializePickup(RolledItem);
    }
}

void AGothicEnemyBase::MulticastOnHit_Implementation(
    FVector HitLocation, bool bVitalHit, float DamageAmount, AActor* DamageInstigator)
{
    // This runs on every client (and the server). Drive visual/audio feedback here.

    // The hit react. PlayHitReact does the direction math and picks the matching
    // directional montage, so every enemy sharing UGothicEnemyAnimInstance reacts
    // without per-Blueprint wiring. Nothing called this before, which is why the
    // montages on ABP_Enemy never played no matter what was assigned to them.
    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        if (UGothicEnemyAnimInstance* EnemyAnim =
                Cast<UGothicEnemyAnimInstance>(MeshComp->GetAnimInstance()))
        {
            EnemyAnim->PlayHitReact(HitLocation);
        }
    }

    // Floating damage number + the canvas health bar. Both run per-client, but they
    // are NOT scoped the same way, and sharing one GetFirstPlayerController() lookup
    // conflated them: on a listen server player 0 is the host, so a remote player's
    // hits painted their damage numbers onto the host's screen.
    //
    // The health bar is VIEWER-scoped — anyone watching this enemy get hit has
    // earned the bar — so every local controller registers it. The damage number is
    // SHOOTER-scoped: only the local player who actually dealt the hit gets it.
    //
    // The canvas health bar is screen-projected, so it is always player-facing by
    // construction, unlike the old world-space HealthBarWidget component.
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC || !PC->IsLocalController())
        {
            continue;
        }

        AGothicHUD* HUD = Cast<AGothicHUD>(PC->GetHUD());
        if (!HUD)
        {
            continue;
        }

        HUD->RegisterEnemyHealthBar(this);

        // Compare against the pawn and the controller both — melee and ability
        // damage credit the avatar pawn, but a controller-owned source would
        // otherwise silently lose its number.
        if (DamageInstigator && (DamageInstigator == PC->GetPawn() || DamageInstigator == PC))
        {
            HUD->ShowDamageNumber(HitLocation, DamageAmount, bVitalHit);
        }
    }
}

void AGothicEnemyBase::HandleStunTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
    if (!HasAuthority())
    {
        return;
    }

    AAIController* AIC = Cast<AAIController>(GetController());
    const bool bStunned = NewCount > 0;

    if (UCharacterMovementComponent* Move = GetCharacterMovement())
    {
        // StopMovementImmediately alone is not enough -- path following would
        // simply re-issue velocity on the next tick, so the brain has to stop too.
        Move->StopMovementImmediately();
        Move->SetMovementMode(bStunned ? MOVE_None : MOVE_Walking);
    }

    if (AIC)
    {
        if (bStunned)
        {
            AIC->StopMovement();
        }

        if (UBrainComponent* Brain = AIC->GetBrainComponent())
        {
            // Named reason so a stun cannot be resumed by some other system's
            // RestartLogic, and so the pause is legible in the AI debugger.
            if (bStunned)
            {
                Brain->PauseLogic(TEXT("Stunned"));
            }
            else
            {
                Brain->ResumeLogic(TEXT("Stunned"));
            }
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("Stun[%s]: %s"),
        *GetName(), bStunned ? TEXT("halted") : TEXT("resumed"));
}
