// BTTask_BossCry.cpp

#include "AI/BTTask_BossCry.h"
#include "AI/GothicBossArenaManager.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/GothicPlayerCharacter.h"

UBTTask_BossCry::UBTTask_BossCry()
{
    NodeName = TEXT("Boss Cry");
    bNotifyTick = false;
}

FString UBTTask_BossCry::GetStaticDescription() const
{
    return FString::Printf(TEXT("Cry: %.0f pillar damage\nStagger: %.0f cm, %.0f deg cone"),
        CryPillarDamage, StaggerRadius, StaggerConeHalfAngle);
}

EBTNodeResult::Type UBTTask_BossCry::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    AAIController* AIC = OwnerComp.GetAIOwner();
    if (!AIC)
    {
        return EBTNodeResult::Failed;
    }

    ACharacter* BossChar = Cast<ACharacter>(AIC->GetPawn());
    if (!BossChar)
    {
        return EBTNodeResult::Failed;
    }

    // --- Pillar damage via arena manager ---
    AGothicBossArenaManager* ArenaManager = Cast<AGothicBossArenaManager>(
        UGameplayStatics::GetActorOfClass(
            BossChar->GetWorld(), AGothicBossArenaManager::StaticClass()));

    if (ArenaManager)
    {
        ArenaManager->ApplyCryDamage(CryPillarDamage);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("BossCry: No ArenaManager found in level"));
    }

    // --- Stagger cone ---
    if (!StaggerEffect)
    {
        UE_LOG(LogTemp, Warning, TEXT("BossCry: No StaggerEffect assigned"));
        return EBTNodeResult::Succeeded;
    }

    const FVector BossLocation = BossChar->GetActorLocation();
    const FVector BossForward = BossChar->GetActorForwardVector();
    const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(StaggerConeHalfAngle));

    // Find all players within radius
    TArray<AActor*> Players;
    UGameplayStatics::GetAllActorsOfClass(
        BossChar->GetWorld(), AGothicPlayerCharacter::StaticClass(), Players);

    int32 PlayersStaggered = 0;

    for (AActor* Player : Players)
    {
        if (!Player) continue;

        const float Distance = FVector::Dist(BossLocation, Player->GetActorLocation());
        if (Distance > StaggerRadius) continue;

        // Cone check — is the player within the frontal cone?
        FVector ToPlayer = Player->GetActorLocation() - BossLocation;
        ToPlayer.Z = 0.f;
        ToPlayer.Normalize();

        const float DotProduct = FVector::DotProduct(BossForward, ToPlayer);

        if (DotProduct < CosHalfAngle) continue;

        // Player is in the cone — apply stagger effect
        UAbilitySystemComponent* PlayerASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Player);

        if (!PlayerASC) continue;

        // Use the boss's ASC as the source
        UAbilitySystemComponent* BossASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(BossChar);

        if (!BossASC) continue;

        FGameplayEffectContextHandle Context = BossASC->MakeEffectContext();
        Context.AddSourceObject(BossChar);

        FGameplayEffectSpecHandle Spec = BossASC->MakeOutgoingSpec(
            StaggerEffect, 1.f, Context);

        if (Spec.IsValid())
        {
            BossASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), PlayerASC);
            PlayersStaggered++;

            UE_LOG(LogTemp, Log, TEXT("BossCry: Staggered %s"), *Player->GetName());
        }
    }

    UE_LOG(LogTemp, Log, TEXT("BossCry: Complete | %d players staggered | Pillar damage: %.0f"),
        PlayersStaggered, CryPillarDamage);

    return EBTNodeResult::Succeeded;
}