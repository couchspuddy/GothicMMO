// GothicEnemyBase.cpp

#include "AI/GothicEnemyBase.h"
#include "GothicMMO.h"                          // ECC_Weapon
#include "AI/GothicCombatStateComponent.h"
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
#include "Kismet/GameplayStatics.h"
#include "Character/GothicPlayerCharacter.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "AI/GothicVitalPointComponent.h"
#include "Components/CapsuleComponent.h"
#include "UI/GothicEnemyHealthBarWidget.h"
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
    HealthBarWidget->SetWidgetSpace(EWidgetSpace::Screen);  
    HealthBarWidget->SetDrawSize(FVector2D(200.f, 20.f));
    HealthBarWidget->SetVisibility(false);
    
    VitalPointComponent = CreateDefaultSubobject<UGothicVitalPointComponent>(
    TEXT("VitalPointComponent"));
    CombatStateComponent = CreateDefaultSubobject<UGothicCombatStateComponent>(TEXT("CombatStateComponent"));
    bReplicates = true;
}

void AGothicEnemyBase::BeginPlay()
{
    Super::BeginPlay();

    // ECC_Weapon defaults to ECR_Ignore (DefaultEngine.ini) — every enemy mesh is
    // transparent to hitscan unless something blocks it. Until now the only place
    // that did was PlayDeathCosmetics, so a living enemy could not be shot at all;
    // BP_Enemy_Draugr only worked because its Blueprint set the response by hand,
    // which made an architectural gap look like a per-Blueprint override.
    //
    // Set at runtime, not in the constructor, deliberately: a Blueprint's serialized
    // collision override beats constructor defaults, so a stale override on any enemy
    // BP would silently reopen this. A BeginPlay write wins over both.
    if (GetMesh())
    {
        GetMesh()->SetCollisionResponseToChannel(ECC_Weapon, ECR_Block);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("%s: No Mesh on BeginPlay — this enemy cannot be shot."), *GetName());
    }

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
        // Notify vital point component of damage taken
        if (VitalPointComponent && Data.NewValue < Data.OldValue)
        {
            const float DamageTaken = Data.OldValue - Data.NewValue;
            VitalPointComponent->NotifyDamageTaken(DamageTaken);
        }
    });
    }
    if (HealthBarWidget)
    {
        HealthBarWidget->InitWidget();
        if (UGothicEnemyHealthBarWidget* HealthBarUserWidget =
                Cast<UGothicEnemyHealthBarWidget>(HealthBarWidget->GetUserWidgetObject()))
        {
            HealthBarUserWidget->SetOwningEnemy(this);
        }
        else
        {
            // Loud on purpose: this fires if HealthBarWidget's assigned Widget
            // Class isn't a WBP_EnemyHealthBar-style child of
            // UGothicEnemyHealthBarWidget — the bar will exist but read 0% forever,
            // which otherwise looks identical to "enemy at full health" until
            // someone notices it never moves.
            UE_LOG(LogTemp, Warning,
                TEXT("%s: HealthBarWidget's Widget Class is not a UGothicEnemyHealthBarWidget child — health bar will not update"),
                *GetName());
        }
    }
    
}

void AGothicEnemyBase::MulticastOnHit_Implementation(FVector_NetQuantize ImpactLocation, bool bWasVital)
{
    if (HealthBarWidget)
    {
        HealthBarWidget->SetVisibility(true);
    }
    OnHitFeedback(ImpactLocation, bWasVital);
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
    // Stop AI before Super — controller is still valid here
    if (AAIController* AIC = Cast<AAIController>(GetController()))
    {
        AIC->StopMovement();
        if (AIC->GetBrainComponent())
        {
            AIC->GetBrainComponent()->StopLogic(TEXT("Dead"));
        }
    }

    Super::OnDeath_Implementation(Killer);
    OnEnemyDied.Broadcast(this);

    // Cosmetics fan out to every machine, this one included.
    MulticastOnDeath(Killer);

    if (!OwningEncounter)
    {
        FTimerHandle CorpseTimer;
        GetWorldTimerManager().SetTimer(
            CorpseTimer,
            this,
            &AGothicEnemyBase::DestroyCorpse,
            CorpseLifetime,
            false);
    }
}



void AGothicEnemyBase::MulticastOnDeath_Implementation(AActor* Killer)
{
    PlayDeathCosmetics();
    OnDeathFeedback(Killer);
}

void AGothicEnemyBase::PlayDeathCosmetics()
{
    if (bDeathCosmeticsPlayed)
    {
        return;
    }
    bDeathCosmeticsPlayed = true;

    if (HealthBarWidget)
    {
        HealthBarWidget->SetVisibility(false);
    }

    if (GetMesh())
    {
        GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        GetMesh()->SetCollisionProfileName(FName("Ragdoll"));
        GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        GetMesh()->SetSimulatePhysics(true);
        GetMesh()->SetCollisionResponseToChannel(ECC_Weapon, ECR_Block);
    }

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Clients never ran Super::OnDeath_Implementation, so their movement component
    // is still live and will fight the ragdoll. Shut it down locally on every machine.
    if (GetCharacterMovement())
    {
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
        GetCharacterMovement()->SetComponentTickEnabled(false);
    }
}

void AGothicEnemyBase::DestroyCorpse()
{
    Destroy();
}