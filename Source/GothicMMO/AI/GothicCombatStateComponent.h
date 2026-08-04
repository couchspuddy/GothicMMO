// GothicCombatStateComponent.h
// Tracks whether a character is currently "in combat" and manages the
// State.InCombat GameplayTag accordingly. Entering combat is immediate
// (on dealing or taking damage). Leaving combat requires a grace period
// of no combat activity, to avoid combat state flickering on/off between
// hits during a single fight.
//
// This is a small, general-purpose foundational system — any future
// ability, UI element, or AI behavior that needs to know "is this
// character currently fighting" should read State.InCombat rather than
// re-deriving combat status independently.
//
// Usage: attach to GothicCharacterBase (or GothicPlayerCharacter /
// GothicEnemyBase specifically, whichever fits your hierarchy).
// Call NotifyCombatAction() from wherever damage is dealt or received
// (OnFire, GA_HuntersStrike's melee trace, GothicAttributeSet's
// PostGameplayEffectExecute on health decrease, etc.)

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "GothicCombatStateComponent.generated.h"

UCLASS(ClassGroup = (Gothic), meta = (BlueprintSpawnableComponent))
class GOTHICMMO_API UGothicCombatStateComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGothicCombatStateComponent();

    virtual void BeginPlay() override;

    /**
     * Call this any time the owning character deals or receives damage.
     * Immediately (re)enters combat state and resets the exit-grace timer.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Combat")
    void NotifyCombatAction();

    UFUNCTION(BlueprintPure, Category = "Gothic|Combat")
    bool IsInCombat() const { return bInCombat; }
    
    // GothicCombatStateComponent.h — add to public section
    /**
     * Immediately exits combat state, bypassing the normal exit grace period.
     * Call this on death — a dead character shouldn't be considered "in combat"
     * for the several seconds the grace period would otherwise wait.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Combat")
    void ForceLeaveCombat();

protected:
    /** Tag applied to the owning ASC while in combat. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Combat")
    FGameplayTag InCombatTag;

    /** Seconds of no combat activity before leaving combat state. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Combat")
    float CombatExitGracePeriod = 5.f;

private:
    bool bInCombat = false;
    FTimerHandle ExitGraceTimerHandle;

    UPROPERTY()
    TObjectPtr<class UAbilitySystemComponent> CachedASC;

    /**
     * Returns the owner's ASC, resolving it lazily if BeginPlay could not.
     *
     * BeginPlay runs inside SpawnActor, which on a RESPAWNED pawn (and on a
     * client-side pawn at join) is BEFORE PossessedBy/InitGASFromPlayerState has
     * set the character's ASC pointer. A single BeginPlay-time cache is null
     * forever on those pawns, and State.InCombat then never went on or off.
     */
    class UAbilitySystemComponent* ResolveASC();

    /** Keeps the "no ASC" warning to one line per component, not one per call. */
    bool bWarnedNoASC = false;

    void EnterCombat();
    void OnExitGraceExpired();
};