// GothicBTService_NearbyTerrainCheck.cpp

#include "AI/GothicBTService_NearbyTerrainCheck.h"
#include "AIController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "GameFramework/Pawn.h"

UGothicBTService_NearbyTerrainCheck::UGothicBTService_NearbyTerrainCheck()
{
    NodeName = TEXT("Gothic Nearby Terrain Check");

    // Cheaper than CombatSync's 5Hz — a wall doesn't move, this only needs to
    // be fresh by the time the cadence-gated Wall Pound ability actually
    // checks it, not every frame.
    Interval        = 0.5f;
    RandomDeviation = 0.1f;

    ResultKey.AddBoolFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTService_NearbyTerrainCheck, ResultKey));
}

void UGothicBTService_NearbyTerrainCheck::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);

    if (UBlackboardData* BBAsset = GetBlackboardAsset())
    {
        ResultKey.ResolveSelectedKey(*BBAsset);
    }
}

void UGothicBTService_NearbyTerrainCheck::TickNode(UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    const AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* Pawn               = AIC ? AIC->GetPawn() : nullptr;

    if (!BB || !Pawn || ResultKey.IsNone())
    {
        return;
    }

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(Pawn);

    TArray<AActor*> Overlapping;
    UKismetSystemLibrary::SphereOverlapActors(
        Pawn,
        Pawn->GetActorLocation(),
        CheckRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_WorldStatic) },
        nullptr,
        IgnoreActors,
        Overlapping);

    bool bNearTerrain = false;
    for (const AActor* Actor : Overlapping)
    {
        if (Actor && Actor->ActorHasTag(TerrainTag))
        {
            bNearTerrain = true;
            break;
        }
    }

    BB->SetValue<UBlackboardKeyType_Bool>(ResultKey.GetSelectedKeyID(), bNearTerrain);
}

FString UGothicBTService_NearbyTerrainCheck::GetStaticDescription() const
{
    return FString::Printf(TEXT("%s\nTag: %s, Radius: %.0f"),
        *Super::GetStaticDescription(), *TerrainTag.ToString(), CheckRadius);
}