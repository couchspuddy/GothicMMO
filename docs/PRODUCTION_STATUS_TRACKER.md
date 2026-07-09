# Production Status Tracker
**Vigil — Living Project State Document**
*Last updated: July 2026 — updated to reflect Warden and Penitent full kit design completion

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
| Eagle's Landing Mini-Boss (Feral Retained) | 🔴 | Fully designed, zero implementation |
| Eagle's Landing Boss (Bestial Lucid) | 🔴 | Fully designed, zero implementation |
| Interaction prompt UI (text display on interactables) | 🟡 | Interaction functions; visible prompt text not implemented |

---

## Core Combat Systems

| Item | Status | Notes |
|---|---|---|
| Vital point system (shimmer, threshold shift, damage bonus) | 🟢 | Functional, server-authoritative, needs feel-tuning pass |
| ADS | 🟢 | Functional, smooth FOV interpolation, movement penalty |
| Camera / locomotion | 🟢 | Fixed after extended debugging session; stable |
| Steadfast (fill, hold-to-reload conversion) | 🔴 | Fully designed (solo + multiplayer scope), zero implementation |
| Selah collection UI/prompt | 🔴 | Designed (appears after encounter clear, full restock), zero implementation |
| Death & failure states (Contract-scale) | 🔴 | Fully designed, zero implementation |
| Death & failure states (open world) | 🔴 | Fully designed, zero implementation |
| Death & failure states (raid-scale) | ⚪ | Explicitly deferred pending raid design |

---

## Hunter Class

| Item | Status | Notes |
|---|---|---|
| The Slicer | 🔴 | Fully designed, zero implementation |
| The Lunge | 🔴 | Fully designed, zero implementation |
| The Read | 🟡 | C++ ability class built and compiling; untested in PIE |
| The Reckoning (Covenant) | 🔴 | Fully designed, zero implementation |
| The Loved and The Lost (passive) | 🔴 | Fully designed, zero implementation |
| Not At All (passive) | 🔴 | Fully designed, zero implementation |
| GA_HuntersStrike | 🟢 | Built earlier in project, functional |
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

## Immediate Priority Order (Given Current Constraints)

Given engine time is the scarce resource right now, not design capacity, the following order maximizes validated progress per engine-hour:

1. **Mini-boss implementation** — fully designed, no blockers, closes a real content gap in the vertical slice
2. **Boss implementation** — fully designed, no blockers, closes the vertical slice's actual climax
3. **Hunter kit completion** — The Lunge, The Reckoning, and both passives remain unimplemented; The Slicer and The Read are functional
4. **Selah collection UI + Steadfast implementation** — both fully designed, both required for the loop to feel complete rather than just functional
5. **Death/failure state implementation (Contract-scale)** — fully designed, closes the last major "this doesn't exist yet and other systems assume it does" gap
6. **Playtest with a person who isn't you** — the actual validation gate; everything above this line is prerequisite to this being meaningful

All three launch classes (Hunter, Warden, Penitent) are now fully designed at the kit level. Warden and Penitent implementation is correctly deferred until the Hunter's kit is fully built and validated first — the framework (Resonance Fork model, the six-slot template, Be-At-Peace/I-Am-the-Wall-style passive-as-hub patterns) should be proven once, in engine, before being applied twice more. Economy implementation, progression systems, and raid design remain **correctly deferred** beyond that — not neglected, just genuinely lower priority than closing the loop between "fully designed" and "fully playable" for what already exists.

---

## How to Use This Document

Update the status table whenever something moves categories — that's the entire job of this document. It is not meant to be exhaustively rewritten each session; it's meant to be a quick, honest snapshot you or anyone else can glance at and immediately understand: what's real right now, versus what's well-designed but still theoretical.

If a feeling of "are we missing something foundational" comes up again, check here first before opening a new design conversation. If everything relevant to the question already has a row in this table with a status, the feeling is very likely about the 🔴/🟡 gap (implementation debt) rather than a genuine ⚪ gap (missing design). Only ⚪ rows represent real unaddressed design gaps.

---

*Document created: July 2026*
*Session: Vigil Design — Production Status & Foundations*
