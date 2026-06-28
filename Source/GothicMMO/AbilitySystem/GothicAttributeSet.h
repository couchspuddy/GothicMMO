// GothicAttributeSet.h
// Defines all base attributes for players and enemies.
// GAS reads/writes these; never modify them directly in gameplay code.
// Add new stats here and expose to Blueprints via the macro pattern below.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GothicAttributeSet.generated.h"

// Boilerplate macro: generates Getter, Setter, and Initter for each attribute.
// Also registers the attribute with the ASC's delegate system for change callbacks.
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class GOTHICMMO_API UGothicAttributeSet : public UAttributeSet
{
    GENERATED_BODY()

public:
    UGothicAttributeSet();

    // -------------------------------------------------------------------------
    // Replication support — must list every replicated attribute here
    // -------------------------------------------------------------------------
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // -------------------------------------------------------------------------
    // Called BEFORE an attribute changes (use to clamp incoming values)
    // -------------------------------------------------------------------------
    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

    // -------------------------------------------------------------------------
    // Called AFTER a gameplay effect changes an attribute (use for death, UI, etc.)
    // -------------------------------------------------------------------------
    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

    // =========================================================================
    // VITAL ATTRIBUTES
    // =========================================================================

    /** Current health. Reaches 0 = death. Replicated for health bars. */
    UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_Health)
    FGameplayAttributeData Health;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, Health)

    /** Maximum health ceiling. Affected by level and gear. */
    UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_MaxHealth)
    FGameplayAttributeData MaxHealth;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, MaxHealth)

    /** Stamina: used for dodges, heavy attacks, sprinting. */
    UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_Stamina)
    FGameplayAttributeData Stamina;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, Stamina)

    /** Max stamina ceiling. */
    UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_MaxStamina)
    FGameplayAttributeData MaxStamina;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, MaxStamina)

    /**
     * Ether: the gothic world's equivalent of Destiny's "Light" / magic resource.
     * Used for covenant abilities and supernatural powers.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_Ether)
    FGameplayAttributeData Ether;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, Ether)

    UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_MaxEther)
    FGameplayAttributeData MaxEther;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, MaxEther)
    
    /** Super meter — fills through damage dealt and kills. 0-100 range. */
    UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_SuperMeter)
    FGameplayAttributeData SuperMeter;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, SuperMeter)

    /** Max super meter — always 100 but exposed for GAS consistency. */
    UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_MaxSuperMeter)
    FGameplayAttributeData MaxSuperMeter;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, MaxSuperMeter) 
    
    // =========================================================================
    // SELAH — crystallized essence collected from Accursed deaths
    // Never decays. Used for Binder exchanges.
    // =========================================================================

    /** Current Selah held by this character. */
    UPROPERTY(BlueprintReadOnly, Category = "Selah", ReplicatedUsing = OnRep_Selah)
    FGameplayAttributeData Selah;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, Selah)

    // =========================================================================
    // COMBAT ATTRIBUTES
    // =========================================================================

    /** Base physical damage before weapon modifiers. */
    UPROPERTY(BlueprintReadOnly, Category = "Combat", ReplicatedUsing = OnRep_AttackPower)
    FGameplayAttributeData AttackPower;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, AttackPower)

    /** Flat damage reduction (armor). */
    UPROPERTY(BlueprintReadOnly, Category = "Combat", ReplicatedUsing = OnRep_Defense)
    FGameplayAttributeData Defense;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, Defense)

    /**
     * Temporary incoming damage container.
     * GameplayEffects write to this; PostGameplayEffectExecute converts it to
     * actual Health reduction. NEVER replicated — server-authoritative only.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Combat")
    FGameplayAttributeData IncomingDamage;
    ATTRIBUTE_ACCESSORS(UGothicAttributeSet, IncomingDamage)

protected:
    // -------------------------------------------------------------------------
    // OnRep callbacks — required for GAS client-side prediction to work correctly
    // -------------------------------------------------------------------------
    UFUNCTION()
    virtual void OnRep_Health(const FGameplayAttributeData& OldHealth);

    UFUNCTION()
    virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);

    UFUNCTION()
    virtual void OnRep_Stamina(const FGameplayAttributeData& OldStamina);

    UFUNCTION()
    virtual void OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina);

    UFUNCTION()
    virtual void OnRep_Ether(const FGameplayAttributeData& OldEther);

    UFUNCTION()
    virtual void OnRep_MaxEther(const FGameplayAttributeData& OldMaxEther);

    UFUNCTION()
    virtual void OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower);

    UFUNCTION()
    virtual void OnRep_Defense(const FGameplayAttributeData& OldDefense);
    
    UFUNCTION()
    virtual void OnRep_SuperMeter(const FGameplayAttributeData& OldSuperMeter);

    UFUNCTION()
    virtual void OnRep_MaxSuperMeter(const FGameplayAttributeData& OldMaxSuperMeter);
    
    UFUNCTION()
    virtual void OnRep_Selah(const FGameplayAttributeData& OldSelah);
};
