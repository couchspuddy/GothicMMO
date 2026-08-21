// AnimNotify_GroundPoundImpact.cpp

#include "AI/AnimNotify_GroundPoundImpact.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Character/GothicPlayerCharacter.h"
#include "GameFramework/Character.h"

void UAnimNotify_GroundPoundImpact::Notify(
    USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!MeshComp || !MeshComp->GetOwner())
    {
        return;
    }

    AActor* BossActor = MeshComp->GetOwner();

    // Only execute on the server
    if (!BossActor->HasAuthority())
    {
        return;
    }

    if (!DamageEffect)
    {
        UE_LOG(LogTemp, Warning, TEXT("GroundPoundImpact: No DamageEffect assigned"));
        return;
    }

    UAbilitySystemComponent* BossASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(BossActor);

    if (!BossASC)
    {
        UE_LOG(LogTemp, Warning, TEXT("GroundPoundImpact: No ASC on boss"));
        return;
    }

    const FVector ImpactLocation = BossActor->GetActorLocation();

    // Find all players within radius
    TArray<AActor*> Players;
    UGameplayStatics::GetAllActorsOfClass(
        BossActor->GetWorld(), AGothicPlayerCharacter::StaticClass(), Players);

    int32 PlayersHit = 0;

    for (AActor* Player : Players)
    {
        if (!Player) continue;

        // Horizontal. The ground pound is a ring travelling out across the
        // floor; ImpactLocation is the boss's actor origin, which sits at her
        // capsule centre and rose 165uu when that capsule was fixed. In 3D
        // every one of these radii quietly shrank by the same amount.
        const float Distance = FVector::Dist2D(ImpactLocation, Player->GetActorLocation());
        if (Distance > ImpactRadius) continue;

        UAbilitySystemComponent* PlayerASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Player);

        if (!PlayerASC) continue;

        // Boss pawn as instigator — MakeEffectContext alone names the ASC's
        // OwnerActor, the AIController, which has no ASC to read AttackPower off.
        FGameplayEffectContextHandle Context =
            UGothicAbilitySystemComponent::MakeDamageContext(BossASC, BossActor);

        FGameplayEffectSpecHandle Spec = BossASC->MakeOutgoingSpec(
            DamageEffect, 1.f, Context);

        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(
                GothicTags::Data_Damage,
                ImpactDamage);

            BossASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), PlayerASC);
            PlayersHit++;
        }
    }

}