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
     * Duration GameplayEffect granting State.Read to the CASTER. Drives the HUD
     * proc icon and nothing else — the damage bonus is gated on the target's
     * mark, so this tag existing on the player no longer buffs anything.
     * Assign GE_ReadState in Blueprint.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Read")
    TSubclassOf<UGameplayEffect> ReadStateEffect;

    /**
     * Duration GameplayEffect granting State.Read.Marked to the TARGET.
     *
     * This is what actually makes vitals hurt more. Give it the same duration as
     * ReadStateEffect so the HUD icon and the live mark agree; if they drift the
     * mark is authoritative, because GA_Fire reads the target.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Read")
    TSubclassOf<UGameplayEffect> ReadMarkEffect;

    /** How far the Read reaches to find its target. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Read")
    float ReadRange = 6000.f;

    /**
     * UNUSED by the trace as of the Aug 2026 targeting fix, and deliberately kept
     * rather than deleted so the tuning intent survives.
     *
     * Read used to sweep a sphere of this radius on ECC_Pawn, which marked a
     * different enemy than the one under the crosshair. It now line-traces on
     * ECC_Weapon exactly like GA_Fire, so Read and Fire can never disagree about
     * what you are aiming at — that agreement is the ability, since the mark only
     * pays off on the shot that follows it.
     *
     * If aim tolerance is wanted later, widen FIRE and READ TOGETHER from a shared
     * helper. Reintroducing a fat sweep here alone re-creates the original bug.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Read")
    float ReadTraceRadius = 40.f;
};
