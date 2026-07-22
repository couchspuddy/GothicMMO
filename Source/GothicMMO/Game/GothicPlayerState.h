// GothicPlayerState.h
// PlayerState hosts the ASC and AttributeSet for players.
// This survives seamless travel, respawns, and controller changes.
// The character grabs pointers from here in PossessedBy/OnRep_PlayerState.
//
// In your GameMode: set PlayerStateClass = AGothicPlayerState.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "GothicPlayerState.generated.h"


class UGothicAbilitySystemComponent;
class UGothicInventoryComponent;


UCLASS()
class GOTHICMMO_API AGothicPlayerState : public APlayerState, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    AGothicPlayerState();

    // IAbilitySystemInterface
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    UFUNCTION(BlueprintPure, Category = "Gothic|PlayerState")
    UGothicAbilitySystemComponent* GetGothicASC() const { return AbilitySystemComponent; }

    UFUNCTION(BlueprintPure, Category = "Gothic|PlayerState")
    UGothicAttributeSet* GetGothicAttributeSet() const { return AttributeSet; }
    
    UFUNCTION(BlueprintPure, Category = "Gothic|PlayerState")
    UGothicInventoryComponent* GetInventory() const { return InventoryComponent; }
    
    // GothicPlayerState.h — add to public section
    /**
     * Cached SuperMeter value at moment of death, consumed once on the next
     * respawn's GAS init to restore it (SuperMeter/Reckoning progress persists
     * through death; other stats reset via GE_InitStats_Player as normal).
     * -1.f = no cached value pending.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|PlayerState")
    float GetCachedSuperMeterOnDeath() const { return CachedSuperMeterOnDeath; }

    void CacheSuperMeterOnDeath(float Value) { CachedSuperMeterOnDeath = Value; }

    /** Returns the cached value and clears it — call once, right after restoring it. */
    float ConsumeCachedSuperMeterOnDeath()
    {
        const float Value = CachedSuperMeterOnDeath;
        CachedSuperMeterOnDeath = -1.f;
        return Value;
    }

protected:
    // The ASC lives here — it is replicated and survives respawns.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Abilities")
    TObjectPtr<UGothicAbilitySystemComponent> AbilitySystemComponent;

    // The attribute set also lives here for the same reason.
    UPROPERTY()
    TObjectPtr<UGothicAttributeSet> AttributeSet;

    // Inventory — survives respawns alongside ASC and AttributeSet.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Inventory")
    TObjectPtr<UGothicInventoryComponent> InventoryComponent;
    
    // GothicPlayerState.h — add to protected section
    float CachedSuperMeterOnDeath = -1.f;
};