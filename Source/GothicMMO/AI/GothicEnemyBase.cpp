// GothicEnemyBase.cpp

#include "AI/GothicEnemyBase.h"

#include "TimerManager.h"
#include "AI/GothicEnemyAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AI/GothicMeleeHitboxComponent.h"
#include "AI/GothicVitalPointComponent.h"
#include "AI/GothicPackSubsystem.h"
#include "UI/GothicHUD.h"
#include "GameFramework/PlayerController.h"
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

    if (AbilitySystemComponent && AttributeSet)
    {
        AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetHealthAttribute()).AddLambda(
            [this](const FOnAttributeChangeData& Data)
            {
                // Health changed — Blueprint widget can poll this
            });
    }
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
                // Health bars are now the HUD's canvas system (screen-projected,
                // player-facing), driven by RegisterEnemyHealthBar on hit — not
                // the old world-space HealthBarWidget, which never faced the
                // camera and whose widget was never bound to this enemy's health.
                SetCombatTarget(Actor);
                break;
            }
        }
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
    CombatTarget = NewTarget;

    if (AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(GetController()))
    {
        AIC->SetBlackboardTarget(NewTarget);
    }
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

    // Vital-point cosmetic teardown — shimmer, mesh overlay, and shift timer.
    // The component documented this call from a PlayDeathCosmetics path that
    // was never built, so nothing ever made it and corpses kept glowing for
    // the full CorpseLifetime.
    if (VitalPointComponent)
    {
        VitalPointComponent->HandleOwnerDeath();
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
    FVector HitLocation, bool bVitalHit, float DamageAmount)
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

    // Floating damage number + the canvas health bar. Both run per-client, so the
    // local player's own HUD is the right target. ShowDamageNumber and
    // RegisterEnemyHealthBar both existed with zero callers project-wide — the
    // reason no number and no working health bar ever appeared. The canvas health
    // bar is screen-projected, so it is always player-facing by construction,
    // unlike the old world-space HealthBarWidget component.
    if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
    {
        if (AGothicHUD* HUD = Cast<AGothicHUD>(PC->GetHUD()))
        {
            HUD->ShowDamageNumber(HitLocation, DamageAmount, bVitalHit);
            HUD->RegisterEnemyHealthBar(this);
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
