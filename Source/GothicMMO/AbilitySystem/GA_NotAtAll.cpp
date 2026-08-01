// GA_NotAtAll.cpp

#include "AbilitySystem/GA_NotAtAll.h"
#include "AbilitySystem/GA_Reckoning.h"
#include "AbilitySystem/GothicGameplayTags.h"
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

    // Say out loud that the size rule is off rather than letting an authored 0.25
    // bonus read as a working mechanic. No Enemy.Size.* tag exists yet.
    if (!LargeEnemyTag.IsValid() && LargeEnemyStunChanceBonus != 0.f)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GA_NotAtAll: LargeEnemyStunChanceBonus is %.2f but LargeEnemyTag is unset — ")
            TEXT("the large-enemy stun bonus is inert. Author a size tag on the enemies to enable it."),
            LargeEnemyStunChanceBonus);
    }
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


    // "Eliminating a stunned enemy extends The Reckoning" — the header's second
    // design line. Every piece of it was already built and authored
    // (ReckoningExtensionPerStunnedKill 1.5, GA_Reckoning::ExtendReckoningDuration,
    // BP_GA_Reckoning.MaxExtendedDuration 12 against BaseDuration 6) except this
    // call, so the cap was unreachable and the extension never happened. Checked
    // BEFORE the new stuns go out, so the kill that caused the stun does not also
    // count as a stunned kill.
    if (const UAbilitySystemComponent* KilledASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(KilledActor))
    {
        if (KilledASC->HasMatchingGameplayTag(GothicTags::State_Stunned))
        {
            TryExtendActiveReckoning(ReckoningExtensionPerStunnedKill);
        }
    }

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

            }

            // This function only applies new stuns. The Reckoning extension for
            // killing an already-stunned enemy is handled in OnKillConfirmed,
            // before this runs.
        }
    }
}


bool UGA_NotAtAll::TryExtendActiveReckoning(float ExtensionAmount)
{
    if (!CachedASC)
    {
        return false;
    }

    // An unauthored ReckoningActiveTag used to make this return false before it
    // did anything. State.Reckoning is the tag GE_ReckoningState grants, so fall
    // back to it rather than silently doing nothing.
    const FGameplayTag ActiveTag = ReckoningActiveTag.IsValid()
        ? ReckoningActiveTag
        : GothicTags::State_Reckoning;

    if (!CachedASC->HasMatchingGameplayTag(ActiveTag))
    {
        return false;
    }

    for (const FGameplayAbilitySpec& Spec : CachedASC->GetActivatableAbilities())
    {
        if (UGA_Reckoning* Reckoning = Cast<UGA_Reckoning>(Spec.GetPrimaryInstance()))
        {
            Reckoning->ExtendReckoningDuration(ExtensionAmount);
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
