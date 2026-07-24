// GothicAbilitySystemComponent.h
// Thin wrapper around UAbilitySystemComponent.
// Adds MMO-specific helpers: ability slot binding, Ether management,
// and a centralized "grant startup abilities" path used by both
// players (from PlayerState) and enemies (from Character directly).

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GothicAbilitySystemComponent.generated.h"

class UGothicGameplayAbility;

/**
 * Slot numbers map to input actions.
 * Slot 0 = Light Attack, 1 = Heavy Attack, 2-5 = Covenant Abilities.
 * Bind these to Enhanced Input actions in BP_GothicPlayerController.
 */
UENUM(BlueprintType)
enum class EGothicAbilitySlot : uint8
{
    LightAttack   UMETA(DisplayName = "Light Attack"),
    HeavyAttack   UMETA(DisplayName = "Heavy Attack"),
    Ability1      UMETA(DisplayName = "Ability 1"),
    Ability2      UMETA(DisplayName = "Ability 2"),
    Ability3      UMETA(DisplayName = "Ability 3"),
    SuperAbility  UMETA(DisplayName = "Super / Covenant Power"),
    PrimaryFire   UMETA(DisplayName = "Primary Fire"),
};

UCLASS()
class GOTHICMMO_API UGothicAbilitySystemComponent : public UAbilitySystemComponent
{
    GENERATED_BODY()

public:
    UGothicAbilitySystemComponent();
    
    /** Static convenience — applies a GE from one ASC to another without building a full spec inline. */
    static void ApplyEffectToASC(
        UAbilitySystemComponent* TargetASC,
        TSubclassOf<UGameplayEffect> EffectClass,
        AActor* SourceActor);

    /**
     * Grants a list of abilities from a data-driven array.
     * Call this on the server only — abilities are server-authoritative.
     * Typically invoked from AGothicCharacterBase::PossessedBy or
     * AGothicEnemyBase::BeginPlay.
     *
     * @param AbilitiesToGrant  Array of ability classes paired with slot enums.
     * @param Level             Ability level (scales with character level later).
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Abilities")
    void GrantStartupAbilities(const TArray<TSubclassOf<UGothicGameplayAbility>>& AbilitiesToGrant, int32 Level = 1);

    /**
     * Attempts to activate the ability bound to the given slot.
     * Called by input handling in the player controller.
     * Returns false if no ability is bound, ability is on cooldown, or
     * cost can't be paid.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Abilities")
    bool TryActivateAbilityBySlot(EGothicAbilitySlot Slot);

    /**
     * Returns the cooldown remaining (0 if ready) for a given slot.
     * Used by the HUD to draw cooldown overlays.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Abilities")
    float GetCooldownRemainingForSlot(EGothicAbilitySlot Slot) const;

    /**
     * Returns the total cooldown duration for the active cooldown on a slot.
     * 0 if no cooldown is active. Used by HUD for fill-percentage calculation.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Abilities")
    float GetCooldownTotalForSlot(EGothicAbilitySlot Slot) const;

    /**
     * Called by the PlayerController when an input tag fires.
     * GAS uses Gameplay Tags for input rather than raw key codes —
     * this keeps abilities portable across input devices.
     */
    
    /**
 * Registers an ability handle to a slot for HUD cooldown polling.
 * Called by UGothicAbilitySet after granting each ability.
 */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Abilities")
    void RegisterAbilitySlot(EGothicAbilitySlot Slot, FGameplayAbilitySpecHandle Handle);
    void AbilityInputTagPressed(const FGameplayTag& InputTag);
    void AbilityInputTagReleased(const FGameplayTag& InputTag);

protected:
    /** Maps slot enum → ability spec handle for quick input lookup. */
    TMap<EGothicAbilitySlot, FGameplayAbilitySpecHandle> SlotToAbilityMap;
};