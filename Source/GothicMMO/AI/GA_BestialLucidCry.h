// GA_BestialLucidCry.h
// Phase 2 only. AOE stun (same shape as Roar) + spawns Thralls from
// AGothicEnemySpawnPoint actors tagged "CrySpawn" in the Rotunda.
// A cry, not a screech — she is a person the world failed.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_BestialLucidCry.generated.h"

class AGothicEnemySpawnPoint;

UCLASS()
class GOTHICMMO_API UGA_BestialLucidCry : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_BestialLucidCry();

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

protected:
    /** Radius for the AOE stun — same as Roar. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    float StunRadius = 600.f;

    /** The GameplayEffect that grants State.Stunned to players. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    TSubclassOf<UGameplayEffect> StunEffectClass;

    /** Actor tag on AGothicEnemySpawnPoint actors used for Cry spawns. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    FName CrySpawnTag = FName("CrySpawn");

    /** Maximum Thralls alive from Cry at any time. Prevents stacking. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    int32 MaxCryThralls = 3;

    // Base class montage callback
    virtual void OnMontageHitWindow(FGameplayEventData Payload) override;

private:
    void PerformCry();
    void SpawnCryThralls();

    /** Track spawned Thralls so we can cap them. */
    TArray<TWeakObjectPtr<AActor>> SpawnedCryThralls;
};