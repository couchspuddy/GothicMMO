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
#include "Kismet/GameplayStatics.h"
#include "Character/GothicPlayerCharacter.h"
#include "AbilitySystemBlueprintLibrary.h"
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
    Super::OnDeath_Implementation(Killer);

    UE_LOG(LogTemp, Log, TEXT("GothicEnemyBase: %s died — running Selah proximity check"),
        *GetName());

    // Check for nearby Embers and award Selah
    AwardSelahToNearbyEmbers();

    // Hide health bar
    if (HealthBarWidget)
    {
        HealthBarWidget->SetVisibility(false);
    }

    // Ragdoll
    if (GetMesh())
    {
        GetMesh()->SetCollisionProfileName(FName("Ragdoll"));
        GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        //GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        //GetMesh()->SetSimulatePhysics(true);

        // Small delay before impulse OR apply impulse after simulate is set
        //FVector ImpulseDir = GetActorForwardVector() * -1.f;
        //GetMesh()->AddImpulse(ImpulseDir * 500.f, NAME_None, true);
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

    // Find all player characters within radius
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

        // Player is within range — award Selah
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

            // Trigger the Selah moment on this player
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
