// GothicGameplayTags.h
// Native gameplay tag declarations for every tag referenced from C++.
//
// Why this exists: FGameplayTag::RequestGameplayTag(FName("State.Daed")) — note
// the typo — compiles clean and fails at runtime with, at best, a one-line
// ensure buried in the log. That is the same silent-FName failure family as
// standing gotcha #2 (Blackboard keys), which already cost a full session.
// Native tags kill the whole class: GothicTags::State_Dead is a compile error
// when misspelled, autocompletes in Rider, and answers find-all-references.
//
// Rules going forward:
//   - Any tag C++ touches gets declared here and defined in the .cpp.
//   - RequestGameplayTag(FName(...)) in new code is a review failure.
//   - Tags that only Blueprints/data assets reference stay in the editor's
//     tag manager as before; this file is for the C++ surface only.
//
// These registrations are native — the tags exist without needing entries in
// DefaultGameplayTags.ini. Existing ini/editor definitions of the same names
// coexist harmlessly.

#pragma once

#include "NativeGameplayTags.h"

namespace GothicTags
{
	// ── States ───────────────────────────────────────────────────────────────
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Downed);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Stunned);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attacking);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Reckoning);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Sprinting);

	// Reactive "action tags" — short timed loose windows applied to the PLAYER ASC
	// so reactive enemy affixes (Evader/Retaliator/Harasser) have an observable
	// trigger window to gate on. An instant ability (GA_HuntersStrike's fallback,
	// GA_Fire) resolves and EndAbility()s in one frame, so its ActivationOwnedTags
	// live one frame and a deferred BT re-check always misses them — these outlive
	// at least one re-evaluation. Applied and self-removed via
	// UGothicAbilitySystemComponent::ApplyTimedLooseTag (world-timer anchored so the
	// removal fires after the ability is gone; cleared on respawn like State.Dead).
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Firing);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Whiffed);

	// Enemy combat state. Set on the enemy ASC by the AI layer and read every
	// frame by UGothicEnemyAnimInstance to drive the combat-idle blend. Replicated
	// tag, so remote clients get it without an extra RPC.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_InCombat);

	// The Read (GA_Read). State_Read is the caster window, State_Read_Marked rides
	// the single prey. Both existed as string-lookup tags before this — formalised
	// here per the file's own rule (RequestGameplayTag(FName(...)) is a review
	// failure). Existing ini/editor definitions of the same names coexist harmlessly.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Read);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Read_Marked);

	// Player-side vulnerability windows the pack surge decorator (PR #69) reads on
	// the server. State_Reloading rides the reload press-to-release hold;
	// State_Selah rides the Selah-moment lock. Applied on the authority ASC.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Reloading);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Selah);

	// ── SetByCaller data channels ────────────────────────────────────────────
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Selah);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_SuperMeter);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_RampMagnitude);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Cooldown);

	// Marks a damage spec as a VITAL hit so the choke point
	// (UGothicAttributeSet::PostGameplayEffectExecute) can branch on it — the flinch
	// pass reads it to pick the vital threshold, the shape pass to pick the vital
	// multiplier. Carried as a 1/0 SetByCaller magnitude rather than a context flag
	// because nothing subclasses FGameplayEffectContext here; vitals are adjudicated
	// upstream in GA_Fire (the only site that resolves them today) and the number is
	// otherwise invisible to the attribute set. No GE consumes it as a modifier.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_VitalHit);

	// ── Events ───────────────────────────────────────────────────────────────
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Kill_Confirmed);

	// Anim-notify event raised at a montage's swing contact frame
	// (AnimNotify_SendGameplayEvent). UGothicGameplayAbility waits on it to open
	// the melee hit window mid-montage.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Montage_HitWindow);

	// ── Tutorial hints ───────────────────────────────────────────────────────
	// Identity for one teachable action, and nothing more. The tag is the key
	// into the hint manager's per-session seen-set and into its copy map; it
	// carries no state and is never applied to an ASC.
	//
	// Named for the ACTION taught, not for the trigger that raises it — several
	// of these fire from more than one place (Reload from the empty magazine and
	// from a level trigger both), and a trigger-named tag would have to be
	// renamed the first time a second call site appeared.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Move);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Look);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Jump);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Sprint);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_SprintLowersGun);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Fire);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Aim);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Melee);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Reload);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_SteadfastConvert);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Interact);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Inventory);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Equip);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_WeaponSwap);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_AbilitySlicer);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_AbilityRead);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_AbilityLunge);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Reckoning);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hint_Collect);

	// ── Weapon perks (WEAPON_PERK_TABLES.md) ─────────────────────────────────
	// Rolled at drop onto FGothicItemInstance::WeaponPerks, three per Resonant or
	// Pure weapon — one from each bucket. The bucket parents are declared so a
	// container query can ask "does this weapon carry any Verb-A perk" without
	// enumerating leaves, and so the catalog asset's bucket field and the tag
	// name agree by construction.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_FineTune);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbA);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbB);

	// Fine-Tune bucket — numeric-only, one rolled per Resonant/Pure weapon.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_FineTune_DeadHand);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_FineTune_TrueBore);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_FineTune_QuickHands);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_FineTune_DeepReserves);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_FineTune_ExtendedMagazine);

	// Verb Bucket A — in-fight behaviour.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbA_Jolt);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbA_Drumbeat);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbA_SteadyRead);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbA_MovingTarget);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbA_MarksmansDue);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbA_Kindling);

	// Verb Bucket B — economy and utility.
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbB_WellTended);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbB_CharitableToll);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbB_FrugalHand);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbB_Overcharge);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbB_SpentWell);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbB_MuffledWork);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Perk_Weapon_VerbB_DreadReport);
}