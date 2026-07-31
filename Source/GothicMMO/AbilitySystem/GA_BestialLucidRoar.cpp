// GA_BestialLucidRoar.cpp

#include "AbilitySystem/GA_BestialLucidRoar.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Character/GothicPlayerCharacter.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TimerManager.h"

UGA_BestialLucidRoar::UGA_BestialLucidRoar()
{
    AbilitySlot = EGothicAbilitySlot::Ability3;
}

void UGA_BestialLucidRoar::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    if (PlayOptionalMontage())
    {
        // Montage started — stun fires in OnMontageHitWindow at the
        // roar's peak frame. Base class EndAbility on montage complete.
        // The montage IS the windup on that path; no timer needed.
        return;
    }

    if (!GetOwningActorFromActorInfo()->HasAuthority())
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    // No montage — telegraph, wait, then stun.
    //
    // The ability deliberately stays ALIVE across the delay instead of ending
    // and leaving a bare world timer behind. InstancingPolicy is
    // InstancedPerExecution, so this instance only survives while the ability
    // is active; ending here would let it be reclaimed with the timer still
    // armed and the stun would fire into a destroyed object. Staying active
    // also means the weighted action selector's IsAbilityActive() check sees
    // the roar as running and won't re-roll over the top of its own windup.
    OnStunWindup();

    UWorld* World = GetWorld();
    if (WindupDelay <= 0.f || !World)
    {
        PerformRoarStun();
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    World->GetTimerManager().SetTimer(
        WindupTimerHandle, this,
        &UGA_BestialLucidRoar::OnWindupElapsed,
        WindupDelay, false);
}

void UGA_BestialLucidRoar::OnWindupElapsed()
{
    PerformRoarStun();

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_BestialLucidRoar::EndAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    // Interrupted mid-windup (staggered, killed, phase transition) — the roar
    // never reached its peak, so it should not stun on the way out.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(WindupTimerHandle);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BestialLucidRoar::OnMontageHitWindow(FGameplayEventData Payload)
{
    if (GetOwningActorFromActorInfo()->HasAuthority())
    {
        PerformRoarStun();
    }
}

void UGA_BestialLucidRoar::PerformRoarStun()
{
    AActor* Avatar = GetAvatarActorFromActorInfo();

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(Avatar);

    TArray<AActor*> Overlapping;
    UKismetSystemLibrary::SphereOverlapActors(
        Avatar,
        Avatar->GetActorLocation(),
        StunRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
        AGothicPlayerCharacter::StaticClass(),
        IgnoreActors,
        Overlapping);

    if (!StunEffectClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Roar: %s activated but StunEffectClass is unassigned"),
            *Avatar->GetName());
    }

    int32 PlayersHit = 0;
    for (AActor* PlayerActor : Overlapping)
    {
        UAbilitySystemComponent* PlayerASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PlayerActor);

        if (PlayerASC && StunEffectClass)
        {
            UGothicAbilitySystemComponent::ApplyEffectToASC(PlayerASC, StunEffectClass, Avatar);
            ++PlayersHit;
        }
    }

}