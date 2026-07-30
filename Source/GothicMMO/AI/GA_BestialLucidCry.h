// GA_BestialLucidCry.h
// Phase 2 only. AOE stun — mechanically Roar, told differently.
// A cry, not a screech — she is a person the world failed.
//
// The Cry CAN spawn Thralls from AGothicEnemySpawnPoint actors tagged
// "CrySpawn", but ships with MaxCryThralls at 0, i.e. off. The fight is
// deliberately a duel: her, the pillars, and the player. The spawn code stays
// because the encounter design may want it back, not because it is running —
// raise MaxCryThralls above 0 and it wakes up unchanged.
//
// What distinguishes Cry from Roar at runtime is therefore CryCueSound and
// whatever the Blueprint hangs off OnStunWindup. Same stun, different voice.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_BestialLucidCry.generated.h"

class AGothicEnemySpawnPoint;
class USoundBase;

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

    virtual void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled) override;

protected:
    /** Radius for the AOE stun — same as Roar. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    float StunRadius = 600.f;

    /** The GameplayEffect that grants State.Stunned to players. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    TSubclassOf<UGameplayEffect> StunEffectClass;

    /**
     * The tell, in seconds. Same reasoning as Roar's: an instant 600-radius
     * stun is not something a player can answer.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    float WindupDelay = 0.9f;

    /**
     * The Cry's voice — what makes it read as a different move from the Roar
     * when both do the same thing to the player. Played at the START of the
     * windup, so it is the sound the player reacts to, not a confirmation.
     * Assign in BP_GA_BestialLucid_Cry.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    TObjectPtr<USoundBase> CryCueSound;

    /** Fires at the start of the windup. VFX hook — implement in the Blueprint. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Cry")
    void OnStunWindup();

    /** Actor tag on AGothicEnemySpawnPoint actors used for Cry spawns. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    FName CrySpawnTag = FName("CrySpawn");

    /**
     * Maximum Thralls alive from Cry at any time.
     *
     * ZERO ships the fight with no adds — a design decision, not a bug and not
     * a placeholder. The Bestial Lucid is meant to be fought alone; thralls
     * turned a duel into crowd control and buried her own telegraphs. Set this
     * above 0 to re-enable the spawn path, which is otherwise untouched.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cry")
    int32 MaxCryThralls = 0;

    // Base class montage callback
    virtual void OnMontageHitWindow(FGameplayEventData Payload) override;

private:
    void PerformCry();
    void SpawnCryThralls();

    /** Timer target: the windup has elapsed, land the stun and finish. */
    void OnWindupElapsed();

    FTimerHandle WindupTimerHandle;

    /** Track spawned Thralls so we can cap them. */
    TArray<TWeakObjectPtr<AActor>> SpawnedCryThralls;
};