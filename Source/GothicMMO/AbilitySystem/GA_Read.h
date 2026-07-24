// GA_Read.h
// "The Read" — Hunter ability.
//
// Redesigned July 2026: was a prediction/telegraph aid that traced an enemy and
// painted a highlight showing where the vital point would move next. It is now an
// instant self-buff — no channel, no target trace, no highlight. On activation it
// grants State.Read for a brief window (GE_ReadState's duration), during which
// GA_Fire multiplies vital-hit damage by its ReadVitalDamageMultiplier. Fits the
// Hunter's "observe, then punish" verb and pairs with Reckoning (Read boosts,
// Reckoning guarantees).
//
// Implementation mirrors GA_Reckoning: commit, apply a duration GameplayEffect
// that grants a state tag, done. The tag auto-expires with the effect, so unlike
// Reckoning this ability needs no manual duration timer.
//
// Blueprint child: BP_GA_Read
//   - Assign ReadStateEffect = GE_ReadState (duration GE granting State.Read)
//   - Tune the buff duration on GE_ReadState, and the bonus on GA_Fire's
//     ReadVitalDamageMultiplier.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_Read.generated.h"

UCLASS()
class GOTHICMMO_API UGA_Read : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_Read();

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

protected:
    /**
     * Duration GameplayEffect that grants State.Read. GA_Fire checks that tag to
     * apply the vital-damage bonus. Assign GE_ReadState in Blueprint — a
     * HasDuration effect; its own duration is the length of the buff.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Read")
    TSubclassOf<UGameplayEffect> ReadStateEffect;
};
