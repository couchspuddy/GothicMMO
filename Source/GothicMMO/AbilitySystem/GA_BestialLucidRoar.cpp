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
    // AssetTags set in the Blueprint child — already Ability.Boss.BestialLucid.Roar
    // per the existing BP_GA_BestialLucid_Roar defaults, no change needed there.
}

void UGA_BestialLucidRoar::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    const bool bCommitted = CommitAbility(Handle, ActorInfo, ActivationInfo);
    if (!bCommitted)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    if (GetOwningActorFromActorInfo()->HasAuthority())
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
                TEXT("Roar: %s activated but StunEffectClass is unassigned — no stun will be applied"),
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

        UE_LOG(LogTemp, Log, TEXT("Roar: %s stunned %d player(s) within %.0f"),
            *Avatar->GetName(), PlayersHit, StunRadius);
    }

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}