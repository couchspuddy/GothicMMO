// GothicSteadfastComponent.h
// Drives Steadfast accumulation and hold-to-reload conversion into ammo.
// Fills while the owning character is in combat (reuses State.InCombat
// from GothicCombatStateComponent — this is a broad, general trigger for
// the vertical slice; per-class specific fill conditions are a planned
// near-term refinement, not yet implemented).
//
// Spend side (hold-to-reload conversion) is NOT handled here — this
// component only owns the resource itself. Conversion logic lives in
// GothicPlayerCharacter's reload input handling, which should call
// TryConvertSteadfast() on this component.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GothicSteadfastComponent.generated.h"

UCLASS(ClassGroup = (Gothic), meta = (BlueprintSpawnableComponent))
class GOTHICMMO_API UGothicSteadfastComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGothicSteadfastComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    /**
     * Attempts to convert Steadfast into ammo for the given weapon tier cost.
     * Returns the ammo amount granted, or 0 if insufficient Steadfast.
     * Does NOT apply the ammo itself — caller (weapon/reload logic) is
     * responsible for adding the returned amount to actual ammo reserves.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Steadfast")
    float TryConvertSteadfast(float SteadfastCost, float AmmoGranted);

    UFUNCTION(BlueprintPure, Category = "Gothic|Steadfast")
    float GetCurrentSteadfast() const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Steadfast")
    float GetMaxSteadfast() const;

protected:
    /** Steadfast gained per second while in combat. Flat for now — see header note. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Steadfast")
    float FillRatePerSecond = 5.f;

    /** Tag checked to determine whether the character is currently filling Steadfast. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Steadfast")
    FGameplayTag InCombatTag;

private:
    UPROPERTY()
    TObjectPtr<class UAbilitySystemComponent> CachedASC;
};