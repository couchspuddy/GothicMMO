// GA_BestialLucidRoar.cpp

#include "AbilitySystem/GA_BestialLucidRoar.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Character/GothicPlayerCharacter.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"

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
        return;
    }

    // No montage — instant fallback
    if (GetOwningActorFromActorInfo()->HasAuthority())
    {
        PerformRoarStun();
    }

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
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