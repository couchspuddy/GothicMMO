# Production Status Tracker
**Vigil — Living Project State Document**
*Last updated: July 15, 2026 — reconciled against repo after three sessions of drift (this document had not been substantively edited since July 8; the July 13 touch was the docs/ folder consolidation only). Eagle's Landing blockout and the Bestial Lucid two-phase Behavior Tree are now built and are struck from the gate. Death/respawn and the multiplayer-safe Selah encounter system added. First real PIE pass on the boss surfaced six defects, now tracked in their own section below — most notably that the vital point system has zero locations configured on any Draugr, which is why that row has moved from 🟢 back to 🟡.*

---

## Purpose

Every other document in `/docs` answers "what is this system and why." This document answers a different question: **what is the current build status of everything that's been designed, and what order should remaining work happen in.** Design docs are largely static once locked. This document is meant to change constantly — treat it as the single place that reflects the actual current state of the project, not the intended-eventually state.

**Status legend:**
- 🟢 **Built & Functional** — exists in engine, works, playtested at least informally
- 🟡 **Partially Built** — some engine work exists but incomplete
- 🔴 **Designed Only** — fully specified in a doc, zero engine implementation
- ⚪ **Not Yet Designed** — acknowledged gap, no doc exists yet
- ⚫ **Explicitly Out of Scope** — deliberately not being pursued right now

---

## Core Loop & Level

| Item | Status | Notes |
|---|---|---|
| Full vertical slice loop (Title → Character Select → Hearth → Eagle's Landing → Boss → Hearth) | 🟢 | Functional one-way loop confirmed working |
| Hearth → Eagle's Landing re-entry | 🟡 | Known bug, non-blocking, deferred |
| Eagle's Landing Encounter 1 (Edge of Town) | 🟢 | Built and playable |
| Eagle's Landing Encounter 2 (Collapsed Building / Retained ambush) | 🟢 | Built and playable |
| Eagle's Landing Encounter 3 (Plaza, interrupted Selah) | 🟢 | Built and playable |
| Eagle's Landing full blockout (Intersection → Collapsed Building → Plaza → Approach → Boss Den) | 🟢 | All five spaces blocked out. Encounter 2 split into two encounter volumes across floors; Encounter 3 carries the multi-wave interrupted Selah |
| Eagle's Landing Mini-Boss (Feral Retained) | 🟡 | Fully configured in engine — vital points, AI Perception, dedicated BT_FeralRetained (no patrol, immediate aggressive pursuit). Not yet playtested end-to-end |
| Eagle's Landing Boss (Bestial Lucid) | 🟡 | Two-phase Behavior Tree built — phase-gated Claw/Charge/Roar priority selection, three GAS abilities, per-boss MeleeAttackRange. Phase blackboard seed verified in PIE. Reached and damaged for the first time July 14. **Phase 2 has never fired** — see Known Defects |
| Death & respawn (open world) | 🟢 | Built and verified — force-leave-combat on death, enemy CombatTarget cleared, SuperMeter cached on PlayerState and restored post-respawn, HUD notified of pawn change. Corpse currently destroyed immediately (placeholder; final design defers to respawn) |
| Selah encounter system (multiplayer-safe) | 🟢 | AGothicEncounterVolume + AGothicGameState. Tracks per-encounter deaths, caches reward data at last-enemy-death, replicates a shared prompt via ActivePromptCorpse, collection via ServerCollectEncounterSelah RPC — first Server RPC in the project. Multi-wave extension via AGothicEnemySpawnPoint |
| Interaction prompt UI (text display on interactables) | 🟡 | Interaction functions; visible prompt text not implemented |

---

## Core Combat Systems

| Item | Status | Notes |
|---|---|---|
| Vital point system (shimmer, threshold shift, damage bonus) | 🟡 | **Moved back from 🟢.** C++ is functional and server-authoritative, and works on the boss. But all 18 Draugr in Eagle's Landing have an empty vital locations array — `No vital point locations defined on BP_Enemy_Draugr_C_*`, 36 warnings in a single PIE session, and `VITAL HIT` never fires once. The system is real; it is configured on nothing but the boss. Still needs a feel-tuning pass after that |
| ADS | 🟢 | Functional, smooth FOV interpolation, movement penalty |
| Camera / locomotion | 🟢 | Fixed after extended debugging session; stable |
| Steadfast (fill, hold-to-reload conversion) | 🟢 | Fully built and confirmed working — combat-state-driven fill, tap/hold reload distinction, Steadfast-to-ammo conversion via GothicSteadfastComponent |
| Ammo & Reload system | 🟢 | Built from scratch this session — magazine/reserve tracking, tap reload from reserves, hold reload converts Steadfast |
| Combat State tracking (State.InCombat) | 🟢 | GothicCombatStateComponent built, wired into damage pipeline on both dealer and receiver |
| Kill Confirmation (Event.Kill.Confirmed) | 🟢 | Wired into GothicAttributeSet, fires on confirmed kills, drives Not At All |
| Selah collection UI/prompt | 🟡 | C++ side complete — AGothicGameState exposes OnEncounterPromptActivated / OnEncounterPromptCollected as BlueprintImplementableEvents off the replicated ActivePromptCorpse OnRep. **The UMG does not exist.** No C++ work remains; this is now purely a widget task |
| Death & failure states (Contract-scale) | 🔴 | Fully designed, zero implementation |
| Death & failure states (open world) | 🔴 | Fully designed, zero implementation |
| Death & failure states (raid-scale) | ⚪ | Explicitly deferred pending raid design |

---

## Hunter Class

| Item | Status | Notes |
|---|---|---|
| The Slicer | 🟢 | Built and functional — projectile, centralized damage/stagger application via GA_Slicer |
| The Lunge | 🟢 | Built and wired — directional, fixed-distance, no target lock |
| The Read | 🟢 | Built, wired, rapid-activation delegate bug fixed |
| The Reckoning (Covenant) | 🟢 | Built and wired — GE_ReckoningState grants State.Reckoning; guaranteed-vital-hit wiring completed for OnFire. Slicer and HuntersStrike confirmed as intentionally not vital-point-interactive (Slicer is a stagger tool by design; HuntersStrike is a deliberately simple, always-available light attack — no systemic integration, minimal damage, no Resonance mechanics — consistent with Draw's role in the Warden kit as "the one deliberately simple tool") |
| The Loved and The Lost (passive) | 🟡 | C++ built; Blueprint created but RampEffect blocked — Steadfast attributes now exist, but this ability's actual regen/rate targets still need definition and a real GameplayEffect built against them |
| Not At All (passive) | 🟢 | Built and wired — stun-on-kill, Reckoning duration extension confirmed functional |
| GA_HuntersStrike | 🟢 | Built earlier in project, functional. Confirmed design intent: Hunter-specific light attack, quick and minimal damage, deliberately no interaction with vital points, stagger, or Resonance mechanics — always-available low-commitment option, not a Resonance-fork-eligible ability |
| Resonance fork variants (Gone / The Close, etc.) | 🔴 | Designed as concept and framework; specific variant kits not detailed |

---

## Other Classes

| Item | Status | Notes |
|---|---|---|
| Warden — identity, creed mapping, Prior Flame expression | 🔴 | Locked in lore, full kit now designed |
| Warden — ability kit (Held Ground, I Am the Wall, Shrapnel, Draw, Hold the Line, The Wall Remains) | 🔴 | Fully designed, zero implementation |
| Penitent — identity, creed mapping, Prior Flame expression | 🔴 | Locked in lore, full kit now designed |
| Penitent — ability kit (Be At Peace, Here Now Present, Antecedent Blood Vial, Last Rites, Recede, Grief Given Voice) | 🔴 | Fully designed, zero implementation |
| Academics — faction identity | 🔴 | Barely sketched, not launching with game |

---

## Progression & Economy

| Item | Status | Notes |
|---|---|---|
| Resonant Level (earning mechanism, milestone model) | 🔴 | Fully designed, zero implementation |
| Primary stats (Resolve / Clarity / Conviction) | 🔴 | Fully designed, zero implementation |
| Secondary/itemized stats | 🔴 | Fully designed, zero implementation |
| Resonance Fork build customization (standalone system) | 🔴 | Fully designed, zero implementation |
| Selah (currency) | 🟢 | Functional in engine (collection working) |
| Pure Selah / Pilgrimage system | 🔴 | Fully designed, zero implementation |
| Gear ceiling / imbuing / loot pools | 🔴 | Fully designed, zero implementation |
| Resonance Lottery (loot distribution) | 🔴 | Fully designed, zero implementation |
| Resonance Strain / gear sunset-reintroduction lifecycle | 🔴 | Fully designed, zero implementation |
| Live balance / telemetry philosophy | 🔴 | Fully designed as philosophy; no actual telemetry infrastructure exists |
| Sean the Binder — Selah exchange interaction | 🔴 | Placeholder NPC exists in Hearth; no functional exchange built |

---

## Party, Social & Multiplayer Structure

| Item | Status | Notes |
|---|---|---|
| Party/Kindle formation model (3-tier: open / premade / none) | 🔴 | Intent locked, zero implementation, deep design deferred to raid design phase |
| LFG / high-level content finder tool | ⚪ | Design intent noted; no detailed design yet |
| Group-scale Steadfast (per-player confirmed) | 🔴 | Design decision locked; depends on Steadfast implementation |
| Social/Hold (guild-equivalent) system | 🔴 | Designed — HOLDS_AND_SOCIAL_SYSTEMS.md. Zero implementation, correctly deferred |
| Solo activity | ⚪ | Not yet designed |
| PvP | ⚫ | Explicitly out of scope, no current plans |

---

## Art & Audio Direction

| Item | Status | Notes |
|---|---|---|
| Tone & Sensory Bible (cross-discipline governing document) | 🟢 | Locked as living document; new pairs expected as other disciplines are addressed |
| Environment art direction — Material Logic (Layer 1) | 🟢 | Fully locked as doctrine |
| Environment art direction — Spatial Logic (Layer 2) | 🟢 | Fully locked as doctrine |
| Environment art direction — Surface Treatment (Layer 3: color, lighting, texture) | ⚪ | Deferred, needs reference-image working session; should be revisited now that Tone & Sensory Bible exists above it |
| Character/enemy art direction | 🟢 | Locked — CHARACTER_ART_DIRECTION.md. Tier-based posture-as-control-loss, silhouette-first identification across all tiers, Shaped silhouettes derived from inversions, Thrall-tier pre-echo tells |
| VFX language (vital point shimmer, Selah, Prior Flame effects) | ⚪ | Dependent on Layer 3 environment work. Note: minimum placeholder hit/death VFX is a *gate* item (see checklist 4-5) and does not wait on this |
| Musical direction | 🟢 | Locked — MUSIC_DIRECTION.md. Zone-based not combat-reactive; Hearth the only major-key cue; staged content follows escalate-and-reset mirroring Selah's checkpoint structure |
| Voice acting direction | ⚪ | Not yet designed |
| Sound design doctrine | 🟢 | Locked — SOUND_DESIGN_DOCTRINE.md. Ambient never rests except during Selah; combat information as a real channel; binary-signal vital hit audio; Shaped signatures derived from each archetype's inversion |
| HUD design doctrine | 🟢 | Locked — HUD_DESIGN_DOCTRINE.md + UI_UX_DOCTRINE.md. Zone map from screen geometry, decision-speed hierarchy, horizontal peripheral reserved for contextual-only, fixed-location variable-prominence |

## Narrative & World

| Item | Status | Notes |
|---|---|---|
| Full cosmology (Lethe, Pantheon, Ember Court, Accord, Bleed, Prior Flame, Selah) | 🟢 | Fully locked in Narrative Bible |
| Accursed hierarchy (Thrall/Retained/Lucid/Hollow + Shaped archetypes) | 🟢 | Fully locked |
| The Weight (primary BBEG) | 🟢 | Fully locked, full arc designed |
| Three-act story structure | 🟢 | Fully locked at outline level |
| Eagle's Landing narrative framing (Philadelphia/City Hall) | 🟢 | Fully locked |
| Cathedral of Chains raid — encounter concepts | 🔴 | Conceptually designed in early lore sessions, not connected to current raid-token/failure-state work |

---

## Noted Future Task — Art Direction Consolidation

**Not yet built.** As of this note, Vigil's visual doctrine exists across multiple separate documents (Environment Art Direction, Tone & Sensory Bible, plus menu/title-screen direction discussed but not yet written up). This is correct for internal design use — each document is detailed and cross-referenced for good reason — but is not the right format to hand an artist unfamiliar with the project.

**When onboarding actual art collaborators**, a single, consolidated visual brief should be built first — shorter, front-loaded, absorbable in ~10 minutes, pointing to the fuller docs for anyone who wants the complete reasoning behind a given rule. This is not a replacement for the detailed doctrine, just a front door into it.

**Not urgent — flagged for whenever actual artist conversations become imminent, not before.**

---

## Known Defects — Found in Engine, Not Yet Fixed

New section as of July 15. These are distinct from the 🔴/🟡 rows above: those are *things not built yet*, these are *things built that don't work*. The distinction matters because implementation debt is predictable and defects are not — everything below was found by actually running the game, and none of it was visible from the code alone.

All six were surfaced by the first PIE pass that reached the Bestial Lucid (July 14). Ordered by how much they distort the slice, not by effort.

| Defect | Evidence | Status |
|---|---|---|
| **Zero vital points on every Draugr** | `No vital point locations defined on BP_Enemy_Draugr_C_*` × 18 unique actors, 36 warnings per PIE. `VITAL HIT` never fires. | Root-caused: component present, locations array empty in Blueprint. Pure config. **Highest priority — the core combat mechanic is absent from every trash enemy in the slice.** |
| **Boss aggro depends on her facing** | Boss sat with `Behavior: Running, Active task: None, Path following: Idle` indefinitely; began behaving correctly the moment the player stepped into her front arc. | Root-caused. `PeripheralVisionAngleDegrees = 90.f` is a *half*-angle → 180° cone, and a den boss correctly has no patrol branch to fall back on, so perception is the only ignition. Fix is not perception tuning — it's threshold-triggered `SetCombatTarget` from the den entry, making aggro a level design decision rather than an accident of mesh rotation. Also the only version that survives 8-Kindle arrival from multiple directions. |
| **No destructible zones exist in the level** | `BestialLucid AI: Found 0 destructible zones tagged 'BestialLucidZone'` | C++ is wired and correct; no actor in L_EaglesLanding carries the tag. Phase 2's timed ceiling collapse cycles an empty array. Warning only fires at Phase 2 entry, so this was invisible until now. |
| **Phase 2 has never fired** | No `advancing phase`, no `Freeze` in any log to date. | Blocked behind boss aggro. `Phase2TriggerVitalIndex = 2` is unproven — if her vital array has fewer than 3 entries or the shift order never lands on index 2, Phase 1 runs forever and reads as a balance problem rather than a wiring one. |
| **`MeleeAttackRange` may not account for capsule radii** | `IsTargetInAttackRange()` is `FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation()) <= MeleeAttackRange` — centre-to-centre, default 200cm. | Untested. Boss capsule was corrected upward July 14 to match her mesh; her centre now cannot approach the player's closer than the sum of both radii. If that sum approaches 200, the attack branch never opens no matter how correct the BT is. Per-boss MeleeAttackRange exists for exactly this; needs checking against the *new* radius. |
| **Navmesh serialized size mismatch** | `Recreating dtNavMesh instance … maxTiles (serialized: 75, 7 bits) vs calculated required (29403, 15 bits)` + `Unable to find RecastNavMesh instance while trying to create UCrowdManager instance`, every load. | Coverage of the boss den verified manually; regenerates at load, so non-blocking. But the nav data on disk was built for a level a fraction of the current size. Rebuild and re-save. |

**Also fixed July 14, recorded so the cause isn't rediscovered:** the boss took no hitscan damage while melee and Slicer worked normally. Cause was her capsule not scaling with her mesh — `OnFire`'s `LineTraceSingleByChannel(..., ECC_Pawn, ...)` was passing through empty space where the visible body was, while melee and Slicer don't use that trace and were unaffected. Capsule corrected; vital hits now land.

---

## Vertical Slice Gate Checklist — What Actually Must Be True to Call This Done

Re-scoped after a full-project inventory surfaced significant confusion between "gating requirement" and "valuable future work." This section is the single, unambiguous answer to "what's left." Everything else in this document is real and correctly documented, but does **not** block the slice.

**Standing gotcha #2 — FName lookups fail silently, everywhere.** Added July 15 after it cost most of a session. `Blackboard->SetValueAsInt(FName, ...)` and its siblings return void and no-op silently when the key doesn't exist on the asset, or exists under a different type. No compile error, no log warning, and the symptom is indistinguishable from a logic bug — a boss stuck in Phase 0 forever. This bit `CurrentPhase` on BB_BestialLucid and cost a full debugging pass. The same exposure exists for every name in the `GothicBBKeys` namespace (`TargetActor`, `TargetLocation`, `bCanSeeTarget`, `bIsInCombat`, `PatrolOrigin`, `AttackRange`), which are C++-side promises that no blackboard asset is obligated to keep — currently honoured by BB_Enemy and BB_BestialLucid, but nothing enforces it for the next blackboard. **The real fix, not yet built:** an `ensure` or `checkf` on the key IDs at `OnPossess`, so a missing key fails loudly at possess time on every enemy rather than silently at runtime on one. Until then, log `GetKeyID` (255 = key does not exist) whenever a blackboard write appears not to land.

**Standing gotcha, worth checking on every ability going forward:** discovered during tonight's cooldown debugging — the ability set was granting the raw C++ class (`GA_Read`) instead of the configured Blueprint child (`BP_GA_Read`), meaning every Blueprint-side value (cooldown effect, damage numbers, everything) was silently never reaching the running instance despite looking correctly configured in the editor. Worth explicitly re-confirming every ability in the ability set is granting its `BP_GA_X` variant, not the bare C++ class, since this exact mistake could easily have been made more than once and would produce no compile error and no obvious symptom beyond "my Blueprint changes don't seem to do anything."

### Required — the slice is not done until every item below is true

1. ~~**Eagle's Landing map rework**~~ — **DONE.** All five spaces blocked out to the locked encounter design: Intersection, two-floor Collapsed Building (split into two encounter volumes), Plaza with multi-wave interrupted Selah, Approach, Boss Den. The space the encounters were designed against now exists.
2. ~~**Bestial Lucid two-phase Behavior Tree**~~ — **DONE (built, not yet proven).** BT_BestialLucid exists with phase-gated Claw/Charge/Roar priority selection, three GAS abilities, and tagged destructible zones wired to Phase 2 zone collapse in C++. Phase 1 blackboard seed verified in PIE (`KeyID=1, Phase=1`). Struck from the gate because the build task is complete — **but Phase 2 has never actually fired in engine.** That verification lives in item 10 (end-to-end playtest), not here, and two known defects sit in front of it. Do not read this strike as "the two-phase fight works."
3. ~~**Guaranteed-vital-hit wiring for The Reckoning**~~ — **DONE.** `IsReckoningActive()` added to `GothicPlayerCharacter`, wired into `OnFire`'s vital hit check via `IsReckoningActive() || VitalPoint->IsVitalPointHit(...)`. Confirmed during design review that Slicer and Hunter's Strike do not check vital points at all, by design — Slicer is a stagger tool, not a precision tool; Hunter's Strike is a deliberately simple, always-available light attack with no systemic integration (minimal damage, no vital/stagger/Resonance interaction, consistent with Draw's role as "the deliberately simple tool" in the Warden kit) — so `OnFire` was the only place this wiring was actually needed. Scope was correctly narrowed rather than force-fit onto abilities that don't use vital detection.
4. **Minimum weapon/ability visual and audio feedback** — confirmed currently missing or unconfirmed: muzzle flash, projectile/tracer visibility, hit impact feedback. A shooter with no visible feedback on firing is not a playable slice regardless of how correct the underlying damage math is. Placeholder-quality VFX/SFX is acceptable; total absence is not.
5. **Enemy death feedback beyond ragdoll** — confirmed currently ragdoll-only. At minimum needs some visual marker (blood, a death VFX beat) before the Selah prompt appears, so the kill itself registers before the reward does.
6. **Selah collection UI** — **scope narrowed.** The C++ is done: AGothicGameState replicates ActivePromptCorpse and fires OnEncounterPromptActivated / OnEncounterPromptCollected on every client. What remains is the widget itself. The loop still has no visible collection moment — the most important five seconds in the game currently render as nothing — but this is now a UMG task with no engineering in front of it.
7. **Death/failure state implementation (Contract-scale)** — fully designed, zero implementation.
8. ~~**Steadfast tier logic (even single-weapon)**~~ — **DONE.** `HoldReload()` rewritten to select the highest tier the player currently has enough Steadfast for (10/30/60 placeholder thresholds, tune to feel), replacing the old hardcoded flat 20/6 values. Matches the "chunked segments" reticle design — whatever tier is lit is what conversion yields right now.
9. ~~**HUD elements actually rendering per the UI/UX Doctrine**~~ — **Largely done, one item remains.** Direct in-engine check performed. Findings and fixes:
   - Health number display was bound to the normalized 0-1 fill value instead of raw Health/MaxHealth (showing values like ".48" instead of "48/100") — **fixed**, text now has its own binding to raw attribute values, bar fill percentage binding left untouched.
   - Steadfast reticle display did not exist — **built.** Consolidated multiple stray/duplicate reticle widgets down to one (`WBP_Crosshair_Pistol`, embedded as a child inside the main persistent HUD widget rather than as a separate top-level widget), added 3 chunked segment images bound to `SteadfastComponent::GetCurrentSteadfast()`/`GetMaxSteadfast()`, thresholds matching the 10/30/60 tier breakpoints above.
   - Ability cooldown bars were not correctly mapped to ability slots — root cause was a chain of two bugs: (1) the `OnAbilityCooldownChanged` delegate and `UpdateAbilityCooldown` were passing a raw `int32` slot index rather than the `EGothicAbilitySlot` enum, causing an off-by-two-plus misalignment once Passive1/Passive2 were added to the enum ahead of the numbered ability slots; (2) separately, the ability set was granting the raw C++ `GA_Read` class rather than the configured `BP_GA_Read` Blueprint, meaning Blueprint-side cooldown configuration was silently never reaching the running ability instance. Both fixed — cooldown delegate chain now passes the enum end-to-end, `Switch on EGothicAbilitySlot` used in place of `Switch on Int`, ability set corrected to grant Blueprint variants.
   - **Remaining:** scale/opacity pass (HUD reads too small/transparent even at full screen) — explicitly left as a solo visual-tuning task, not requiring further design input.
10. **Mini-boss and boss end-to-end playtest** — not a build task; requires items 1-9 above to be meaningful.
11. **Playtest with a person who isn't you** — the actual finish line.

### Explicitly Not Gating — valuable, correctly deferred, does not block the slice

- **Weapon slot expansion (Primary/Secondary/Heavy as three distinct weapons)** — Steadfast's design doesn't require three weapons to exist to be internally coherent, only for the tier *contrast* to be demonstrable. One weapon with correct tier logic (item 8 above) satisfies the slice; a second and third weapon are a stretch goal, not a requirement, unless the explicit goal of the demo becomes "show weapon variety" specifically.
- Warden and Penitent implementation
- Full sound design doctrine (beyond the minimum feedback in item 4)
- Menu doctrine beyond the title screen
- Solo activity, Holds, economy, progression implementation
- Everything else in this document not listed under Required above

---

## Prior Priority Notes (Superseded by Checklist Above, Retained for Context)

All three launch classes (Hunter, Warden, Penitent) are fully designed at the kit level. The Hunter kit is now essentially complete in engine (6 of 6 abilities built, 1 partially blocked on The Loved and The Lost's RampEffect, itself non-gating since it's a passive refinement rather than a core-loop requirement). Warden and Penitent implementation should follow the same proven pattern — C++ ability classes staged ahead of engine sessions, Blueprint wiring done in-session — once the slice gate above is cleared. Economy implementation, progression systems, and raid design remain **correctly deferred** — not neglected, just genuinely lower priority than closing the loop between "fully designed" and "fully playable" for what already exists.

---

## How to Use This Document

Update the status table whenever something moves categories — that's the entire job of this document. It is not meant to be exhaustively rewritten each session; it's meant to be a quick, honest snapshot you or anyone else can glance at and immediately understand: what's real right now, versus what's well-designed but still theoretical.

If a feeling of "are we missing something foundational" comes up again, check here first before opening a new design conversation. If everything relevant to the question already has a row in this table with a status, the feeling is very likely about the 🔴/🟡 gap (implementation debt) rather than a genuine ⚪ gap (missing design). Only ⚪ rows represent real unaddressed design gaps.

---

*Document created: July 2026*
*Session: Vigil Design — Production Status & Foundations*
