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
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Read, "State.Read",
		"The Read's CASTER window (GA_Read applies GE_ReadState, a duration GE). Drives the HUD proc "
		"icon via AGothicPlayerCharacter::IsReadActive and NOTHING else — the damage payoff is gated on "
		"the TARGET's State.Read.Marked, so this tag on the player buffs nothing. The ASC lives on the "
		"PlayerState and outlives the pawn (gotcha #4), so it is cleared in OnDeath like State.Sprinting.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Read_Marked, "State.Read.Marked",
		"Worn by the single enemy a player has Read (GA_Read applies GE_ReadMark, a duration GE that "
		"self-expires on the target's death). GA_Fire multiplies a VITAL hit on a marked target by its "
		"ReadVitalDamageMultiplier. The enemy ASC replicates in Minimal mode, so the tag reaches co-op "
		"clients for the glow (AGothicEnemyBase::HandleReadMarkTagChanged) and the server for the math.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Reloading, "State.Reloading",
		"The player is mid-reload — worn for the press-to-release hold (OnReloadPressed → OnReloadReleased "
		"and every interrupt path). The pack surge decorator (PR #69) reads it on the SERVER to open the "
		"vulnerability window, so it is applied on the authority ASC and, like State.Sprinting, cleared on "
		"every end path and on fresh-pawn cleanup (the ASC lives on the PlayerState and outlives the pawn).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Selah, "State.Selah",
		"The player is inside a Selah moment (bSelahMomentLock). The pack surge decorator (PR #69) reads it "
		"on the SERVER. Applied on the authority ASC in TriggerSelahMoment and cleared in EndSelahMomentLock "
		"and on fresh-pawn cleanup, since the ASC outlives the pawn (gotcha #4).");

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

	// ── Tutorial hints ───────────────────────────────────────────────────────
	// The DevComment on each is the TRIGGER — where the hint is raised from —
	// because the copy itself lives on the hint manager's HintCopy map where the
	// user can redline it, and duplicating it here would guarantee the two drift.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Move, "Hint.Move",
		"Opening beat. Raised by the hint manager's opener timer if the player has not moved yet.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Look, "Hint.Look",
		"Opening beat. Raised by the opener timer if the player has not looked yet.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Jump, "Hint.Jump",
		"Opening beat. Raised by the opener timer once Move and Look are done with.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Sprint, "Hint.Sprint",
		"Opening beat. Raised by the opener timer, last of the four.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_SprintLowersGun, "Hint.SprintLowersGun",
		"Raised the first time the player sprints — teaches the opportunity cost, not the key.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Fire, "Hint.Fire",
		"Level-driven (AGothicHintTrigger). No C++ trigger — firing needs a target in front of you.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Aim, "Hint.Aim",
		"Level-driven (AGothicHintTrigger).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Melee, "Hint.Melee",
		"Level-driven (AGothicHintTrigger) — placed where the fight closes to melee range.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Reload, "Hint.Reload",
		"Raised when the magazine empties (AGothicPlayerCharacter::ConsumeRound). Dismissed by a manual reload.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_SteadfastConvert, "Hint.SteadfastConvert",
		"Raised the first time Steadfast reaches its ceiling — the resource is there to be spent.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Interact, "Hint.Interact",
		"Raised the first time any interactable raises the HUD's interact prompt.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Inventory, "Hint.Inventory",
		"Raised on the first item collected (UGothicInventoryComponent::OnItemAdded).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Equip, "Hint.Equip",
		"Raised on the first equip (UGothicInventoryComponent::OnItemEquipped).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_WeaponSwap, "Hint.WeaponSwap",
		"Raised when a SECOND weapon slot becomes armed — there is nothing to swap to before that.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_AbilitySlicer, "Hint.AbilitySlicer",
		"Level-driven (AGothicHintTrigger).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_AbilityRead, "Hint.AbilityRead",
		"Level-driven (AGothicHintTrigger).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_AbilityLunge, "Hint.AbilityLunge",
		"Level-driven (AGothicHintTrigger).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Reckoning, "Hint.Reckoning",
		"Raised when SuperMeter first reaches full. The Glen's hint trigger fills the meter so this is always reachable.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Hint_Collect, "Hint.Collect",
		"Level-driven (AGothicHintTrigger) — placed on the first meditation encounter.");

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