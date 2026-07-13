// GothicAbilitySet.h
// Data asset that defines a set of abilities to grant to a character.
// Replaces hardcoded ability tags in C++ constructors.
// Assign in BP_GothicPlayerCharacter under Startup Ability Sets.
//
// Lyra pattern: ability configuration lives in data, not code.
// Adding a new ability requires no recompile — just a new data asset entry.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "GothicAbilitySet.generated.h"

class UGothicGameplayAbility;
class UGameplayEffect;

/**
 * One entry in an ability set.
 * Pairs an ability class with the input tag that activates it
 * and the slot it occupies in the ability system.
 */
USTRUCT(BlueprintType)
struct FGothicAbilitySetEntry
{
    GENERATED_BODY()

    /** The ability class to grant. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
    TSubclassOf<UGothicGameplayAbility> AbilityClass;

    /** The gameplay tag that activates this ability via input. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
    FGameplayTag InputTag;

    /** The slot this ability occupies in the HUD and slot map. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
    EGothicAbilitySlot AbilitySlot = EGothicAbilitySlot::Ability1;

    /** Ability level when granted. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
    int32 AbilityLevel = 1;
    
    /** If true, this ability is activated immediately when granted, rather than
 *  waiting for input. Use for passives — anything with no InputTag. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
    bool bActivateOnGranted = false;   // <-- new
};

/**
 * Collection of abilities granted together as a set.
 * One set per character archetype — Hunter set, Warden set, Draugr set etc.
 * Multiple sets can be granted to one character simultaneously.
 */
UCLASS(BlueprintType)
class GOTHICMMO_API UGothicAbilitySet : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /**
     * Grants all abilities in this set to the given ASC.
     * Stores handles so abilities can be removed later if needed.
     * Call on server only — abilities replicate down to clients.
     *
     * @param ASC           The ability system component to grant to
     * @param SourceObject  The object granting these abilities (usually the character)
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Abilities")
    void GiveToAbilitySystem(
        UGothicAbilitySystemComponent* ASC,
        UObject* SourceObject = nullptr) const;

    /** The abilities defined in this set. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
    TArray<FGothicAbilitySetEntry> GrantedAbilities;

    /** Gameplay effects applied when this set is granted (e.g. stat buffs). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
    TArray<TSubclassOf<UGameplayEffect>> GrantedEffects;
};