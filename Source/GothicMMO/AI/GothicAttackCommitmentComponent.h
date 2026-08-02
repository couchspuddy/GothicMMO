// GothicAttackCommitmentComponent.h
// Where an attack commitment lives once it has to outlive a BT branch.
//
// Phase 2 kept the commitment in the weighted-select service's NodeMemory. That
// was correct for what phase 2 needed — a lock around ONE ability — and wrong
// for anything longer, for a reason that was measured rather than reasoned:
//
//   The dispatch task returns Succeeded when the ability ends. The phase
//   Selector succeeds, the parent Sequence completes, the branch unwinds, the
//   subtree is re-entered, and OnBecomeRelevant fires and zeroes NodeMemory.
//   So the service NEVER observes the "committed, and the key just cleared"
//   transition — the state it needed to see the transition died in the same
//   event that produced it. Over a full PIE session: 13 ATTACK-commit, 47
//   ATTACK-committed, and ZERO ATTACK-release.
//
// Chain state therefore cannot live in NodeMemory. It lives here, on the AI
// controller, whose lifetime is the pawn's possession rather than a subtree's
// activation. Three consequences worth stating, because each one was a reason
// this shape beat the alternatives:
//
//   - Blackboard keys are the more BT-idiomatic transport and are what
//     ChosenAttackTag already uses, but a chain needs at least a chain
//     identity and a step index, which means new keys hand-authored into
//     every Blackboard asset before ANY of this can be tested. The prerequisite
//     fix has to be provable on the tree exactly as it stands today, with no
//     editor work, or it cannot be proven before the chain logic is built on
//     top of it. That ruled the Blackboard out for the state; the CHOSEN TAG
//     still travels by Blackboard, unchanged.
//   - State directly on AGothicEnemyAIController would work, but the weighted
//     select service is deliberately generic ("no BestialLucid-specific logic
//     lives here") and is configured onto trees run by three different
//     controller classes. A component asked for by base AAIController couples
//     to none of them.
//   - The component installs itself on first use. Nothing to add in the
//     editor, nothing to forget on a new enemy, and no path where a tree is
//     configured for chains but the state carrier is missing.
//
// Lifetime is the controller's: unpossess, death and level change destroy it,
// which is exactly the set of events that should also destroy a commitment.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GothicAttackCommitmentComponent.generated.h"

class AAIController;

/** Why a commitment ended — the cause named in the CHAIN-exit timeline line. */
UENUM()
enum class EGothicChainExitReason : uint8
{
    /** Ran its last step. The only non-abnormal exit. */
    Completed,

    /** Target lost: leash break, death, disengage. */
    NoTarget,

    /**
     * The branch was away longer than the step's recovery window allowed. A
     * stagger, a phase transition, or any other event that took the subtree
     * out from under an in-flight chain long enough that resuming it would be
     * resuming a fight that has since moved.
     */
    RecoveryLapsed,

    /** The committed attack never went active — AttackDispatchTimeout. */
    DispatchTimeout,

    /** A step named a tag that is not a registered gameplay tag. */
    BadStep,

    /** Something outside this service wrote the attack key. */
    Superseded
};

/**
 * One pawn's attack commitment, and the chain it belongs to if it belongs to
 * one. Read and written only by UGothicBTService_WeightedActionSelect; the
 * dispatch task never learns that any of this exists.
 */
UCLASS()
class GOTHICMMO_API UGothicAttackCommitmentComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGothicAttackCommitmentComponent();

    /**
     * The component on this controller, creating and registering it on first
     * call. Returns null only for a null controller.
     *
     * Not a UPROPERTY lookup on the controller class on purpose — see the file
     * header. The service holds no pointer to the result between ticks either;
     * a weak lookup every tick is cheaper than reasoning about which of the
     * three controller classes tore down first.
     */
    static UGothicAttackCommitmentComponent* FindOrAdd(AAIController* Controller);

    // ── The commitment (phase 2 state, relocated verbatim) ───────────────────

    /** True between ATTACK-commit and the release that follows it. */
    bool bCommitted = false;

    /**
     * Latch, not a sample. Once the committed ability has been seen active even
     * once, AttackDispatchTimeout is disarmed for the rest of this commitment —
     * a swing between montage sections, or a chain between steps, reads
     * inactive and must never be mistaken for a dispatch that never happened.
     */
    bool bDispatched = false;

    /** When the CURRENT step was written to the key. Re-armed on every advance. */
    float CommitTime = -1.f;

    /** ActionID of the committed pool entry. Logging, and the chain lookup. */
    FName CommittedActionID = NAME_None;

    /** Full tag name written to the attack key, so release can verify identity. */
    FName CommittedTagName = NAME_None;

    // ── The chain (phase 3) ──────────────────────────────────────────────────

    /**
     * Index into the service's Chains array, or INDEX_NONE for a single attack.
     *
     * A single attack is deliberately not modelled as a one-step chain. The
     * single-attack path is the one that ships enabled on every existing tree,
     * and it has to stay byte-identical whether or not any chain is authored;
     * routing it through chain code would make "no chains authored" a different
     * code path from "chains authored but none picked".
     */
    int32 ChainIndex = INDEX_NONE;

    /** Which step of ChainIndex is currently dispatched. */
    int32 ChainStepIndex = INDEX_NONE;

    /** World time the chain was entered — the held duration on CHAIN-exit. */
    float ChainEnterTime = -1.f;

    /**
     * World time the previous step's key clear was observed. The advance
     * deadline is measured from here, so a chain that loses its subtree to a
     * stagger exits rather than resuming into a fight that has moved on.
     */
    float LastStepEndTime = -1.f;

    /** True while a chain is in flight — the thing OnBecomeRelevant must not clear. */
    bool IsChaining() const { return ChainIndex != INDEX_NONE; }

    /** Drops the chain only. The commitment fields are cleared by the caller's release. */
    void ClearChain()
    {
        ChainIndex      = INDEX_NONE;
        ChainStepIndex  = INDEX_NONE;
        ChainEnterTime  = -1.f;
        LastStepEndTime = -1.f;
    }

    /** Full reset — commitment and chain. */
    void ClearAll()
    {
        bCommitted        = false;
        bDispatched       = false;
        CommitTime        = -1.f;
        CommittedActionID = NAME_None;
        CommittedTagName  = NAME_None;
        ClearChain();
    }
};
