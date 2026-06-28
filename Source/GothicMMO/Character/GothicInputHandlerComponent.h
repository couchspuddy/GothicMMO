// GothicInputHandlerComponent.h
// Handles all ability input binding for the player character.
// Separates input binding logic from the character class itself.
// Supports multiple Input Mapping Contexts for state changes
// like the Ember activation swapping IMC_Default for IMC_Ember.
//
// Lyra pattern: input handling in a dedicated component,
// not in SetupPlayerInputComponent on the character.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GothicInputHandlerComponent.generated.h"

class UGothicInputConfig;
class UGothicAbilitySystemComponent;
class UEnhancedInputComponent;
class UInputMappingContext;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GOTHICMMO_API UGothicInputHandlerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGothicInputHandlerComponent();

    /**
     * Binds all ability input actions from the InputConfig to the ASC.
     * Call this after the ASC is initialized and valid.
     * Safe to call multiple times — clears previous bindings first.
     *
     * @param InputComponent    The enhanced input component to bind to
     * @param ASC               The ability system component to send tags to
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Input")
    void SetupAbilityInputBindings(
        UEnhancedInputComponent* InputComponent,
        UGothicAbilitySystemComponent* ASC);

    /**
     * Swaps the active Input Mapping Context.
     * Used when entering or exiting the Ember state.
     * Removes OldContext and adds NewContext at the given priority.
     *
     * @param OldContext    The IMC to remove
     * @param NewContext    The IMC to add
     * @param Priority      Input priority for the new context
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Input")
    void SwapMappingContext(
        UInputMappingContext* OldContext,
        UInputMappingContext* NewContext,
        int32 Priority = 1);

    /** The input config defining ability input action to tag mappings. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UGothicInputConfig> InputConfig;

private:
    /** Cached reference to the ASC for input tag forwarding. */
    UPROPERTY()
    TObjectPtr<UGothicAbilitySystemComponent> CachedASC;

    /** Handles for all ability input bindings so they can be cleared. */
    TArray<uint32> AbilityBindingHandles;

    void HandleAbilityInputPressed(FGameplayTag InputTag);
    void HandleAbilityInputReleased(FGameplayTag InputTag);
};