// GA_Read.cpp

#include "AbilitySystem/GA_Read.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

UGA_Read::UGA_Read()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
}

void UGA_Read::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    AActor* Avatar = GetAvatarActorFromActorInfo();

    if (!ASC || !Avatar || !ReadMarkEffect)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GA_Read: missing ASC, avatar or ReadMarkEffect — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    // ── Find who is being read ───────────────────────────────────────────
    // Traced from the camera, matching GA_Fire, so what you are looking at is
    // what you read. Deliberately BEFORE CommitAbility: a Read that finds
    // nothing must not burn the cooldown, or the ability punishes the player
    // for a moving target slipping the trace.
    UWorld* World = Avatar->GetWorld();
    ACharacter* Char = Cast<ACharacter>(Avatar);
    UCameraComponent* Camera = Char ? Char->FindComponentByClass<UCameraComponent>() : nullptr;

    if (!World || !Camera)
    {
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    const FVector Start = Camera->GetComponentLocation();
    const FVector End = Start + Camera->GetForwardVector() * ReadRange;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Avatar);

    // Swept sphere rather than a line -- demanding pixel-accurate aim to read a
    // moving enemy would make the ability feel broken rather than skilful.
    FHitResult Hit;
    const bool bHit = World->SweepSingleByChannel(
        Hit, Start, End, FQuat::Identity, ECC_Pawn,
        FCollisionShape::MakeSphere(ReadTraceRadius), Params);

    UAbilitySystemComponent* TargetASC = (bHit && Hit.GetActor())
        ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor())
        : nullptr;

    if (!TargetASC)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("GA_Read: nothing readable under the crosshair — no cost spent."));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    // ── Mark the target ──────────────────────────────────────────────────
    // The bonus rides on the enemy, not the shooter, so reading one enemy no
    // longer sharpens every shot you take at everything else in the room.
    FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
    Context.AddSourceObject(Avatar);

    FGameplayEffectSpecHandle MarkSpec = ASC->MakeOutgoingSpec(ReadMarkEffect, 1.f, Context);
    if (MarkSpec.IsValid())
    {
        ASC->ApplyGameplayEffectSpecToTarget(*MarkSpec.Data.Get(), TargetASC);
    }

    // Caster-side window tag, HUD only. Kept so IsReadActive() and its proc icon
    // work unchanged; on its own it now grants no damage.
    if (ReadStateEffect)
    {
        FGameplayEffectSpecHandle SelfSpec = ASC->MakeOutgoingSpec(ReadStateEffect, 1.f, Context);
        if (SelfSpec.IsValid())
        {
            ASC->ApplyGameplayEffectSpecToSelf(*SelfSpec.Data.Get());
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("GA_Read: marked %s"), *GetNameSafe(Hit.GetActor()));

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
