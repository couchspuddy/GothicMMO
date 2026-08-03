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

    // ── Downed state ─────────────────────────────────────────────────────────
    // A downed player is ALIVE (health held at 1) but out of the fight: no enemy
    // will target them until they are revived. Nothing in this class puts anyone
    // into the state — the death pipeline calls SetDowned. This is only the
    // primitive: authoritative on the server, observable on every client.
    //
    // It lives on the PlayerState rather than the pawn because a downed player's
    // pawn is exactly the thing most likely to churn (respawn, possession change),
    // and the ASC/AttributeSet already live here for the same reason.

    /**
     * True while this player is downed. Replicated — this is the SOURCE OF TRUTH
     * for clients (the reviving player's UI reads it). The State.Downed gameplay
     * tag mirrored onto the ASC is server-side only; see SetDowned.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Downed")
    bool IsDowned() const { return bIsDowned; }

    /**
     * Server-only. Sets or clears the downed state, mirrors the State.Downed tag
     * onto the ASC for GAS consumers, and fires OnDownedChanged locally as well as
     * on remote clients (standing gotcha #3 — OnRep never fires on the authority,
     * so a listen-server host or standalone PIE would otherwise never see it).
     *
     * Modelled on AGothicGameState::SetSelahCollectPhase, which solves the same
     * problem for the Selah bar.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Downed")
    void SetDowned(bool bNewDowned);

    /**
     * Fired on every machine whenever the downed state changes — remote clients via
     * OnRep, the authority directly from SetDowned. The HUD hooks the revive prompt
     * and the downed vignette here.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Downed")
    void OnDownedChanged(bool bNowDowned);
    
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

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    /** Authority writes this through SetDowned only. See IsDowned for why it replicates. */
    UPROPERTY(ReplicatedUsing = OnRep_IsDowned, BlueprintReadOnly, Category = "Gothic|Downed")
    bool bIsDowned = false;

    UFUNCTION()
    void OnRep_IsDowned();

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