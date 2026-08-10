// GothicBTDecorator_PlayerHasAnyTag.h
// BT Decorator — passes while the enemy's CURRENT TARGET has any of the
// configured gameplay tags on its Ability System Component. Built for the
// "pack surges when the player is vulnerable" behaviour: gate the aggressive
// branch (surge / tighten orbit / spend a token early) behind this decorator
// and it opens the instant a vulnerability tag lands on the target and closes
// the instant it clears.
//
// CO-OP: the target is resolved through AGothicEnemyAIController::GetTargetActor()
// — the enemy's own blackboard target, NOT player 0. Two players, two ASCs; the
// decorator watches whichever one this enemy is hunting.
//
// The player ASC lives on the PlayerState and OUTLIVES the pawn (pawns respawn).
// We never cache a raw ASC pointer — it is held weak, re-resolved on tick, and
// re-registered when the target (and therefore the ASC) switches. Tag listeners
// are torn down on OnCeaseRelevant and on every switch so no delegate is left
// haunting the outliving PlayerState ASC.
//
// Observer abort is left to the BT editor's "Observer aborts" dropdown; a tag
// change on the watched ASC calls RequestExecution(this) so the tree re-evaluates
// under whatever abort mode the designer picks. BT wiring is a later editor edit.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "GameplayTagContainer.h"
#include "GothicBTDecorator_PlayerHasAnyTag.generated.h"

// Per-node relevance memory. Non-trivial members (weak ptr, arrays) mean this
// node overrides Initialize/CleanupMemory to placement-new and destruct it.
struct FGothicPlayerHasAnyTagMemory
{
	// The ASC we currently have tag listeners registered on. Weak so a target
	// pawn dying/respawning (ASC survives on PlayerState) or the target vanishing
	// never leaves us dereferencing freed memory.
	TWeakObjectPtr<class UAbilitySystemComponent> ObservedASC;

	// One handle per tag in TagsThatTrigger, same order, for clean unregistration.
	TArray<FDelegateHandle> TagEventHandles;

	// Throttle for the target-switch re-resolve check in TickNode.
	float TimeSinceCheck = 0.f;
};

UCLASS()
class GOTHICMMO_API UGothicBTDecorator_PlayerHasAnyTag : public UBTDecorator
{
	GENERATED_BODY()

public:
	UGothicBTDecorator_PlayerHasAnyTag();

	virtual FString GetStaticDescription() const override;

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;

	// ANY of these tags present on the target ASC opens the decorator.
	//
	// PLACEHOLDER DEFAULT: State.Sprinting. No reload- or Selah-in-progress tag is
	// applied to the player ASC anywhere in the codebase today (investigation
	// finding — Hint.Reload is a HUD hint, not an ASC tag; the Selah moment is a
	// pawn-side lock, not a tag). State.Sprinting is a real, reproducible loose tag
	// on the player ASC, so this default makes the decorator PIE-verifiable now.
	// Replace it (editor hand-edit) with the real vulnerability tags once a
	// reload/Selah state tag is actually applied to the player — applying that tag
	// is a separate, player-side change deliberately OUT of scope here.
	UPROPERTY(EditAnywhere, Category = "Condition")
	FGameplayTagContainer TagsThatTrigger;

private:
	// Resolve the ASC of this enemy's current blackboard target, or nullptr.
	class UAbilitySystemComponent* ResolveTargetASC(UBehaviorTreeComponent& OwnerComp) const;

	// (Un)register AnyCountChange listeners for every tag in TagsThatTrigger.
	void RegisterOn(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, class UAbilitySystemComponent* ASC) const;
	void UnregisterFrom(uint8* NodeMemory) const;
};
