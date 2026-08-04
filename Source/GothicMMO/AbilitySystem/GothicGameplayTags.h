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

	// ── SetByCaller data channels ────────────────────────────────────────────
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Selah);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_SuperMeter);
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_RampMagnitude);

	// ── Events ───────────────────────────────────────────────────────────────
	GOTHICMMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Kill_Confirmed);

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