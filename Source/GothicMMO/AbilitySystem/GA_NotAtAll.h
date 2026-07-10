// GA_NotAtAll.h
// "Not At All" — Hunter passive.
// Every elimination has a chance to stun nearby enemies. Larger, more
// deeply Bleed-corrupted enemies have a higher stun chance. While The
// Reckoning is active, eliminating a stunned enemy extends The Reckoning's
// duration, up to its cap.
//
// Implementation: passive ability, granted at spawn, listens for kills made
// by the owning character. Assumes a "GameplayEvent" with tag
// "Event.Kill.Confirmed" is sent to the killer's ASC whenever they land a
// killing blow (payload Target = the killed actor). If your project signals
// kills differently, adjust the WaitGameplayEvent tag below to match.
//
// Blueprint child: BP_GA_NotAtAll
//   - Set KillEventTag (default Event.Kill.Confirmed)
//   - Set StunEffect (the stagger/stun GE to apply to nearby enemies)
//   - Set BaseStunChance (default 0.35)
//   - Set StunRadius (default 400)
//   - Set LargeEnemyStunChanceBonus (default 0.25 — added when target tag
//     Enemy.Size.Large or similar is present on nearby candidates)
//   - Set ReckoningExtensionPerStunnedKill (default 1.5s)

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_NotAtAll.generated.h"

UCLASS()
class GOTHICMMO_API UGA_NotAtAll : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_NotAtAll();

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
    /** GameplayEvent tag sent to this character's ASC when they land a kill. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NotAtAll")
    FGameplayTag KillEventTag;

    /** Stun/stagger GameplayEffect applied to nearby enemies on proc. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NotAtAll")
    TSubclassOf<UGameplayEffect> StunEffect;

    /** Base chance [0-1] to stun each nearby enemy on elimination. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NotAtAll")
    float BaseStunChance = 0.35f;

    /** Radius around the killed enemy to check for other nearby enemies. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NotAtAll")
    float StunRadius = 400.f;

    /** Tag identifying "large" enemies for the increased stun chance rule. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NotAtAll")
    FGameplayTag LargeEnemyTag;

    /** Additional stun chance applied to enemies carrying LargeEnemyTag. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NotAtAll")
    float LargeEnemyStunChanceBonus = 0.25f;

    /** Tag identifying an enemy as currently stunned — used to check Reckoning extension eligibility. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NotAtAll")
    FGameplayTag StunnedStateTag;

    /** Tag identifying that The Reckoning is currently active on this character. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NotAtAll")
    FGameplayTag ReckoningActiveTag;

    /** Seconds added to The Reckoning's duration per stunned-enemy elimination while it's active. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NotAtAll")
    float ReckoningExtensionPerStunnedKill = 1.5f;

private:
    UPROPERTY()
    TObjectPtr<class UAbilitySystemComponent> CachedASC;

    FDelegateHandle KillEventDelegateHandle;

    
    void OnKillConfirmed(const FGameplayEventData* Payload);

    void ApplyStunToNearbyEnemies(AActor* KilledActor);

    /**
     * Attempts to find the character's active GA_Reckoning instance and call
     * ExtendReckoningDuration on it. Returns true if extension was applied.
     */
    bool TryExtendActiveReckoning(float ExtensionAmount);
};
