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
    PassiveAbility1 UMETA(DisplayName = "Passive Ability 1"),
    PassiveAbility2 UMETA(DisplayName = "Passive Ability 2"),
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

    /**
     * THE project-wide way to apply a GameplayEffect class to an ASC.
     * Replaces the MakeEffectContext → MakeOutgoingSpec → SetByCaller →
     * ApplyGameplayEffectSpecToSelf chain that was hand-copied in seven files
     * — where a fix applied to one copy (e.g. GA_Fire's July 15 instigator
     * fix) reliably failed to reach the others, because there was no single
     * place to fix.
     *
     * Static and taking the target ASC as a parameter deliberately: callers
     * hold plain UAbilitySystemComponent* (ability CachedASC members, the
     * encounter volume's player ASCs) and shouldn't need a cast to use it.
     *
     * @param ASC              The ASC to apply the effect to. Null-safe.
     * @param EffectClass      The GameplayEffect class. Null-safe.
     * @param SourceObject     Optional context source (the granting actor).
     * @param SetByCallerTag   Optional SetByCaller channel (e.g. GothicTags::Data_Selah).
     *                         Invalid tag = no SetByCaller written.
     * @param SetByCallerValue Magnitude for the SetByCaller channel.
     * @param Level            Effect level.
     * @return The active effect handle (valid for Duration/Infinite effects —
     *         store it if you need to remove or refresh the effect later, as
     *         Lunge, Reckoning, and the ramp do). Instant effects return an
     *         invalid handle by GAS design; that is not a failure.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Abilities")
    static FActiveGameplayEffectHandle ApplyEffectToASC(
        UAbilitySystemComponent* ASC,
        TSubclassOf<UGameplayEffect> EffectClass,
        UObject* SourceObject = nullptr,
        FGameplayTag SetByCallerTag = FGameplayTag(),
        float SetByCallerValue = 0.f,
        float Level = 1.f);

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