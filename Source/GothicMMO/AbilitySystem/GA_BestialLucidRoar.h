// GA_BestialLucidRoar.h
// AOE stun. Applies the State.Stunned tag to every player in range — that
// tag is already wired as an ActivationBlockedTags entry on Fire and
// Hunter's Strike, so this doesn't need to invent a stun mechanism, it just
// needs to grant a tag those abilities already respect.
//
// Scope note: this blocks ABILITY activation only (no Fire, no melee).
// Movement/camera aren't GAS-gated in this project, so a stunned player can
// still walk and look around. If a full freeze is wanted later, that's a
// separate, small addition (disabling movement input for the duration) —
// not something granting a tag can cover on its own.
//
// Self-contained, same pattern as GA_BestialLucidWallPound: does its own
// overlap query rather than reading anything off the Blackboard. Targets
// AGothicPlayerCharacter directly via class filter — no tagging step needed
// in the level, unlike Wall Pound's wall/pillar targets.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_BestialLucidRoar.generated.h"

UCLASS()
class GOTHICMMO_API UGA_BestialLucidRoar : public UGothicGameplayAbility
{
    GENERATED_BODY()

public:
    UGA_BestialLucidRoar();

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override;

protected:
    /** Radius around her that gets hit. Tune in engine — no locked number yet. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roar")
    float StunRadius = 600.f;

    /**
     * The GameplayEffect that grants State.Stunned. Assign a new GE in the
     * Blueprint child — Duration Policy = Has Duration, Duration Magnitude =
     * Scalable Float (a fixed design constant, same reasoning as the cooldown-
     * duration lesson from earlier: this isn't runtime-variable, it belongs
     * on the GE, not passed in from a call site), Granted Tags = State.Stunned.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roar")
    TSubclassOf<UGameplayEffect> StunEffectClass;
};