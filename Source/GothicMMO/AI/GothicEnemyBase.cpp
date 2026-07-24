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

}