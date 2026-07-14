// GothicCombatStateComponent.cpp

#include "AI/GothicCombatStateComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "TimerManager.h"
#include "Engine/World.h"

UGothicCombatStateComponent::UGothicCombatStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGothicCombatStateComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner())
	{
		CachedASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	}

	if (!CachedASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("GothicCombatStateComponent: No ASC found on %s"),
			*GetOwner()->GetName());
	}
}

void UGothicCombatStateComponent::NotifyCombatAction()
{
	if (!bInCombat)
	{
		EnterCombat();
	}

	// Reset the exit-grace timer regardless — any combat action refreshes it
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			ExitGraceTimerHandle,
			this,
			&UGothicCombatStateComponent::OnExitGraceExpired,
			CombatExitGracePeriod,
			false);
	}
}

void UGothicCombatStateComponent::EnterCombat()
{
	bInCombat = true;

	if (CachedASC && InCombatTag.IsValid())
	{
		CachedASC->AddLooseGameplayTag(InCombatTag);
		UE_LOG(LogTemp, Log, TEXT("CombatState: %s entered combat"), *GetOwner()->GetName());
	}
}

void UGothicCombatStateComponent::OnExitGraceExpired()
{
	bInCombat = false;

	if (CachedASC && InCombatTag.IsValid())
	{
		CachedASC->RemoveLooseGameplayTag(InCombatTag);
		UE_LOG(LogTemp, Log, TEXT("CombatState: %s left combat"), *GetOwner()->GetName());
	}
}

// GothicCombatStateComponent.cpp — new function
void UGothicCombatStateComponent::ForceLeaveCombat()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ExitGraceTimerHandle);
	}
	OnExitGraceExpired();
}