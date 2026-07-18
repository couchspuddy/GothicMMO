// GA_BestialLucidWallPound.cpp

#include "AbilitySystem/GA_BestialLucidWallPound.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"

UGA_BestialLucidWallPound::UGA_BestialLucidWallPound()
{
    AbilitySlot = EGothicAbilitySlot::Ability2;
    // AssetTags set in the Blueprint child — see CONSOLIDATION notes from
    // earlier today: this is the field WeightedActionSelect, CombatSync, and
    // ActivateAbilityByTag all read, NOT AbilityInputTag.
}

void UGA_BestialLucidWallPound::ActivateAbility(
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
            SearchRadius,
            TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_WorldStatic) },
            nullptr,
            IgnoreActors,
            Overlapping);

        AActor* NearestWall = nullptr;
        float NearestDistSq = TNumericLimits<float>::Max();

        for (AActor* Candidate : Overlapping)
        {
            if (!Candidate || !Candidate->ActorHasTag(TerrainTag))
            {
                continue;
            }

            const float DistSq = FVector::DistSquared(Avatar->GetActorLocation(), Candidate->GetActorLocation());
            if (DistSq < NearestDistSq)
            {
                NearestDistSq = DistSq;
                NearestWall = Candidate;
            }
        }

        if (NearestWall)
        {
            UE_LOG(LogTemp, Log, TEXT("WallPound: %s triggering collapse on %s"),
                *Avatar->GetName(), *NearestWall->GetName());

            // Blueprint on the wall actor owns what "collapsing" actually
            // looks like — VFX, destructible swap, rubble spawn. This
            // ability only decided which wall and when.
            UFunction* CollapseFn = NearestWall->FindFunction(FName("TriggerWallCollapse"));
            if (CollapseFn)
            {
                NearestWall->ProcessEvent(CollapseFn, nullptr);
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("WallPound: %s has no TriggerWallCollapse event implemented — nothing will visibly happen"),
                    *NearestWall->GetName());
            }
        }
        else
        {
            // Shouldn't happen if the BT's own terrain check gated this
            // correctly, but loud rather than silent if the two radii or
            // tags ever drift out of sync with each other.
            UE_LOG(LogTemp, Warning,
                TEXT("WallPound: %s activated but found no '%s'-tagged actor within %.0f — check SearchRadius against the BT's CheckRadius"),
                *Avatar->GetName(), *TerrainTag.ToString(), SearchRadius);
        }
    }

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}