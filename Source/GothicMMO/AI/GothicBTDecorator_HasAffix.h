// GothicBTDecorator_HasAffix.h
// BT Decorator — passes when the owning enemy's EnemyData carries the configured
// behavioural affix in EITHER slot (Primary or Secondary). The gate for hanging an
// affix-specific branch (e.g. the Evader sidestep) inside a shared BT_EnemyCombat:
// author the branch behind this decorator pointed at, say, Affix.Evader, and it
// only runs on enemies whose data asset actually declares that affix.
//
// Affix is STATIC per enemy — it comes from the possessed pawn's EnemyData asset
// and never changes at runtime — so this is a plain CalculateRawConditionValue with
// no relevance/tick/observer machinery (unlike GothicBTDecorator_PlayerHasAnyTag,
// which watches a live, changing ASC). Set "Observer aborts" in the BT editor as
// needed; the value it returns is constant for a given pawn regardless.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "AI/GothicEnemyDataAsset.h"   // EGothicEnemyAffix used by-value below
#include "GothicBTDecorator_HasAffix.generated.h"

UCLASS(meta = (DisplayName = "Gothic Has Affix"))
class GOTHICMMO_API UGothicBTDecorator_HasAffix : public UBTDecorator
{
	GENERATED_BODY()

public:
	UGothicBTDecorator_HasAffix();

	virtual FString GetStaticDescription() const override;

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

	/** The affix the owning enemy must carry (in either slot) for this to pass.
	 *  None never passes — an unconfigured decorator is inert, not a wildcard. */
	UPROPERTY(EditAnywhere, Category = "Condition")
	EGothicEnemyAffix AffixToCheck = EGothicEnemyAffix::None;
};
