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
     *
     * BlueprintCallable so the dev harness can build the same context: the
     * combat driver's apply_damage used stock MakeEffectContext and therefore
     * reproduced, from Python, the exact AttackPower-0 defect this function
     * exists to fix.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|GAS")
    static FGameplayEffectContextHandle MakeDamageContext(
        UAbilitySystemComponent* SourceASC,
        AActor* SourceAvatar);

    /**
     * Static convenience — applies a GE from one ASC to another without building a
     * full spec inline. Non-damage effects (stuns, debuffs) go through here.
     *
     * SourceActor is the attacker and must be the actor that CAUSED the effect: the
     * spec is made off its ASC through MakeDamageContext, so the instigator is the
     * attacker. Passing the target here names the victim as its own instigator,
     * which is what this used to do.
     */
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
     * Applies Tag as a self-managed timed loose tag: takes exactly ONE loose count
     * the first time a window opens and schedules its removal after Duration, on the
     * WORLD timer manager rather than any ability instance's.
     *
     * Two properties this exists for:
     *   - It OUTLIVES the ability. GA_Fire and GA_HuntersStrike's instant path
     *     EndAbility() on the same frame they fire; a timer set on the ability
     *     instance would be torn down with it and the tag would either never clear
     *     or (worse) never open long enough for a deferred BT re-check to see it.
     *     The world timer manager and this ASC (on the PlayerState) both survive the
     *     pawn, so the removal is guaranteed to run — see ClearTimedLooseTags for the
     *     one case (a mid-window respawn) the timer alone does not need to cover but
     *     is swept anyway.
     *   - It NEVER STACKS the count. A re-apply while the window is still open just
     *     restarts the timer, so rapid fire cannot leak counts. Because it is a
     *     count-based +1/-1 (not an absolute SetLooseGameplayTagCount), it also
     *     coexists with an ActivationOwnedTags instance of the same tag — a future
     *     montage path can hold State.Attacking through ActivationOwnedTags while
     *     this window's +1/-1 rides alongside without stripping it.
     *
     * Server-side intent: reactive enemy affixes read the player tag on the
     * authority, so call this on the authority ASC (loose tags do not replicate).
     */
    void ApplyTimedLooseTag(const FGameplayTag& Tag, float Duration);

    /**
     * Cancels every in-flight timed loose-tag window (its pending removal timer and
     * the one count it holds). Called from the fresh-pawn cleanup sweep so a window
     * that was open when a pawn died cannot ride its stale count into the next life —
     * the same outlives-the-pawn hazard State.Dead/State.Sprinting are swept for.
     */
    void ClearTimedLooseTags();

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

private:
    /**
     * Sprint's opportunity cost, applied at the ability input choke point: if this
     * input tag maps to any ability that is NOT the Primary Fire slot, the avatar's
     * sprint ends before the activation runs. No-op on a non-player avatar.
     *
     * Primary Fire is excluded on purpose — the gun is BLOCKED during a sprint
     * (UGA_Fire::CanActivateAbility), not a way to cancel out of one.
     */
    void CancelSprintForNonGunInput(const FGameplayTag& InputTag);

    /** Timer callback for ApplyTimedLooseTag — removes the window's single loose
     *  count and forgets the handle. Bound with CreateUObject so a destroyed ASC
     *  simply never fires it. */
    void HandleTimedLooseTagExpired(FGameplayTag Tag);

    /** One in-flight removal timer per open window, keyed by tag. Plain member (not
     *  a UPROPERTY): FGameplayTag/FTimerHandle are value types with no UObject to
     *  keep alive, same as LastObservedCooldownTotals below. */
    TMap<FGameplayTag, FTimerHandle> TimedLooseTagHandles;

public:

    /**
     * How many ability slots currently point at a spec handle. Telemetry only —
     * the GASInit line used to print a weapon-slot count under the name "slots",
     * next to an ability count, which read as "8 abilities went into 1 slot".
     * The two numbers were never about the same thing; this is the one that is.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Abilities")
    int32 GetRegisteredAbilitySlotCount() const { return SlotToAbilityMap.Num(); }

    /**
     * True if the ability bound to this slot is LocalPredicted or LocalOnly.
     *
     * Exists so a caller can tell the two meanings of TryActivateAbilityBySlot's
     * `true` apart. UAbilitySystemComponent::TryActivateAbility defaults
     * bAllowRemoteActivation to true, and on an authoritative ASC whose avatar is
     * NOT locally controlled it answers a Local* ability by firing
     * ClientTryActivateAbility at the owning client and returning true
     * unconditionally (AbilitySystemComponent_Abilities.cpp:1621-1627) — nothing
     * ran here. A ServerInitiated ability in the same situation genuinely does
     * run, and also answers true. Only the ability's net policy separates them.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Abilities")
    bool IsSlotAbilityLocallyPredicted(EGothicAbilitySlot Slot) const;

    /**
     * DEV/HARNESS ONLY, EXPERIMENTAL AND UNPROBED. Activates a slot on the NEXT
     * TICK instead of inline.
     *
     * The point is to escape FEditorScriptExecutionGuard. Editor Python runs
     * inside that guard, which sets GAllowActorScriptExecutionInEditor, and
     * AActor::GetFunctionCallspace then answers Local for every RPC before it
     * looks at the net role — which is why direct client-world activation
     * recurses (see TryActivateAbilityBySlot's tripwire) and why the server-world
     * route's ClientTryActivateAbility executes in process instead of going over
     * the wire. A zero-second timer fires after the Python call has returned and
     * the guard has left scope, so callspace resolves normally and a client-world
     * ASC's LocalPredicted path sends a REAL server RPC.
     *
     * The tripwire in TryActivateAbilityBySlot is left in place and is expected
     * NOT to fire from here: GAllowActorScriptExecutionInEditor is false by the
     * time a timer callback runs, so the `!IsOwnerActorAuthoritative() &&
     * GAllowActorScriptExecutionInEditor` conjunction is false. That reasoning is
     * unverified in PIE — nothing calls this yet, deliberately.
     *
     * Fire-and-forget: the result lands a frame later, so there is nothing to
     * return. Compiled to a no-op outside editor builds.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|GAS|Dev")
    void DevDeferredTryActivateAbilityBySlot(EGothicAbilitySlot Slot);

protected:
    /**
     * Rebuilds SlotToAbilityMap from the replicated ability list.
     *
     * SlotToAbilityMap is only ever written by GrantStartupAbilities, which
     * early-returns without authority, so on a client the map stayed empty
     * forever: TryActivateAbilityBySlot fell straight through to its silent
     * `return false`, and the HUD's cooldown overlay — which polls
     * GetCooldownRemainingForSlot / GetCooldownTotalForSlot through the same map
     * — read nothing on every non-host player. The specs themselves DO replicate
     * (this ASC is replicated, Full mode), and each one carries its ability CDO,
     * so the slot is recoverable client-side from GetAbilitySlot().
     *
     * Rebuilt from scratch rather than merged, so a revoked ability drops out of
     * the map on the client the same way it does on the server.
     */
    virtual void OnRep_ActivateAbilities() override;

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