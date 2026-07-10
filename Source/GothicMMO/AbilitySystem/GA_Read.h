#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GA_Read.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UGothicVitalPointComponent;

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

    virtual void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled) override;

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Read")
    TObjectPtr<UNiagaraSystem> ReadIndicatorSystem;

    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Read")
    float ReadDuration = 3.f;

    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Read")
    float TraceRange = 5000.f;

private:
    UPROPERTY()
    TObjectPtr<UNiagaraComponent> ReadIndicator;

    UPROPERTY()
    TObjectPtr<UGothicVitalPointComponent> TrackedVitalPoint;

    FDelegateHandle VitalShiftHandle;
    FTimerHandle ReadDurationHandle;

    UGothicVitalPointComponent* TraceForVitalPoint() const;

    UFUNCTION()
    void OnVitalPointShifted(int32 NewIndex, FVector NewWorldLocation);

    void OnReadExpired();
};