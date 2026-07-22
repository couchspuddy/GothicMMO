// GothicEnemyBase.cpp

#include "AI/GothicEnemyBase.h"

#include "TimerManager.h"
#include "AI/GothicEnemyAIController.h"
#include "AI/GothicMeleeHitboxComponent.h"
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

    bReplicates = true;
}

void AGothicEnemyBase::BeginPlay()
{
    Super::BeginPlay();

    InitializeGAS();

    // Attach hitbox to the correct bone now that the skeleton is loaded
    if (MeleeHitbox && GetMesh())
    {
        MeleeHitbox->AttachToComponent(
            GetMesh(),
            FAttachmentTransformRules::SnapToTargetNotIncludingScale,
            HitboxAttachBone);

        UE_LOG(LogTemp, Log, TEXT("%s: Hitbox attached to bone '%s'"),
            *GetName(), *HitboxAttachBone.ToString());
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
                if (HealthBarWidget)
                {
                    HealthBarWidget->SetVisibility(true);
                }
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

    UE_LOG(LogTemp, Log, TEXT("GothicEnemyBase: %s died — running Selah proximity check"),
        *GetName());

    // Force-disable hitbox so a dying enemy can't damage players mid-death anim
    if (MeleeHitbox)
    {
        MeleeHitbox->DisableHitbox();
    }

    AwardSelahToNearbyEmbers();
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

    UE_LOG(LogTemp, Log, TEXT("AwardSelahToNearbyEmbers: Checking %.0f unit radius around %s"),
        SelahAwardRadius, *GetName());

    TArray<AActor*> NearbyActors;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(),
        AGothicPlayerCharacter::StaticClass(),
        NearbyActors);

    int32 PlayersAwarded = 0;

    for (AActor* Actor : NearbyActors)
    {
        float Distance = FVector::Dist(GetActorLocation(), Actor->GetActorLocation());

        UE_LOG(LogTemp, Log, TEXT("AwardSelahToNearbyEmbers: Player %s is %.0f units away"),
            *Actor->GetName(), Distance);

        if (Distance > SelahAwardRadius)
        {
            UE_LOG(LogTemp, Log, TEXT("AwardSelahToNearbyEmbers: Too far — no Selah"));
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

            UE_LOG(LogTemp, Log, TEXT("AwardSelahToNearbyEmbers: Awarded %.1f Selah to %s"),
                SelahAwardAmount, *Actor->GetName());

            PlayersAwarded++;

            AGothicPlayerCharacter* PlayerChar = Cast<AGothicPlayerCharacter>(Actor);
            if (PlayerChar)
            {
                PlayerChar->TriggerSelahMoment();
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("AwardSelahToNearbyEmbers: Awarded Selah to %d players"),
        PlayersAwarded);
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
        UE_LOG(LogTemp, Log, TEXT("%s: Loot roll — nothing dropped"), *GetName());
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
        UE_LOG(LogTemp, Log, TEXT("%s: Spawned loot pickup — %s"),
            *GetName(),
            RolledItem.Definition ? *RolledItem.Definition->ItemID.ToString() : TEXT("Unknown"));
    }
}

void AGothicEnemyBase::MulticastOnHit_Implementation(
    FVector HitLocation, bool bVitalHit, float DamageAmount)
{
    // This runs on every client (and the server). Drive visual/audio feedback here.
    // The anim instance's OnHitFeedback is the intended consumer — if it's wired up,
    // forward to it. Otherwise log so it's visible during testing.

    UE_LOG(LogTemp, Log, TEXT("%s: MulticastOnHit | Loc=%s | Vital=%d | Dmg=%.1f"),
        *GetName(), *HitLocation.ToString(), bVitalHit ? 1 : 0, DamageAmount);
}