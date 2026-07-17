// GothicGameplayTags.cpp

#include "AbilitySystem/GothicGameplayTags.h"

namespace GothicTags
{
	// ── States ───────────────────────────────────────────────────────────────
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead",
		"Character is dead. Blocks ability activation; applied in OnDeath.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Stunned, "State.Stunned",
		"Character is stunned (Not At All stun-on-kill, future CC). Blocks ability activation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attacking, "State.Attacking",
		"Owned for the duration of a melee swing (Hunter's Strike).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Reckoning, "State.Reckoning",
		"The Reckoning is active. GA_Fire reads this off the ASC for guaranteed vital hits — the ASC lives on the PlayerState, so the tag is its state, not the pawn's (gotcha #4).");

	// ── SetByCaller data channels ────────────────────────────────────────────
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Damage, "Data.Damage",
		"SetByCaller channel for damage magnitude on damage GEs.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Selah, "Data.Selah",
		"SetByCaller channel for Selah award magnitude on GE_SelahGain.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_SuperMeter, "Data.SuperMeter",
		"SetByCaller channel for SuperMeter gain on super-gain GEs.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_RampMagnitude, "Data.RampMagnitude",
		"SetByCaller channel for The Loved and The Lost's ramp GE magnitude.");

	// ── Events ───────────────────────────────────────────────────────────────
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Kill_Confirmed, "Event.Kill.Confirmed",
		"Sent to the killer's ASC from GothicAttributeSet on a confirmed kill. Drives Not At All.");
}