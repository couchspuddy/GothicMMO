// GothicCharacterBase.h
// Base class for ALL characters: players, enemies, and NPCs.
// Owns the AbilitySystemComponent and AttributeSet.
// Player characters use AGothicPlayerCharacter (subclass).
// Enemy characters use AGothicEnemyBase (subclass).
//
// KEY NETWORKING NOTE:
//   - Players: ASC and AttributeSet live on the PlayerState (standard UE5 MMO pattern).
//     This survives respawn/possession changes.
//   - Enemies: ASC and AttributeSet live directly on the Character.
//     Simpler, cheaper, and enemies don't need persistent state.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "GothicCharacterBase.generated.h"

class UGothicAbilitySystemComponent;
class UGothicAttributeSet;
class UGameplayEffect;
class UGothicGameplayAbility;

/**
 * Combat interface — implemented by all damageable characters.
 * Allows the attribute set's damage pipeline to call OnDeath without
 * knowing the concrete class (avoids circular includes).
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UGothicCombatInterface : public UInterface
{
    GENERATED_BODY()
};

class GOTHICMMO_API IGothicCombatInterface
{
    GENERATED_BODY()

public:
    /** Called when Health reaches 0. Implement death logic here. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Gothic|Combat")
    void OnDeath(AActor* Killer);
};

// =============================================================================

UCLASS(Abstract)
class GOTHICMMO_API AGothicCharacterBase : public ACharacter,
                                           public IAbilitySystemInterface,
                                           public IGothicCombatInterface,
                                            public IGenericTeamAgentInterface
{
    GENERATED_BODY()

public:
    AGothicCharacterBase();

    // -------------------------------------------------------------------------
    // IAbilitySystemInterface — GAS requires this to find the ASC on any actor.
    // -------------------------------------------------------------------------
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    /** Typed accessor — avoids casting throughout the codebase. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Abilities")
    UGothicAbilitySystemComponent* GetGothicASC() const;

    /** Typed accessor for the attribute set. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Attributes")
    const UGothicAttributeSet* GetAttributeSet() const { return AttributeSet; }

    // -------------------------------------------------------------------------
    // Attribute convenience getters (Blueprint-friendly, no cast needed)
    // -------------------------------------------------------------------------
    UFUNCTION(BlueprintPure, Category = "Gothic|Attributes")
    float GetHealth() const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Attributes")
    float GetMaxHealth() const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Attributes")
    float GetStamina() const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Attributes")
    float GetEther() const;

    UFUNCTION(BlueprintPure, Category = "Gothic|Attributes")
    bool IsAlive() const;

    // -------------------------------------------------------------------------
    // IGothicCombatInterface
    // -------------------------------------------------------------------------
    virtual void OnDeath_Implementation(AActor* Killer) override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Team")
    uint8 TeamId = 0;

    virtual FGenericTeamId GetGenericTeamId() const override
    {
        return FGenericTeamId(TeamId);
    }

protected:
    virtual void BeginPlay() override;

    /**
     * Called on server after the character is possessed (players) or
     * on BeginPlay (enemies) to initialize the GAS component.
     * Applies the initial stats effect and grants startup abilities.
     */
    virtual void InitializeGAS();

    // -------------------------------------------------------------------------
    // GAS references
    // Subclasses must set these pointers. Players set them from PlayerState;
    // enemies set them in the constructor.
    // -------------------------------------------------------------------------
    UPROPERTY()
    TObjectPtr<UGothicAbilitySystemComponent> AbilitySystemComponent;

    UPROPERTY()
    TObjectPtr<UGothicAttributeSet> AttributeSet;

    // -------------------------------------------------------------------------
    // Data — set these in Blueprint subclasses
    // -------------------------------------------------------------------------

    /**
     * GameplayEffect applied at initialization to set base attribute values.
     * Create: GE_InitStats_Player or GE_InitStats_Draugr etc.
     * Use "Override" modifiers for each attribute's starting value.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Init")
    TSubclassOf<UGameplayEffect> DefaultAttributeEffect;

    /**
     * Abilities granted to this character at spawn.
     * Designers fill this array in Blueprint with ability classes.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Init")
    TArray<TSubclassOf<UGothicGameplayAbility>> StartupAbilities;

    /** Tags to apply immediately at spawn (e.g., faction tags, AI state tags). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Init")
    FGameplayTagContainer StartupTags;
};
