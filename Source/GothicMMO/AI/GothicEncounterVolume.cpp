// GothicEncounterVolume.cpp

#include "AI/GothicEncounterVolume.h"
#include "AI/GothicEnemyBase.h"
#include "Game/GothicGameState.h"
#include "Game/GothicPlayerState.h"
#include "Character/GothicPlayerCharacter.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Engine/World.h"

AGothicEncounterVolume::AGothicEncounterVolume()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false; // effects go through GameState, this actor itself never needs to replicate
}

void AGothicEncounterVolume::BeginPlay()
{
    Super::BeginPlay();

    if (!HasAuthority())
    {
        return; // encounter tracking is server-only
    }

    RemainingEnemyCount = EncounterEnemies.Num();

    if (RemainingEnemyCount == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("AGothicEncounterVolume %s: No enemies assigned — this encounter will never complete"),
            *GetName());
        return;
    }

    for (AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        if (!Enemy)
        {
            UE_LOG(LogTemp, Warning, TEXT("AGothicEncounterVolume %s: Null entry in EncounterEnemies — check level placement"),
                *GetName());
            continue;
        }

        Enemy->OwningEncounter = this;
        Enemy->OnEnemyDied.AddDynamic(this, &AGothicEncounterVolume::HandleEnemyDied);
    }
}

// AGothicEncounterVolume.cpp — replace HandleEnemyDied's completion block
void AGothicEncounterVolume::HandleEnemyDied(AGothicEnemyBase* DeadEnemy)
{
    if (!HasAuthority())
    {
        return;
    }

    RemainingEnemyCount = FMath::Max(0, RemainingEnemyCount - 1);
    LastEnemyToDie = DeadEnemy;

    UE_LOG(LogTemp, Log, TEXT("AGothicEncounterVolume %s: %s died — %d remaining"),
        *GetName(), DeadEnemy ? *DeadEnemy->GetName() : TEXT("Unknown"), RemainingEnemyCount);

    if (RemainingEnemyCount <= 0)
    {
        CachedTotalSelah = 0.f;
        CachedGainEffect = nullptr;
        for (AGothicEnemyBase* Enemy : EncounterEnemies)
        {
            if (!Enemy) continue;
            CachedTotalSelah += Enemy->SelahAwardAmount;
            if (!CachedGainEffect && Enemy->SelahGainEffect)
            {
                CachedGainEffect = Enemy->SelahGainEffect;
            }
        }

        AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
        if (GS)
        {
            GS->ActivePromptCorpse = LastEnemyToDie;
            GS->ForceNetUpdate();
            UE_LOG(LogTemp, Log, TEXT("AGothicEncounterVolume %s: Complete — prompt on %s, cached %.1f Selah"),
                *GetName(), LastEnemyToDie ? *LastEnemyToDie->GetName() : TEXT("Unknown"), CachedTotalSelah);
        }
    }
}

// AGothicEncounterVolume.cpp — replace CompleteCollection entirely
void AGothicEncounterVolume::CompleteCollection()
{
    if (!HasAuthority() || !IsComplete())
    {
        return;
    }

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
    if (!GS || GS->ActivePromptCorpse != LastEnemyToDie)
    {
        return;
    }

    if (CachedGainEffect)
    {
        for (APlayerState* PS : GS->PlayerArray)
        {
            AGothicPlayerState* GothicPS = Cast<AGothicPlayerState>(PS);
            UGothicAbilitySystemComponent* ASC = GothicPS ? GothicPS->GetGothicASC() : nullptr;
            if (!ASC) continue;

            FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
            Context.AddSourceObject(this);
            FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(CachedGainEffect, 1.f, Context);

            if (Spec.IsValid())
            {
                Spec.Data->SetSetByCallerMagnitude(
                    FGameplayTag::RequestGameplayTag(FName("Data.Selah")), CachedTotalSelah);
                ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
            }

            if (AGothicPlayerCharacter* PlayerChar = Cast<AGothicPlayerCharacter>(GothicPS->GetPawn()))
            {
                PlayerChar->TriggerSelahMoment();
            }
        }
    }

    GS->CheckpointLocation = GetActorLocation();
    GS->ActivePromptCorpse = nullptr;
    GS->ForceNetUpdate();

    // Reward is secured — safe to clean up every corpse in this encounter now.
    for (AGothicEnemyBase* Enemy : EncounterEnemies)
    {
        if (Enemy)
        {
            Enemy->Destroy();
        }
    }
}