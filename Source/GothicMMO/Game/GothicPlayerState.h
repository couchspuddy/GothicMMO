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
#include "GameplayTagContainer.h"
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

    // ── Revive channel ───────────────────────────────────────────────────────
    // A living player holds interact over a downed one for ReviveChannelSeconds.
    // The clock runs on the SERVER (AGothicPlayerCharacter drives it); what lives
    // here is the replicated readout, and it lives on the PlayerState for the same
    // reason bIsDowned does — the pawn is the thing most likely to churn, and a
    // PlayerState is always relevant to every client, so BOTH the reviver's HUD and
    // the downed player's HUD can read the same number without either of them
    // needing the other's pawn to be net-relevant.
    //
    // The channel is recorded on the DOWNED player's PlayerState (bReviveChannelActive
    // / ReviveChannelProgress / ReviveChannelReviver) and the reviver's PlayerState
    // carries only a back-pointer (ReviveChannelTarget). That direction is what
    // makes "one reviver per body" a structural property rather than a rule someone
    // has to remember: the slot is occupied or it is not. A SECOND reviver joining an
    // in-progress channel is deliberately out of scope — see StartReviveChannel.

    /** True while somebody is channelling a revive on THIS player. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Downed")
    bool IsReviveChannelActive() const { return bReviveChannelActive; }

    /** 0..1 server-authoritative fill. Holds its final value after the channel ends
     *  so a bar can freeze where it broke; reset to 0 by the next channel. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Downed")
    float GetReviveChannelProgress() const { return ReviveChannelProgress; }

    /** Who is picking us up, or null. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Downed")
    AGothicPlayerState* GetReviveChannelReviver() const { return ReviveChannelReviver; }

    /** Who WE are picking up, or null. The reviver-side half. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Downed")
    AGothicPlayerState* GetReviveChannelTarget() const { return ReviveChannelTarget; }

    /**
     * How the last channel on this player ended: 0 none/still running, 1 interrupted,
     * 2 completed. Replicated so a client can tell a broken bar from a finished one
     * AFTER bReviveChannelActive has already gone false — the flag alone cannot,
     * because both endings look identical from the outside.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Downed")
    uint8 GetReviveChannelResult() const { return ReviveChannelResult; }

    /** Server-only. Opens the channel slot on this (downed) player. */
    void BeginReviveChannel(AGothicPlayerState* InReviver);

    /** Server-only. Pushes the authoritative fill. No-op if no channel is open. */
    void SetReviveChannelProgress(float NewProgress);

    /** Server-only. Closes the slot and stamps the result. No-op if none is open. */
    void EndReviveChannel(bool bCompleted);

    /** Server-only. Sets/clears the reviver-side back-pointer. */
    void SetReviveChannelTarget(AGothicPlayerState* InTarget);

    /**
     * Fired on every machine whenever the channel state changes — remote clients via
     * OnRep, the authority directly from the setters above (OnRep never fires on the
     * authority, the same trap SetDowned documents).
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Downed")
    void OnReviveChannelChanged(bool bActive, float Progress, AGothicPlayerState* Reviver);


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

    /**
     * Tutorial hints this player has already been shown, for the whole session.
     *
     * Here for the same reason CachedSuperMeterOnDeath is here: the hint manager
     * is a component on the PAWN, and the pawn is destroyed on every death. A
     * seen-set held on the pawn would re-teach a revived player how to walk.
     *
     * NOT replicated. Hints are drawn on the owning client and raised there, so
     * the only copy that matters is the one on the machine doing the drawing;
     * pushing it to the server would buy nothing and cost a replicated TSet.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Hints")
    bool HasSeenTutorialHint(FGameplayTag HintTag) const { return SeenTutorialHints.Contains(HintTag); }

    UFUNCTION(BlueprintCallable, Category = "Gothic|Hints")
    void MarkTutorialHintSeen(FGameplayTag HintTag) { SeenTutorialHints.Add(HintTag); }

    /** Test affordance — re-arms every hint without restarting the session. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Hints")
    void ClearSeenTutorialHints() { SeenTutorialHints.Reset(); }

    const TSet<FGameplayTag>& GetSeenTutorialHints() const { return SeenTutorialHints; }

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    /** See HasSeenTutorialHint. Owning-client state; deliberately unreplicated. */
    UPROPERTY(Transient)
    TSet<FGameplayTag> SeenTutorialHints;

    /** Authority writes this through SetDowned only. See IsDowned for why it replicates. */
    UPROPERTY(ReplicatedUsing = OnRep_IsDowned, BlueprintReadOnly, Category = "Gothic|Downed")
    bool bIsDowned = false;

    UFUNCTION()
    void OnRep_IsDowned();

    // ── Revive channel — replicated readout. Authority writes through the
    //    Begin/Set/End functions only. See IsReviveChannelActive.
    UPROPERTY(ReplicatedUsing = OnRep_ReviveChannel, BlueprintReadOnly, Category = "Gothic|Downed")
    bool bReviveChannelActive = false;

    UPROPERTY(ReplicatedUsing = OnRep_ReviveChannel, BlueprintReadOnly, Category = "Gothic|Downed")
    float ReviveChannelProgress = 0.f;

    UPROPERTY(ReplicatedUsing = OnRep_ReviveChannel, BlueprintReadOnly, Category = "Gothic|Downed")
    TObjectPtr<AGothicPlayerState> ReviveChannelReviver;

    /** 0 none, 1 interrupted, 2 completed. See GetReviveChannelResult. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gothic|Downed")
    uint8 ReviveChannelResult = 0;

    /** The reviver-side back-pointer. Not on the OnRep — nothing draws off it,
     *  it only tells the reviver's own client which PlayerState to read. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Gothic|Downed")
    TObjectPtr<AGothicPlayerState> ReviveChannelTarget;

    /** Shared by all three channel properties — a progress update and a state
     *  change want the identical client-side response. */
    UFUNCTION()
    void OnRep_ReviveChannel();

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