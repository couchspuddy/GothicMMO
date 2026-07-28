// GothicBleedGate.h
// A stretch of Bleed too concentrated to walk through. Placed across the exit
// of an encounter space, it blocks the player from moving on until that
// encounter's Selah has actually been collected.
//
// Why this exists: waves 2 and 3 only spawn when the player ATTEMPTS a
// collection, so a player who cleared the first roster and simply walked away
// skipped the rest of the encounter entirely. Measured in Eagle's Landing:
// Encounter 3's 15-enemy roster resolved as one Feral Retained, because the
// player left the building without ever triggering the interrupt. Gating the
// exit rather than changing the wave trigger keeps the fiction — the city
// doesn't let you leave until you've taken what you came for — and leaves the
// interrupted-Selah fake-out exactly as designed.
//
// Blocks pawns only. The player can see and shoot through a Bleed gate; it
// stops bodies, not sightlines or bullets.
//
// Fails OPEN. A gate with nothing assigned to wait on, or one whose conditions
// are already met, never blocks — a misconfigured gate must not be able to make
// the level unfinishable.
//
// Two ways to open, and a gate may use either or both:
//   GatedEncounters    — opens once those encounters have PAID OUT. The exit you
//                        earn by finishing the fight and collecting.
//   BreakoutEncounters — opens when the occupant BREAKS OUT mid-fight. The exit
//                        she tears open for you on her way down.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GothicBleedGate.generated.h"

class AGothicEncounterVolume;
class UBoxComponent;

UCLASS()
class GOTHICMMO_API AGothicBleedGate : public AActor
{
	GENERATED_BODY()

public:
	AGothicBleedGate();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** True once the gate has dissolved and the player may pass. */
	UFUNCTION(BlueprintPure, Category = "Gothic|BleedGate")
	bool IsOpen() const { return bOpen; }

protected:
	/**
	 * The barrier. Sized per instance in the level — the default extent is a
	 * placeholder. Root component, so moving the actor moves the wall.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|BleedGate")
	TObjectPtr<UBoxComponent> Barrier;

	/**
	 * Every encounter this gate waits on. The gate opens only once ALL of them
	 * have paid out.
	 *
	 * A list rather than a single reference because one exit can serve more than
	 * one encounter — the collapsed building holds two, and a gate on its exit
	 * that waited on only one of them would let the player leave with the other
	 * unfinished. Leave empty and the gate stays open; see the fail-open note in
	 * the file header.
	 */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|BleedGate")
	TArray<TObjectPtr<AGothicEncounterVolume>> GatedEncounters;

	/**
	 * Encounters whose BREAK-OUT opens this gate, rather than their payout.
	 *
	 * This is the way on that the Feral Retained makes for you: the exit out of
	 * her upstairs room does not exist until she smashes through it, and it must
	 * open with the encounter still unfinished — she is alive, the fight is
	 * ongoing, and nothing has been collected. Waiting on the payout here would
	 * seal the player in the room she just left through.
	 *
	 * A gate may list both kinds. The conditions are ANDed: every entry in BOTH
	 * lists must be satisfied before it opens.
	 */
	UPROPERTY(EditInstanceOnly, Category = "Gothic|BleedGate")
	TArray<TObjectPtr<AGothicEncounterVolume>> BreakoutEncounters;

	/**
	 * Hook for the wall's look and sound — the Bleed haze, the audio bed, the
	 * dissolve. Fires on every machine, both when the gate first closes
	 * (bNowOpen false) and when it opens.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|BleedGate")
	void OnBleedGateStateChanged(bool bNowOpen);

public:
	// bOpen and ApplyOpenState are public so the Vigil.OpenBleedGates console
	// command can force a gate open from outside the class.

	/** Replicated so a client's barrier collision and VFX follow the server's. */
	UPROPERTY(ReplicatedUsing = OnRep_Open)
	bool bOpen = false;

	/** Applies bOpen to collision + visibility and raises the BP event. */
	void ApplyOpenState();

private:
	UFUNCTION()
	void OnRep_Open();

	/** Bound to each gated encounter's OnEncounterRewarded. */
	UFUNCTION()
	void HandleEncounterRewarded(AGothicEncounterVolume* Encounter);

	/** Bound to each break-out encounter's OnEncounterBreakout. */
	UFUNCTION()
	void HandleEncounterBreakout(AGothicEncounterVolume* Encounter);

	/** Opens the gate if every gated encounter has now paid out. */
	void RefreshOpenState();
};
