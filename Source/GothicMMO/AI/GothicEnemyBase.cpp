// GothicEnemyBase.cpp

#include "AI/GothicEnemyBase.h"
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
    
    VitalPointComponent = CreateDefaultSubobject<UGothicVitalPointComponent>(
    TEXT("VitalPointComponent"));
    CombatStateComponent = CreateDefaultSubobject<UGothicCombatStateComponent>(TEXT("CombatStateComponent"));
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
        // Notify vital point component of damage taken
        if (VitalPointComponent && Data.NewValue < Data.OldValue)
        {
            const float DamageTaken = Data.OldValue - Data.NewValue;
            VitalPointComponent->NotifyDamageTaken(DamageTaken);
        }
    });
    }
    
}

void AGothicEnemyBase::MulticastOnHit_Implementation(FVector_NetQuantize ImpactLocation, bool bWasVital)
{
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

void AGothicEnemyBase::CollectAllNearbyCorpses(AActor* Collector, float Radius, UObject* WorldContextObject)
{
    if (!Collector || !WorldContextObject)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return;
    }

    TArray<AActor*> AllEnemies;
    UGameplayStatics::GetAllActorsOfClass(World, AGothicEnemyBase::StaticClass(), AllEnemies);

    int32 CorpsesCollected = 0;

    for (AActor* Actor : AllEnemies)
    {
        AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(Actor);
        if (!Enemy) continue;
        UE_LOG(LogTemp, Log, TEXT("CollectAllNearbyCorpses: Checking %s | bCollected=%d | IsAlive=%d"),
    *Enemy->GetName(), Enemy->bCollected, Enemy->IsAlive());
        if (!Enemy || Enemy->bCollected || Enemy->IsAlive() || Enemy->OwningEncounter)
        {
            continue;
        }

        const float Distance = FVector::Dist(Collector->GetActorLocation(), Enemy->GetActorLocation());
        UE_LOG(LogTemp, Log, TEXT("CollectAllNearbyCorpses: Distance to %s = %.0f (Radius=%.0f)"),
    *Enemy->GetName(), Distance, Radius);
        
        if (Distance > Radius)
        {
            continue;
        }

        UGothicAbilitySystemComponent* PlayerASC =
            Cast<UGothicAbilitySystemComponent>(
                UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Collector));

        if (PlayerASC && Enemy->SelahGainEffect)
        {
            FGameplayEffectContextHandle Context = PlayerASC->MakeEffectContext();
            Context.AddSourceObject(Enemy);
            FGameplayEffectSpecHandle Spec = PlayerASC->MakeOutgoingSpec(
                Enemy->SelahGainEffect, 1.f, Context);
            if (Spec.IsValid())
            {
                Spec.Data->SetSetByCallerMagnitude(
                    FGameplayTag::RequestGameplayTag(FName("Data.Selah")),
                    Enemy->SelahAwardAmount);
                PlayerASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
                UE_LOG(LogTemp, Log, TEXT("CollectAllNearbyCorpses: Awarded %.1f Selah from %s"),
                    Enemy->SelahAwardAmount, *Enemy->GetName());
            }
        }

        Enemy->bCollected = true;
        CorpsesCollected++;
    }

    if (CorpsesCollected > 0)
    {
        AGothicPlayerCharacter* PlayerChar = Cast<AGothicPlayerCharacter>(Collector);
        if (PlayerChar)
        {
            PlayerChar->TriggerSelahMoment();
        }
    }

    UE_LOG(LogTemp, Log, TEXT("CollectAllNearbyCorpses: Collected from %d corpses"), CorpsesCollected);
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
