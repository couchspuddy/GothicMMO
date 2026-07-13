# Production Status Tracker
**Vigil — Living Project State Document**
*Last updated: July 2026 — engine session: Reckoning wiring, Steadfast tiers, HUD health/cooldown/Steadfast fixes all completed and verified

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
| Eagle's Landing Mini-Boss (Feral Retained) | 🟡 | Fully configured in engine — vital points, AI Perception, dedicated BT_FeralRetained (no patrol, immediate aggressive pursuit). Not yet playtested end-to-end |
| Eagle's Landing Boss (Bestial Lucid) | 🟡 | Blueprint created, vital point component configured (timer + threshold shift), boss AI controller architecture built in C++ (AGothicBossAIController base + BestialLucid subclass with Phase 1→2 trigger on designated vital index). Two-phase Behavior Tree not yet built |
| Interaction prompt UI (text display on interactables) | 🟡 | Interaction functions; visible prompt text not implemented |

---

## Core Combat Systems

| Item | Status | Notes |
|---|---|---|
| Vital point system (shimmer, threshold shift, damage bonus) | 🟢 | Functional, server-authoritative, needs feel-tuning pass |
| ADS | 🟢 | Functional, smooth FOV interpolation, movement penalty |
| Camera / locomotion | 🟢 | Fixed after extended debugging session; stable |
| Steadfast (fill, hold-to-reload conversion) | 🟢 | Fully built and confirmed working — combat-state-driven fill, tap/hold reload distinction, Steadfast-to-ammo conversion via GothicSteadfastComponent |
| Ammo & Reload system | 🟢 | Built from scratch this session — magazine/reserve tracking, tap reload from reserves, hold reload converts Steadfast |
| Combat State tracking (State.InCombat) | 🟢 | GothicCombatStateComponent built, wired into damage pipeline on both dealer and receiver |
| Kill Confirmation (Event.Kill.Confirmed) | 🟢 | Wired into GothicAttributeSet, fires on confirmed kills, drives Not At All |
| Selah collection UI/prompt | 🔴 | Designed (appears after encounter clear, full restock), zero implementation |
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
| Social/Hold (guild-equivalent) system | ⚪ | Named once, never designed |
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
| Character/enemy art direction | ⚪ | Not yet designed; Humanity Spectrum section of Tone & Sensory Bible provides a governing framework to build from |
| VFX language (vital point shimmer, Selah, Prior Flame effects) | ⚪ | Dependent on Layer 3 environment work |
| Musical direction | ⚪ | Not yet designed; several Tone & Sensory pairs (Reverent Not Sacred, Significant Not Celebrated) directly constrain this |
| Voice acting direction | ⚪ | Not yet designed |
| Sound design doctrine | ⚪ | Earned Quiet Not Ambient Calm pair locked; full sound doctrine document not yet written |

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

## Vertical Slice Gate Checklist — What Actually Must Be True to Call This Done

Re-scoped after a full-project inventory surfaced significant confusion between "gating requirement" and "valuable future work." This section is the single, unambiguous answer to "what's left." Everything else in this document is real and correctly documented, but does **not** block the slice.

**Standing gotcha, worth checking on every ability going forward:** discovered during tonight's cooldown debugging — the ability set was granting the raw C++ class (`GA_Read`) instead of the configured Blueprint child (`BP_GA_Read`), meaning every Blueprint-side value (cooldown effect, damage numbers, everything) was silently never reaching the running instance despite looking correctly configured in the editor. Worth explicitly re-confirming every ability in the ability set is granting its `BP_GA_X` variant, not the bare C++ class, since this exact mistake could easily have been made more than once and would produce no compile error and no obvious symptom beyond "my Blueprint changes don't seem to do anything."

### Required — the slice is not done until every item below is true

1. **Eagle's Landing map rework** — current geometry does not yet reflect the locked encounter design (Z-route collapsed building, isolated mini-boss room, boss den structural decay). No level editing has been done yet. This is gating, not polish — the encounters were designed against a space that doesn't exist yet.
2. **Bestial Lucid two-phase Behavior Tree** — Blueprint, vital point timer+threshold config, and C++ phase-trigger logic all exist; only the actual tree structure and Stillness beat pause timing remain.
3. ~~**Guaranteed-vital-hit wiring for The Reckoning**~~ — **DONE.** `IsReckoningActive()` added to `GothicPlayerCharacter`, wired into `OnFire`'s vital hit check via `IsReckoningActive() || VitalPoint->IsVitalPointHit(...)`. Confirmed during design review that Slicer and Hunter's Strike do not check vital points at all, by design — Slicer is a stagger tool, not a precision tool; Hunter's Strike is a deliberately simple, always-available light attack with no systemic integration (minimal damage, no vital/stagger/Resonance interaction, consistent with Draw's role as "the deliberately simple tool" in the Warden kit) — so `OnFire` was the only place this wiring was actually needed. Scope was correctly narrowed rather than force-fit onto abilities that don't use vital detection.
4. **Minimum weapon/ability visual and audio feedback** — confirmed currently missing or unconfirmed: muzzle flash, projectile/tracer visibility, hit impact feedback. A shooter with no visible feedback on firing is not a playable slice regardless of how correct the underlying damage math is. Placeholder-quality VFX/SFX is acceptable; total absence is not.
5. **Enemy death feedback beyond ragdoll** — confirmed currently ragdoll-only. At minimum needs some visual marker (blood, a death VFX beat) before the Selah prompt appears, so the kill itself registers before the reward does.
6. **Selah collection UI** — fully designed, zero implementation. The loop currently has no visible collection moment at all.
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
