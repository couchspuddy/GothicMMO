# Death & Failure States
**Vigil — Core Systems Design Document**
*Status: Locked — Contract & Open World. Raid deferred.*

---

## Overview

Failure severity in Vigil is tiered by content structure, not by a single universal death mechanic. The governing principle: **stakes should scale with structure, not just danger.** A place can be dangerous-feeling without being costly (open world), and a bounded activity can be both harder and costlier because the player opted into it knowingly (staged content). These are two separate design dials, not one, and should be treated as such for all future zone and activity design.

Since Absolution was shelved (see prior session), there is currently no self-rescue mechanic anywhere in the game. All recovery from a downed state requires either a teammate or, in open world, simply time passing.

---

## Open World Death

**Sequence:** Player reaches zero health → brief down period → returned to an established safe hub.

**Cost:** Time only. **No material cost** — no gear loss, no Selah loss, no Resonance Strain penalty, nothing beyond the inconvenience of the trip back.

**Design rationale:** Open world's purpose is onboarding, exploration, and worldbuilding — not testing mastery. Its hostility should feel real without being punishing. This lets players learn the game and engage with hostile territory without the game "pushing back too hard," while staged content remains the place where genuine stakes and cost live. The hub itself should have standalone value as a place worth visiting for reasons beyond death-recovery — it's a real location, not a purely functional respawn screen.

---

## Staged Content (Contracts) — In-Progress Failure

**Sequence on individual player death:**
1. Player enters a **downed state**, not an immediate loss.
2. A **revival window** opens — a teammate must reach and revive the downed player within this window.
3. **No self-rescue exists.** Revival requires a teammate, except where a specific class ability is explicitly designed to allow it (an intentional exception point for future class kit design, not a general rule).
4. If the window expires without revival, the player remains down for the rest of the current attempt.

**Sequence on full-party failure:**
- If **all players in the instance** are simultaneously downed, the encounter reverts to the **last completed checkpoint.**

---

## Checkpoints — Selah Collection Points Double as Save States

**Core decision:** rather than designing a separate checkpoint system, **Selah collection points serve as checkpoints.** This is a deliberate consolidation — the same beat that asks the player to pause and honor a kill is also the beat that determines how much progress a wipe costs. One mechanic, two jobs, both reinforcing the same rhythm rather than competing.

**What this means practically for Eagle's Landing specifically:** the encounter structure already locked in the Eagle's Landing Encounters document produces evenly-spaced checkpoints for free — a checkpoint exists after every encounter's Selah moment, with no separate checkpoint-placement design pass required.

**On full-party wipe:** the instance reverts entirely to the last completed Selah collection. All progress made since that point is lost — enemies respawn, any consumables used since that point are restored, any partial progress toward the next Selah moment is discarded.

---

## The Interrupted Selah Edge Case — Resolved

Eagle's Landing Encounter 3 includes a Selah moment that is deliberately interrupted mid-collection by a surprise second wave (see Eagle's Landing Encounters document). This created a genuine ambiguity worth resolving explicitly rather than leaving implicit:

**If a full-party wipe occurs after Wave 1 is cleared but before the interrupted Selah collection completes (i.e., during or after the Wave 2 interruption), the checkpoint reverts to *before Wave 1 began* — not to some intermediate point.**

**Rationale:** the Selah moment was not completed. A checkpoint should only exist where a Selah moment was fully honored, not partially reached. This has a secondary benefit worth naming: it makes the encounter's existing design intent — teaching the player that Eagle's Landing does not respect the rhythm they've built — *more* effective on a wipe, not less. A party that fails during the surprise wave loses the entire encounter's progress, reinforcing rather than softening the lesson the encounter was already designed to teach.

---

## Raid-Scale Failure — Explicitly Deferred

During design discussion, a token/med-kit resource layer for raid revivals was proposed: reviving a downed player in raid content consumes a finite, party-carried resource (rather than being free and unlimited as in standard Contracts), turning "can we revive this person" into a resource-management question across the whole raid attempt rather than a purely mechanical one.

**This is intentionally not locked yet.** Open questions include:
- Whether tokens/med-kits are a fixed allotment for the entire raid attempt, or replenishable through some mid-raid mechanism
- Exact quantity per party
- Whether the same Selah-checkpoint model applies at raid scale, or raids need a distinct checkpoint structure given their likely longer, multi-phase length

**Rationale for deferring:** no raid content has been designed yet. Resolving this now would mean designing a resource economy with no actual encounter structure to attach it to — the same mistake being deliberately avoided elsewhere in the project (see: Absolution being shelved rather than forced into shape prematurely). Revisit once raid-scale content design begins in earnest.

---

## Design Tenet Alignment

- **Every system serves the feeling** — using Selah collection points as checkpoints means the game's most emotionally significant recurring beat is also its most mechanically significant one; failure and remembrance are tied together rather than being separate, competing systems.
- **Respect the player's intelligence** — the downed-state/revival-window pattern is a well-understood co-op genre convention; no need to reinvent it where the existing pattern already serves the design well.
- **Earn Everything, low-stakes onboarding excepted** — open world explicitly opts out of the "cost" side of this tenet by design, on the reasoning that stakes should scale with structure. Staged content is where the tenet's teeth actually live.

---

## Open Items for Future Development

- Full raid-scale failure/revival economy (token/med-kit source, quantity, replenishment — deferred until raid content design begins)
- Whether any class's kit-defining ability should include a self-revival or ally-revival-without-consuming-window exception, and if so, how that interacts with the "no self-rescue" baseline rule
- Party/Kindle formation systems (matchmaking vs. premade) — adjacent gap, not resolved here, affects how forgiving the revival window needs to be tuned

---

*Document generated: July 2026*
*Session: Vigil Design — Death & Failure States*
