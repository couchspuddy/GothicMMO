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

	// First-spawn pawns resolve here. Respawned and joining pawns don't have an
	// ASC yet at this point — ResolveASC() picks them up on first use.
	ResolveASC();
}

UAbilitySystemComponent* UGothicCombatStateComponent::ResolveASC()
{
	if (CachedASC)
	{
		return CachedASC;
	}

	if (GetOwner())
	{
		CachedASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	}

	if (!CachedASC && !bWarnedNoASC)
	{
		bWarnedNoASC = true;
		UE_LOG(LogTemp, Warning, TEXT("GothicCombatStateComponent: No ASC found on %s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("NULL owner"));
	}

	return CachedASC;
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

	UAbilitySystemComponent* ASC = ResolveASC();
	if (ASC && InCombatTag.IsValid())
	{
		ASC->AddLooseGameplayTag(InCombatTag);
	}
}

void UGothicCombatStateComponent::OnExitGraceExpired()
{
	bInCombat = false;

	UAbilitySystemComponent* ASC = ResolveASC();
	if (ASC && InCombatTag.IsValid())
	{
		ASC->RemoveLooseGameplayTag(InCombatTag);
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