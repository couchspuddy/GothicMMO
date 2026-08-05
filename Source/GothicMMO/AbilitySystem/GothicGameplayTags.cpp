// GothicGameplayTags.cpp

#include "AbilitySystem/GothicGameplayTags.h"

namespace GothicTags
{
	// ── States ───────────────────────────────────────────────────────────────
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead",
		"Character is dead. Blocks ability activation; applied in OnDeath.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Downed, "State.Downed",
		"Player is downed — alive but out of the fight, awaiting revive. SERVER-SIDE "
		"CONVENIENCE ONLY: applied as a loose tag from AGothicPlayerState::SetDowned so "
		"GAS consumers (ActivationBlockedTags, future GEs) can read it on the authority. "
		"Loose tags do NOT replicate — clients must read AGothicPlayerState::IsDowned().");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Stunned, "State.Stunned",
		"Character is stunned (Not At All stun-on-kill, future CC). Blocks ability activation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attacking, "State.Attacking",
		"Owned for the duration of a melee swing (Hunter's Strike).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Reckoning, "State.Reckoning",
		"The Reckoning is active. GA_Fire reads this off the ASC for guaranteed vital hits — the ASC lives on the PlayerState, so the tag is its state, not the pawn's (gotcha #4).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Sprinting, "State.Sprinting",
		"The player is sprinting. Applied as a loose tag from AGothicPlayerCharacter::SetSprinting "
		"on every start path and cleared on every end path (key release, aim, death). Blocks GA_Fire; "
		"the non-gun ability input path cancels the sprint rather than being blocked by it. "
		"Loose tags do NOT replicate, and neither does bIsSprinting — this is owning-client state, "
		"which is where the activation gate has to fire anyway.");

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

	// ── Weapon perks (WEAPON_PERK_TABLES.md) ─────────────────────────────────
	// The DevComments are the doc's effect column verbatim, because the catalog
	// asset that will carry the real magnitudes does not exist yet — until it is
	// authored these strings are the only in-engine record of what each perk does.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon, "Perk.Weapon",
		"Parent of every rolled weapon perk. Never rolled itself.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_FineTune, "Perk.Weapon.FineTune",
		"Fine-Tune bucket parent — numeric-only perks. Exactly one rolls per Resonant/Pure weapon.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbA, "Perk.Weapon.VerbA",
		"Verb Bucket A parent — in-fight behaviour. Exactly one rolls per Resonant/Pure weapon.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbB, "Perk.Weapon.VerbB",
		"Verb Bucket B parent — economy and utility. Exactly one rolls per Resonant/Pure weapon.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_FineTune_DeadHand, "Perk.Weapon.FineTune.DeadHand",
		"Recoil pitch -30%.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_FineTune_TrueBore, "Perk.Weapon.FineTune.TrueBore",
		"Yaw spread -50%.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_FineTune_QuickHands, "Perk.Weapon.FineTune.QuickHands",
		"Swap-to speed +25%.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_FineTune_DeepReserves, "Perk.Weapon.FineTune.DeepReserves",
		"+50% max reserve ammo. PILOT — wired in FGothicWeaponSlot::GetEffectiveMaxReserve().");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_FineTune_ExtendedMagazine, "Perk.Weapon.FineTune.ExtendedMagazine",
		"+25% magazine capacity, rounded up.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbA_Jolt, "Perk.Weapon.VerbA.Jolt",
		"8% chance per hit to stagger target 1s.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbA_Drumbeat, "Perk.Weapon.VerbA.Drumbeat",
		"Every 8th consecutive unmissed hit on one target staggers it. Reload does not reset the streak — only a miss or a weapon swap does.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbA_SteadyRead, "Perk.Weapon.VerbA.SteadyRead",
		"+0.25 VitalDamageMultiplier while stationary (no movement input in the last 0.5s). Vital-dependent.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbA_MovingTarget, "Perk.Weapon.VerbA.MovingTarget",
		"Vital hits while sprinting/strafing: flat +20% damage. Vital-dependent.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbA_MarksmansDue, "Perk.Weapon.VerbA.MarksmansDue",
		"A vital hit returns 1 round to the magazine. Vital-dependent, ammo-dependent, and excluded on MagazineCapacity <= 3.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbA_Kindling, "Perk.Weapon.VerbA.Kindling",
		"SuperGainOnHit +60% (5 -> 8). PILOT — wired in UGA_Fire's super-gain block.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbB_WellTended, "Perk.Weapon.VerbB.WellTended",
		"Steadfast refill restores 50% more reserve ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbB_CharitableToll, "Perk.Weapon.VerbB.CharitableToll",
		"Steadfast refill costs 1 fewer charge (minimum 1).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbB_FrugalHand, "Perk.Weapon.VerbB.FrugalHand",
		"Hold-reload always yields one ammo tier lower, at half Steadfast cost.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbB_Overcharge, "Perk.Weapon.VerbB.Overcharge",
		"Hold-reload always yields one ammo tier higher, at 1.5x Steadfast cost.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbB_SpentWell, "Perk.Weapon.VerbB.SpentWell",
		"Covenant activation instantly refills Steadfast to full. Eligible on Heavy Melee — the meter is global, only spending is weapon-scoped.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbB_MuffledWork, "Perk.Weapon.VerbB.MuffledWork",
		"Hearing-aggro radius x0.5.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Perk_Weapon_VerbB_DreadReport, "Perk.Weapon.VerbB.DreadReport",
		"Hearing-aggro radius x1.5.");
}