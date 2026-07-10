// GA_NotAtAll.cpp

#include "AbilitySystem/GA_NotAtAll.h"
#include "AbilitySystem/GA_Reckoning.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

UGA_NotAtAll::UGA_NotAtAll()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_NotAtAll::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    CachedASC = GetAbilitySystemComponentFromActorInfo();

    if (!CachedASC || !KillEventTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_NotAtAll: Missing ASC or KillEventTag — this passive will not function"));
        return;
    }

    // Listen for kill confirmation events for the lifetime of this passive
    KillEventDelegateHandle = CachedASC->GenericGameplayEventCallbacks
        .FindOrAdd(KillEventTag)
        .AddUObject(this, &UGA_NotAtAll::OnKillConfirmed);

    UE_LOG(LogTemp, Log, TEXT("GA_NotAtAll: Passive active, listening for %s"),
        *KillEventTag.ToString());
}

void UGA_NotAtAll::OnKillConfirmed(const FGameplayEventData* Payload)
{
    if (!Payload)
    {
        return;
    }

    AActor* KilledActor = const_cast<AActor*>(Payload->Target.Get());
    if (!KilledActor)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("GA_NotAtAll: Kill confirmed on %s — checking nearby enemies"),
        *KilledActor->GetName());

    ApplyStunToNearbyEnemies(KilledActor);
}

void UGA_NotAtAll::ApplyStunToNearbyEnemies(AActor* KilledActor)
{
    if (!KilledActor || !GetWorld() || !StunEffect)
    {
        return;
    }

    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(StunRadius);

    GetWorld()->OverlapMultiByChannel(
        Overlaps,
        KilledActor->GetActorLocation(),
        FQuat::Identity,
        ECC_Pawn,
        Sphere);

    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Candidate = Overlap.GetActor();
        if (!Candidate || Candidate == KilledActor)
        {
            continue;
        }

        UAbilitySystemComponent* CandidateASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate);

        if (!CandidateASC)
        {
            continue;
        }

        float StunChance = BaseStunChance;

        if (LargeEnemyTag.IsValid() && CandidateASC->HasMatchingGameplayTag(LargeEnemyTag))
        {
            StunChance += LargeEnemyStunChanceBonus;
        }

        StunChance = FMath::Clamp(StunChance, 0.f, 1.f);

        if (FMath::FRand() <= StunChance)
        {
            FGameplayEffectContextHandle Context = CandidateASC->MakeEffectContext();
            FGameplayEffectSpecHandle Spec = CandidateASC->MakeOutgoingSpec(StunEffect, 1.f, Context);

            if (Spec.IsValid())
            {
                CandidateASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

                UE_LOG(LogTemp, Log, TEXT("GA_NotAtAll: Stunned %s (chance was %.2f)"),
                    *Candidate->GetName(), StunChance);
            }

            // If this candidate was already stunned AND Reckoning is active,
            // and this overlap check is itself catching a stunned-kill scenario,
            // extension should be triggered from the enemy's death handler
            // calling TryExtendActiveReckoning — see header note. This function
            // only handles applying new stuns from the triggering kill.
        }
    }
}


bool UGA_NotAtAll::TryExtendActiveReckoning(float ExtensionAmount)
    {
        if (!CachedASC || !ReckoningActiveTag.IsValid())
        {
            return false;
        }

        if (!CachedASC->HasMatchingGameplayTag(ReckoningActiveTag))
        {
            return false;
        }

        for (const FGameplayAbilitySpec& Spec : CachedASC->GetActivatableAbilities())
        {
            if (UGA_Reckoning* Reckoning = Cast<UGA_Reckoning>(Spec.GetPrimaryInstance()))
            {
                Reckoning->ExtendReckoningDuration(ExtensionAmount);
                UE_LOG(LogTemp, Log, TEXT("GA_NotAtAll: Extended Reckoning by %.1f via stunned elimination"),
                    ExtensionAmount);
                return true;
            }
        }

        return false;
    }
void UGA_NotAtAll::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    if (CachedASC && KillEventTag.IsValid() && KillEventDelegateHandle.IsValid())
    {
        CachedASC->GenericGameplayEventCallbacks
            .FindOrAdd(KillEventTag)
            .Remove(KillEventDelegateHandle);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
