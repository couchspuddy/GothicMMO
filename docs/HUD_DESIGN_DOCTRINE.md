# HUD Design Doctrine
**Vigil — Zone-Based HUD Architecture**
*Status: Governing structure locked. Supersedes and extends the combat-surface work in the UI/UX Doctrine, which remains valid and should be read alongside this document.*

---

## Purpose and How This Document Relates to the UI/UX Doctrine

The UI/UX Doctrine established individual combat surfaces (Health, Steadfast, Ability States, Selah Collection) and two governing principles (legibility-scope test, binary-state signaling). This document sits one level above that work: it establishes **where on screen anything belongs and why**, based on screen geometry and the actual speed of decision each piece of information needs to support. Every surface locked in the UI/UX Doctrine should be placed according to the zone map below; this document does not replace those surface designs, it gives them a home.

---

## The Governing Reframe: Information Enables Agency, It Does Not Replace It

Earlier HUD exploration in this project operated from an unstated assumption that HUD minimalism was inherently more aligned with "Respect the Player's Intelligence" than a fuller HUD would be. This was corrected during design discussion and is worth stating explicitly as the doctrine's actual foundation:

**A player cannot make a skillful, informed, agency-driven decision about information they cannot accurately read.** Destiny's most memorable player moments were not built from withholding information — they were built from players making fast, correct decisions because the information to make them was available at a glance. Elden Ring's boss health bar does not cheapen a fight; it is what allows a player to choose to push for one more hit instead of retreating, because they can see a phase transition approaching. Call of Duty's hit-marker color coding does not remove the skill in aiming — it confirms execution, which is what lets a player calibrate and improve.

**The correct test for any HUD element is not "does this respect the player's intelligence by staying minimal" — it is "does this let the player make a better decision, faster, than they could without it."** Nothing should exist on the HUD that fails this test. Nothing that passes it should be omitted for the sake of minimalism alone. Readability is not the enemy of player agency — it is the raw material agency is built from, and higher information quality raises the achievable skill ceiling rather than lowering it.

---

## The Zone Map — Built From Screen Geometry, Not Convention

Screens are wide rectangles, not squares. This has real, measurable implications for how quickly the eye can travel to and return from a given point on screen, and the zone map is built directly from this geometry rather than from genre convention (though genre convention frequently arrives at the same answer, for the same underlying reason).

**Distance from center is not the only cost.** A corner is farther from center than an edge midpoint by raw Euclidean distance (the diagonal is always longer than either leg alone) — but corners carry an additional cost beyond distance: diagonal eye movement (a less natural saccade) is generally slower and less comfortable than pure horizontal or pure vertical movement, even accounting for distance. Corners are therefore doubly disadvantaged, not simply the "farthest" point on a single distance scale.

### Zone 1 — The Reticle (Zero Latency)

The single most valuable real estate on the entire HUD, since the eye is parked here by default throughout combat. Reserved exclusively for information that must be read with zero perceptible delay, as part of the same instant as aiming or firing itself.

**Locked contents:**
- Steadfast display (chunked segments, per UI/UX Doctrine) — already correctly placed here prior to this document
- **Hit confirmation feedback** (new scope, not previously designed) — visual confirmation that a shot registered as a vital hit should appear at or immediately around the point of impact/reticle itself, not as a separate UI element elsewhere on screen. This is the Vigil-specific application of the Call of Duty color-coded-hit-marker principle discussed during design.

### Zone 2 — Bottom-Center Cluster (Near-Zero Latency)

Short, clean vertical eye travel from the reticle, with easy return to center. Reserved for information requiring fast reading but not the zero-latency treatment of Zone 1.

**Locked contents:**
- Health bar (per UI/UX Doctrine's existing specification — warm-to-cold color treatment, party-legible)
- Ability readiness row (per UI/UX Doctrine's binary-state and synergy-state signaling rules)
- **Buff/debuff cluster** — see dedicated section below

### Zone 3 — Horizontal Edges (Deliberately Minimized)

**This zone is treated differently from the others by design, not merely deprioritized.** Horizontal peripheral screen space is also where a player's actual gameplay-relevant peripheral vision operates in an FPS — spotting lateral movement, an enemy entering frame from the side, environmental threats. Persistent UI occupying this space does not merely represent a lower design priority; it actively degrades the player's functional ability to use peripheral vision for its actual gameplay purpose.

**Rule: nothing persistent should default to Zone 3.** Any information that might have naturally been assigned here (ammo reserve counts, Kindle/party status) should instead appear **contextually and temporarily** — a brief flash of reserve ammo count during/immediately after a reload, a brief Kindle-status alert only when a teammate's state changes meaningfully (e.g. dropping to low health) — rather than sitting permanently. This is the same design instinct already applied elsewhere in the project (Selah's contextual prompt, the ability state-layer's discrete signals rather than constant readouts) extended into a new zone.

### Zone 4 — Corners (Lowest Priority, Permanent but Quiet)

Reserved for information that supports slower-paced decisions — orientation and planning rather than split-second combat choices.

**Locked contents:**
- Compass (N/S/E/W orientation). No minimap is planned; the compass is explicitly a simple orientation aid, not a navigational tool.

Corners are currently underused in this framework by design — almost nothing in Vigil's current systems generates information that only needs corner-tier, glance-only treatment.

---

## Buffs and Debuffs — Fixed Location, Variable Prominence

**Locked rule:** buffs and debuffs occupy a single, consistent home region on the HUD (Zone 2, adjacent to health and ability readiness) regardless of individual urgency. Within that fixed region, **position and visual prominence are ranked by urgency** — an actively punishing debuff that should change the player's immediate decision sits closer to the primary reading position within the cluster; a minor, low-urgency tracked effect sits further out within the same cluster.

**Why fixed location matters more than per-item optimal placement:** a player only needs to learn "where buffs and debuffs live" once, permanently, if the location never changes. If different effects lived in different zones based on individual urgency, the player would need to re-scan multiple screen regions to determine their full status, which costs more in practice than any single effect's marginally-optimized placement would save. This is the same underlying principle as the reticle's permanent ownership of Steadfast and hit confirmation, and health's fixed position regardless of current danger level — **fixed location, variable prominence** is a recurring, deliberate Vigil HUD principle, not a one-off rule.

This section is currently written ahead of having real buffs/debuffs to test it against (Warden and Penitent, the classes most likely to introduce meaningful buff/debuff design, are not yet implemented). The rule should be validated once real effects exist to place within it.

---

## Remappable HUD — Filed as a Real Future Feature, Not Default Scope

The idea of allowing players to remap or reposition HUD elements to personal preference was raised during design discussion and is a genuinely good instinct, consistent with the project's broader commitment to personal skill expression and player-driven builds (Resonance Forks, etc.). It is explicitly **not** part of the default HUD's build scope. Remapping is significantly easier to design and implement once a stable default HUD exists with well-defined element boundaries, rather than being designed simultaneously with the default layout itself. Filed alongside other genuine future concepts (32-person Hold content, Ancients) — real, worth returning to, not blocking current work.

---

## Known Implementation Bugs — Current HUD State (As of This Session)

Confirmed by direct observation of the current in-engine HUD, worth recording precisely since these are concrete, fixable issues rather than design gaps:

**Health number display bug.** The health bar's numeric text is currently reading from the normalized 0-1 fill value used to drive the progress bar's visual fill percentage, rather than from the raw Health attribute. This produces nonsensical display (observed: "100 to .48" rather than accurate current/max values). The progress bar's fill percentage binding is correct and should remain a 0-1 normalized value; the text display needs an independent binding directly to the raw Health and MaxHealth attributes. This is a Blueprint widget-graph fix, not a C++ or design issue.

**Scale and transparency.** Current HUD elements read as too small and too transparent even at full screen resolution, to the point of being difficult to read (including the health number itself, independent of the binding bug above). Needs a direct sizing/opacity pass in the Widget Blueprint.

**Steadfast reticle display does not exist yet.** Confirmed not built — this is new construction, not a fix, per the original design in the UI/UX Doctrine.

**Reticle has no weapon-type awareness.** Currently irrelevant with only one weapon (pistol) implemented — the reticle can be hardcoded to pistol styling without being incorrect. This becomes a real requirement the moment a second weapon exists to differentiate against, but is explicitly not gating for the current vertical slice (see Production Status Tracker). Building weapon-type-awareness now, with nothing to validate it against, would be premature.

---

## Design Tenet Alignment

- **Respect the Player's Intelligence** — reframed and strengthened by this document: intelligence-respect is best served by giving players accurate, fast, well-organized information to reason with, not by withholding information in the name of minimalism.
- **Weight Before Spectacle** — the Zone 3 horizontal-space restraint is this tenet applied to a new problem: even useful information can be spectacle if it costs the player something more valuable (functional peripheral awareness) than it provides.
- **Every system serves the feeling** — fixed-location-variable-prominence, applied consistently across Steadfast, health, and now buffs/debuffs, gives the whole HUD a coherent, learnable internal logic rather than being a collection of independently-optimized elements.

---

## Open Items

- Buff/debuff cluster's exact prominence-ranking mechanics — needs real buffs/debuffs (Warden/Penitent) to test against
- Zone 3's contextual-trigger specifics (exact conditions for the brief ammo-reserve flash, exact Kindle-status-alert thresholds)
- Remappable HUD — filed, not scheduled
- Weapon-type-aware reticle styling — filed until a second weapon exists

---

*Document generated: July 2026*
*Session: Vigil Design — HUD Doctrine*
