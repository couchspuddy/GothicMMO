// GothicBTDecorator_PlayerHasAnyTag.cpp

#include "AI/GothicBTDecorator_PlayerHasAnyTag.h"
#include "AI/GothicEnemyAIController.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GothicMMO.h" // LogVigilCombat

namespace
{
	// How often TickNode re-checks for a target switch (target changes are rare;
	// the flip itself is driven by the tag event, not this poll).
	constexpr float kTargetRecheckInterval = 0.25f;
}

UGothicBTDecorator_PlayerHasAnyTag::UGothicBTDecorator_PlayerHasAnyTag()
{
	NodeName = TEXT("Target Has Any Vulnerability Tag");

	// Relevance + tick notifications so we can (un)register listeners and detect
	// target switches. Tick is throttled internally, not per-frame work.
	bNotifyBecomeRelevant = true;
	bNotifyCeaseRelevant  = true;
	bNotifyTick           = true;

	// PLACEHOLDER default — see the header. State.Sprinting is genuinely applied
	// to the player ASC, so the decorator can be verified in PIE before any real
	// reload/Selah vulnerability tag exists.
	TagsThatTrigger.AddTag(GothicTags::State_Sprinting);
}

FString UGothicBTDecorator_PlayerHasAnyTag::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Passes while the enemy's current target ASC has ANY of:\n%s"),
		*TagsThatTrigger.ToStringSimple());
}

UAbilitySystemComponent* UGothicBTDecorator_PlayerHasAnyTag::ResolveTargetASC(
	UBehaviorTreeComponent& OwnerComp) const
{
	AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(OwnerComp.GetAIOwner());
	if (!AIC)
	{
		return nullptr;
	}

	// CO-OP: the enemy's OWN target, not player 0. GetTargetActor() reads the
	// blackboard TargetActor — the same route the orbit task and approach service
	// take. Lazy every call: never cache across possession/target changes.
	AActor* Target = AIC->GetTargetActor();
	if (!Target)
	{
		return nullptr;
	}

	// Routes through the pawn's IAbilitySystemInterface to the ASC that actually
	// lives on the PlayerState.
	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
}

bool UGothicBTDecorator_PlayerHasAnyTag::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	if (TagsThatTrigger.IsEmpty())
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = ResolveTargetASC(OwnerComp);
	if (!ASC)
	{
		return false;
	}

	// ANY match — the target need only carry one vulnerability tag to open the gate.
	return ASC->HasAnyMatchingGameplayTags(TagsThatTrigger);
}

void UGothicBTDecorator_PlayerHasAnyTag::RegisterOn(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, UAbilitySystemComponent* ASC) const
{
	FGothicPlayerHasAnyTagMemory* Mem = reinterpret_cast<FGothicPlayerHasAnyTagMemory*>(NodeMemory);
	if (!Mem || !ASC)
	{
		return;
	}

	Mem->ObservedASC = ASC;
	Mem->TagEventHandles.Reset();

	// Capture the BT comp by pointer (its lifetime spans this node's relevance)
	// and this decorator by const pointer — RequestExecution(const UBTDecorator*)
	// re-runs the branch under the editor-configured observer-abort mode.
	UBehaviorTreeComponent* OwnerCompPtr = &OwnerComp;
	const UGothicBTDecorator_PlayerHasAnyTag* Self = this;

	for (const FGameplayTag& Tag : TagsThatTrigger)
	{
		FDelegateHandle Handle = ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::AnyCountChange)
			.AddLambda([OwnerCompPtr, Self](const FGameplayTag /*ChangedTag*/, int32 /*NewCount*/)
			{
				OwnerCompPtr->RequestExecution(Self);
			});
		Mem->TagEventHandles.Add(Handle);
	}

	UE_LOG(LogVigilCombat, Verbose,
		TEXT("VigilTimeline|Decorator=PlayerHasAnyTag|Event=Register|asc=%s|tags=%s"),
		*GetNameSafe(ASC), *TagsThatTrigger.ToStringSimple());
}

void UGothicBTDecorator_PlayerHasAnyTag::UnregisterFrom(uint8* NodeMemory) const
{
	FGothicPlayerHasAnyTagMemory* Mem = reinterpret_cast<FGothicPlayerHasAnyTagMemory*>(NodeMemory);
	if (!Mem)
	{
		return;
	}

	// The ASC may already be gone (target destroyed) — weak ptr guards that. If it
	// survives (the common case: it outlives the pawn on the PlayerState), pull our
	// listeners so no delegate rides into the next life.
	if (UAbilitySystemComponent* ASC = Mem->ObservedASC.Get())
	{
		const int32 Num = FMath::Min(Mem->TagEventHandles.Num(), TagsThatTrigger.Num());
		int32 Index = 0;
		for (const FGameplayTag& Tag : TagsThatTrigger)
		{
			if (Index >= Num)
			{
				break;
			}
			// Re-fetch the same multicast delegate and Remove our handle — the
			// house idiom (GA_TheLovedandTheLost, GothicPlayerCharacter stun watch).
			ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::AnyCountChange)
				.Remove(Mem->TagEventHandles[Index]);
			++Index;
		}

		UE_LOG(LogVigilCombat, Verbose,
			TEXT("VigilTimeline|Decorator=PlayerHasAnyTag|Event=Unregister|asc=%s"),
			*GetNameSafe(ASC));
	}

	Mem->TagEventHandles.Reset();
	Mem->ObservedASC = nullptr;
}

void UGothicBTDecorator_PlayerHasAnyTag::OnBecomeRelevant(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);

	if (UAbilitySystemComponent* ASC = ResolveTargetASC(OwnerComp))
	{
		RegisterOn(OwnerComp, NodeMemory, ASC);
	}
}

void UGothicBTDecorator_PlayerHasAnyTag::OnCeaseRelevant(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UnregisterFrom(NodeMemory);
	Super::OnCeaseRelevant(OwnerComp, NodeMemory);
}

void UGothicBTDecorator_PlayerHasAnyTag::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	FGothicPlayerHasAnyTagMemory* Mem = reinterpret_cast<FGothicPlayerHasAnyTagMemory*>(NodeMemory);
	if (!Mem)
	{
		return;
	}

	Mem->TimeSinceCheck += DeltaSeconds;
	if (Mem->TimeSinceCheck < kTargetRecheckInterval)
	{
		return;
	}
	Mem->TimeSinceCheck = 0.f;

	// Re-resolve. If the enemy switched targets (a different player's ASC) — or the
	// current one went stale — move our listeners and force a re-evaluation so the
	// gate reflects the new target immediately, not on its next tag change.
	UAbilitySystemComponent* CurrentASC = ResolveTargetASC(OwnerComp);
	if (CurrentASC != Mem->ObservedASC.Get())
	{
		UnregisterFrom(NodeMemory);
		if (CurrentASC)
		{
			RegisterOn(OwnerComp, NodeMemory, CurrentASC);
		}
		OwnerComp.RequestExecution(this);
	}
}

uint16 UGothicBTDecorator_PlayerHasAnyTag::GetInstanceMemorySize() const
{
	return sizeof(FGothicPlayerHasAnyTagMemory);
}

void UGothicBTDecorator_PlayerHasAnyTag::InitializeMemory(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	// Placement-new: the memory block is raw, and our struct has non-trivial members.
	new (NodeMemory) FGothicPlayerHasAnyTagMemory();
}

void UGothicBTDecorator_PlayerHasAnyTag::CleanupMemory(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	reinterpret_cast<FGothicPlayerHasAnyTagMemory*>(NodeMemory)->~FGothicPlayerHasAnyTagMemory();
}
