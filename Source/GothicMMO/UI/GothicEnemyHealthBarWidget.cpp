// GothicEnemyHealthBarWidget.cpp

#include "UI/GothicEnemyHealthBarWidget.h"
#include "AI/GothicEnemyBase.h"
#include "AbilitySystem/GothicAttributeSet.h"

void UGothicEnemyHealthBarWidget::SetOwningEnemy(AGothicEnemyBase* InOwningEnemy)
{
	OwningEnemy = InOwningEnemy;
}

float UGothicEnemyHealthBarWidget::GetHealthPercent() const
{
	const AGothicEnemyBase* Enemy = OwningEnemy.Get();
	if (!Enemy)
	{
		return 0.f;
	}

	const float MaxHealth = Enemy->GetMaxHealth();
	return MaxHealth > 0.f ? Enemy->GetHealth() / MaxHealth : 0.f;
}

FText UGothicEnemyHealthBarWidget::GetAccursedName() const
{
	const AGothicEnemyBase* Enemy = OwningEnemy.Get();
	return Enemy ? Enemy->GetAccursedName() : FText::GetEmpty();
}