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

        // Horizontal, like the cone test immediately below it — this is a
        // shockwave across the floor, not a sphere. Measured in 3D it shrank
        // with the boss's capsule: her actor origin sits 253uu up against the
        // player's 88uu, so a nominal StaggerRadius lost most of a metre of
        // effective reach purely because she got taller.
        const float Distance = FVector::Dist2D(BossLocation, Player->GetActorLocation());
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

        }
    }

    // PlayersStaggered was counted and then discarded. It is the one number that
    // separates "the Cry fired but nobody was in the cone" from "the Cry fired and
    // the stagger did not land" — the open playtest question on this ability.
    UE_LOG(LogTemp, Verbose,
        TEXT("BossCry[%s]: staggered %d of %d player(s) within %.0fuu / %.0fdeg"),
        *BossChar->GetName(), PlayersStaggered, Players.Num(),
        StaggerRadius, StaggerConeHalfAngle);

    return EBTNodeResult::Succeeded;
}