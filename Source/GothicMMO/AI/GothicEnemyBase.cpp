// GothicEnemyBase.cpp

#include "AI/GothicEnemyBase.h"

#include "TimerManager.h"
#include "AI/GothicEnemyAIController.h"
#include "AI/GothicMeleeHitboxComponent.h"
#include "AI/GothicVitalPointComponent.h"
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

void AGothicEnemyBase::BeginPlay()
{
    Super::BeginPlay();

    InitializeGAS();

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

    FTimerHandle CorpseTimer;
    GetWorldTimerManager().SetTimer(
        CorpseTimer,
        this,
        &AGothicEnemyBase::DestroyCorpse,
        CorpseLifetime,
        false);
}

void AGothicEnemyBase::AwardSelahToNearbyEmbers()
{
    if (!GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("AwardSelahToNearbyEmbers: No world"));
        return;
    }


    TArray<AActor*> NearbyActors;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(),
        AGothicPlayerCharacter::StaticClass(),
        NearbyActors);

    int32 PlayersAwarded = 0;

    for (AActor* Actor : NearbyActors)
    {
        float Distance = FVector::Dist(GetActorLocation(), Actor->GetActorLocation());


        if (Distance > SelahAwardRadius)
        {
            continue;
        }

        UGothicAbilitySystemComponent* PlayerASC =
            Cast<UGothicAbilitySystemComponent>(
                UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor));

        if (!PlayerASC)
        {
            UE_LOG(LogTemp, Warning, TEXT("AwardSelahToNearbyEmbers: No ASC on player %s"),
                *Actor->GetName());
            continue;
        }

        if (!SelahGainEffect)
        {
            UE_LOG(LogTemp, Warning, TEXT("AwardSelahToNearbyEmbers: SelahGainEffect not assigned on %s"),
                *GetName());
            continue;
        }

        FGameplayEffectContextHandle Context = PlayerASC->MakeEffectContext();
        Context.AddSourceObject(this);

        FGameplayEffectSpecHandle Spec = PlayerASC->MakeOutgoingSpec(
            SelahGainEffect, 1.f, Context);

        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(
                FGameplayTag::RequestGameplayTag(FName("Data.Selah")),
                SelahAwardAmount);

            PlayerASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());


            PlayersAwarded++;

            AGothicPlayerCharacter* PlayerChar = Cast<AGothicPlayerCharacter>(Actor);
            if (PlayerChar)
            {
                PlayerChar->TriggerSelahMoment();
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