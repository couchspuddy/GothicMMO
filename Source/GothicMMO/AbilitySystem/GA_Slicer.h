// GA_Slicer.h
// The Slicer — Hunter weapon ability.
// Spawns a thrown projectile. On hit, applies stagger + low HP damage.
// Damage application follows the same centralized pattern as OnFire,
// so all Hunter damage balance lives in one predictable place.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_Slicer.generated.h"

class AGothicSlicerProjectile;
class UGameplayEffect;

UCLASS()
class GOTHICMMO_API UGA_Slicer : public UGothicGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Slicer();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer")
	TSubclassOf<AGothicSlicerProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer")
	TSubclassOf<UGameplayEffect> StaggerEffectClass;

	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** Low HP damage — the Slicer opens, it doesn't kill. */
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer")
	float SlicerDamage = 10.f;

	/** Spawn offset in front of the camera. */
	UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer")
	float SpawnDistance = 100.f;

private:
	UFUNCTION()
	void HandleSlicerHit(AActor* HitActor, FVector HitLocation);
	
	UFUNCTION()
	void HandleSlicerExpired();   

	UPROPERTY()
	TObjectPtr<AGothicSlicerProjectile> SpawnedProjectile;
};