// GothicEnemyBase.cpp

#include "AI/GothicEnemyBase.h"

#include "TimerManager.h"
#include "AI/GothicEnemyAIController.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

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

    bReplicates = true;
}

void AGothicEnemyBase::BeginPlay()
{
    Super::BeginPlay();

    InitializeGAS();

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
        // UE5.8: GetCurrentStimulus was removed. Use GetActorsPerception instead.
        FActorPerceptionBlueprintInfo PerceptionInfo;
        if (PerceptionComponent->GetActorsPerception(Actor, PerceptionInfo))
        {
            // Check if any stimulus was successfully sensed
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

    // Notify the AI controller to update the Blackboard
    if (AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(GetController()))
    {
        AIC->SetBlackboardTarget(NewTarget);
    }
}

void AGothicEnemyBase::OnDeath_Implementation(AActor* Killer)
{
    // Don't call Super — we handle everything here for enemies
    // Super disables all collision which breaks ragdoll

    // Apply dead tag and cancel abilities
    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->AddLooseGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Dead")));
        AbilitySystemComponent->CancelAllAbilities();
    }

    // Hide health bar
    if (HealthBarWidget)
    {
        HealthBarWidget->SetVisibility(false);
    }

    // Stop AI movement
    if (GetController())
    {
        GetController()->StopMovement();
    }

    // Disable capsule collision only
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);

    // Stop character movement
    GetCharacterMovement()->DisableMovement();
    GetCharacterMovement()->StopMovementImmediately();

    // Detach mesh from capsule so it can fall freely
    GetMesh()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

    // Set mesh collision to ragdoll profile BEFORE enabling physics
    GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetMesh()->SetCollisionObjectType(ECC_PhysicsBody);

    // Enable physics simulation
    GetMesh()->SetSimulatePhysics(true);

    // Apply impulse after a tiny delay to let physics initialize
    FTimerHandle ImpulseTimer;
    GetWorldTimerManager().SetTimer(ImpulseTimer, [this]()
    {
        if (GetMesh())
        {
            const FVector ImpulseDir = GetActorForwardVector() * -1.f;
            GetMesh()->AddImpulse(ImpulseDir * 30000.f, NAME_None, false);
        }
    }, 0.1f, false);

    // Schedule corpse cleanup
    FTimerHandle CorpseTimer;
    GetWorldTimerManager().SetTimer(
        CorpseTimer,
        this,
        &AGothicEnemyBase::DestroyCorpse,
        CorpseLifetime,
        false);

    UE_LOG(LogTemp, Log, TEXT("%s died."), *GetName());
}

void AGothicEnemyBase::DestroyCorpse()
{
    Destroy();
}
