// AnimNotify_GroundPoundImpact.cpp

#include "AI/AnimNotify_GroundPoundImpact.h"
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

        const float Distance = FVector::Dist(ImpactLocation, Player->GetActorLocation());
        if (Distance > ImpactRadius) continue;

        UAbilitySystemComponent* PlayerASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Player);

        if (!PlayerASC) continue;

        FGameplayEffectContextHandle Context = BossASC->MakeEffectContext();
        Context.AddSourceObject(BossActor);

        FGameplayEffectSpecHandle Spec = BossASC->MakeOutgoingSpec(
            DamageEffect, 1.f, Context);

        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(
                FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
                ImpactDamage);

            BossASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), PlayerASC);
            PlayersHit++;
        }
    }

}