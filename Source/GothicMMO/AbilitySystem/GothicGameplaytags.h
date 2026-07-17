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
}