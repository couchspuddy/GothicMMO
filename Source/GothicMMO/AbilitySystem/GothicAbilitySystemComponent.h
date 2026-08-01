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

    /**
     * THE way a damage effect context is built in Vigil.
     *
     * GothicAttributeSet reads the attacker's AttackPower off
     * Context.GetOriginalInstigatorAbilitySystemComponent(), so the instigator
     * has to be an actor that can actually answer "what is your ASC?".
     * MakeEffectContext alone does NOT give you that: it stamps the ASC's
     * OwnerActor as instigator, and AGothicCharacterBase::InitializeGAS passes
     * GetOwner() — the Controller — for players and enemies alike. A Controller
     * has no ASC and implements no IAbilitySystemInterface, so every damage
     * site that relied on the default silently contributed AttackPower 0.
     * Measured in PIE 2026-08-01: boss claw landed 7 (15 + 0 - 8) instead of 27.
     *
     * The AVATAR is the right instigator, not the owner. It is the pawn that
     * swung, it implements IAbilitySystemInterface, and it resolves to the same
     * ASC for a player (whose ASC lives on the PlayerState) as for an enemy
     * (whose ASC lives on itself). GA_Fire had always done this by hand; this
     * makes it the one shape every site uses.
     *
     * @param SourceASC     ASC that will make the outgoing spec.
     * @param SourceAvatar  The pawn/actor that dealt the blow. Becomes both the
     *                      source object and the instigator.
     */
    static FGameplayEffectContextHandle MakeDamageContext(
        UAbilitySystemComponent* SourceASC,
        AActor* SourceAvatar);

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
     * Returns the designed total cooldown duration for a slot, whether or not a
     * cooldown is currently running — the HUD needs it as the denominator of
     * Remaining/Total on every frame, not only while the ability is recharging.
     *
     * Returns 0 only when the slot's duration has never been observed: an
     * ability with a SetByCaller duration (GA_Fire, whose interval comes from the
     * equipped weapon's fire rate) has no static magnitude to read, so its total
     * is learned from the first cooldown it runs and remembered after that.
     * Callers must still treat 0 as "unknown" and not divide by it.
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

    /**
     * Last observed full cooldown duration per slot, for abilities whose duration
     * is a SetByCaller and therefore unreadable from the GE CDO. Written while a
     * cooldown is running, read back once it ends. Mutable because the getter is
     * const and this is a cache, not state.
     */
    mutable TMap<EGothicAbilitySlot, float> LastObservedCooldownTotals;
};