// GothicHintTrigger.h
// A box you walk into that raises one tutorial hint. The placement-pass tool for
// every hint that has no natural C++ call site — Fire, Aim, Melee, the three
// abilities, Collect — because those are taught by ARRIVING somewhere, not by a
// state change the code can watch for.
//
// Fires once PER PLAYER by default and then does nothing for that player again.
// The hint manager's seen-set would already swallow a repeat, but a trigger that
// keeps overlapping keeps doing work and keeps writing log lines for a hint that
// will never show. Per player rather than per trigger because a gate is a place
// each member of the party arrives at separately — see LatchedPlayers.
//
// The Covenant fill. bFillSuperMeterOnOverlap exists for exactly one placement —
// the Glen — and the reasoning generalises: Hint.Reckoning is raised when the
// SuperMeter caps, so a player who reaches the Glen having never filled it has
// never been taught the game's signature verb. Rather than making the hint fire
// on an empty meter (teaching a locked door), the trigger fills the meter and
// lets the existing SuperMeter-full latch raise the hint on its own. The hint
// stays honest: it only ever appears when Reckoning is actually spendable.
//
// SERVER-SIDE for the fill, client-side for the hint. The meter is an attribute
// and only the authority may write it; the hint is a screen element and only the
// owning client can draw it. Both halves run off the same overlap.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "GothicHintTrigger.generated.h"

class UBoxComponent;

UCLASS()
class GOTHICMMO_API AGothicHintTrigger : public AActor
{
    GENERATED_BODY()

public:
    AGothicHintTrigger();

    virtual void BeginPlay() override;

protected:
    /**
     * The volume. Root component, so moving the actor moves the trigger; size it
     * per instance in the level — the default extent is a placeholder.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|HintTrigger")
    TObjectPtr<UBoxComponent> TriggerBox;

    /**
     * The hint to raise. Leave unset to make this a pure SuperMeter-fill volume
     * with no hint of its own — the Glen wants the fill, and lets the existing
     * full-meter latch raise Hint.Reckoning rather than raising it here.
     */
    UPROPERTY(EditInstanceOnly, Category = "Gothic|HintTrigger", meta = (Categories = "Hint"))
    FGameplayTag HintTag;

    /**
     * Fills the overlapping player's SuperMeter to MaxSuperMeter. See the file
     * header — this is the Glen's Covenant fill.
     */
    UPROPERTY(EditInstanceOnly, Category = "Gothic|HintTrigger")
    bool bFillSuperMeterOnOverlap = false;

    /** Fire once and never again. See the file header for why this is the default. */
    UPROPERTY(EditInstanceOnly, Category = "Gothic|HintTrigger")
    bool bTriggerOnce = true;

private:
    UFUNCTION()
    void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);

    /** Authority-only. Writes SuperMeter to its ceiling through the ASC's own setter. */
    void FillSuperMeter(class AGothicPlayerCharacter* Player);

    /**
     * Returns the object this trigger latches a player by: the PlayerState when
     * one is resolved, the pawn otherwise. See LatchedPlayers.
     */
    static AActor* LatchKeyFor(class AGothicPlayerCharacter* Player);

    /**
     * Who this trigger has already served. Was a single actor-level bool, which
     * made the first player through a gate consume it for everyone else in the
     * same world — in a two-player session the player who spawns ahead of the
     * host burns the Move gate at t=0 and nobody is ever taught to walk.
     *
     * Keyed on the PlayerState, not the pawn, so the latch survives a death and
     * respawn the way the hint seen-set it mirrors does. Weak, unlogged, and not
     * a UPROPERTY: entries reference nothing this actor owns, a stale one costs a
     * set slot until the world unloads, and the lifetime is unchanged from the
     * bool — world load clears it, nothing persists.
     */
    TSet<TWeakObjectPtr<AActor>> LatchedPlayers;
};
