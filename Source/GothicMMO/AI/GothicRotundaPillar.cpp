// GothicRotundaPillar.cpp

#include "AI/GothicRotundaPillar.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/GothicPlayerCharacter.h"
#include "TimerManager.h"
#include "NavigationSystem.h"

AGothicRotundaPillar::AGothicRotundaPillar()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;

    PillarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PillarMesh"));
    RootComponent = PillarMesh;

    CollapseDamageVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("CollapseDamageVolume"));
    CollapseDamageVolume->SetupAttachment(RootComponent);
    CollapseDamageVolume->SetBoxExtent(FVector(200.f, 200.f, 300.f));
    CollapseDamageVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CollapseDamageVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollapseDamageVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AGothicRotundaPillar::BeginPlay()
{
    Super::BeginPlay();
    CurrentHealth = MaxHealth;
    CurrentState = EPillarState::Healthy;

    // Force blocking volume to start hidden and disabled
    // regardless of how it was placed in the editor
    if (BlockingVolumeActor)
    {
        BlockingVolumeActor->SetActorHiddenInGame(true);
        BlockingVolumeActor->SetActorEnableCollision(false);
    }
}

bool AGothicRotundaPillar::ApplyPillarDamage(float DamageAmount)
{
    if (CurrentState == EPillarState::Destroyed) return false;

    CurrentHealth = FMath::Max(0.f, CurrentHealth - DamageAmount);

    UE_LOG(LogTemp, Log, TEXT("Pillar %s took %.1f damage | HP: %.1f / %.1f"),
        *GetName(), DamageAmount, CurrentHealth, MaxHealth);

    if (CurrentHealth <= 0.f)
    {
        TransitionToState(EPillarState::Destroyed);
        return true;
    }
    else if (CurrentHealth <= MaxHealth * CrackedThreshold
             && CurrentState == EPillarState::Healthy)
    {
        TransitionToState(EPillarState::Cracked);
    }

    return false;
}

void AGothicRotundaPillar::TransitionToState(EPillarState NewState)
{
    if (CurrentState == NewState) return;
    CurrentState = NewState;

    switch (NewState)
    {
    case EPillarState::Cracked:
        if (CrackedMaterial && PillarMesh)
        {
            PillarMesh->SetMaterial(0, CrackedMaterial);
        }
        OnPillarCracked();
        break;

    case EPillarState::Destroyed:
        BeginCeilingCollapse();
        OnPillarCollapse();
        OnPillarDestroyed.Broadcast(this);
        break;

    default:
        break;
    }
}

void AGothicRotundaPillar::BeginCeilingCollapse()
{
    UE_LOG(LogTemp, Log, TEXT("Pillar %s: Beginning ceiling collapse"), *GetName());

    if (PillarMesh)
    {
        PillarMesh->SetVisibility(false);
        PillarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    CollapseDamageVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ApplyCollapseDamageToPlayersInZone();

    FTimerHandle CollapseTimer;
    GetWorldTimerManager().SetTimer(
        CollapseTimer, this,
        &AGothicRotundaPillar::FinishCeilingCollapse,
        CollapseDuration, false);
}

void AGothicRotundaPillar::FinishCeilingCollapse()
{
    UE_LOG(LogTemp, Log, TEXT("Pillar %s: Collapse complete"), *GetName());

    CollapseDamageVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    if (BlockingVolumeActor)
    {
        BlockingVolumeActor->SetActorHiddenInGame(false);
        BlockingVolumeActor->SetActorEnableCollision(true);

        UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        if (NavSys)
        {
            NavSys->Build();
        }
    }

    if (CeilingMesh)
    {
        FVector Loc = CeilingMesh->GetComponentLocation();
        Loc.Z -= 400.f;
        CeilingMesh->SetWorldLocation(Loc);
    }
}

void AGothicRotundaPillar::ApplyCollapseDamageToPlayersInZone()
{
    if (!CollapseDamageEffect || !HasAuthority()) return;

    TArray<AActor*> OverlappingActors;
    CollapseDamageVolume->GetOverlappingActors(
        OverlappingActors, AGothicPlayerCharacter::StaticClass());

    for (AActor* Actor : OverlappingActors)
    {
        UAbilitySystemComponent* TargetASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
        if (!TargetASC) continue;

        FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
        Context.AddSourceObject(this);

        FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(
            CollapseDamageEffect, 1.f, Context);

        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(
                FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
                CollapseDamage);
            TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

            UE_LOG(LogTemp, Log, TEXT("Pillar %s: Collapse dealt %.1f to %s"),
                *GetName(), CollapseDamage, *Actor->GetName());
        }
    }
}