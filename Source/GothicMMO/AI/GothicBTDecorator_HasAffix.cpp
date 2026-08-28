// GothicBTDecorator_HasAffix.cpp

#include "AI/GothicBTDecorator_HasAffix.h"
#include "AI/GothicEnemyBase.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "UObject/Enum.h"

UGothicBTDecorator_HasAffix::UGothicBTDecorator_HasAffix()
{
	NodeName = TEXT("Enemy Has Affix");
}

FString UGothicBTDecorator_HasAffix::GetStaticDescription() const
{
	const UEnum* AffixEnum = StaticEnum<EGothicEnemyAffix>();
	const FString AffixName = AffixEnum
		? AffixEnum->GetDisplayNameTextByValue(static_cast<int64>(AffixToCheck)).ToString()
		: TEXT("?");
	return FString::Printf(TEXT("Passes when the owning enemy's EnemyData carries affix: %s"), *AffixName);
}

bool UGothicBTDecorator_HasAffix::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	// None is never a match — see AffixToCheck.
	if (AffixToCheck == EGothicEnemyAffix::None)
	{
		return false;
	}

	const AAIController* AIC = OwnerComp.GetAIOwner();
	const AGothicEnemyBase* Enemy = AIC ? Cast<AGothicEnemyBase>(AIC->GetPawn()) : nullptr;
	if (!Enemy)
	{
		return false;
	}

	// Static per pawn: the affix lives on the EnemyData asset assigned to the
	// enemy Blueprint. Null EnemyData (legacy/unclassified enemies) never matches.
	const UGothicEnemyDataAsset* Data = Enemy->GetEnemyData();
	return Data && Data->HasAffix(AffixToCheck);
}
