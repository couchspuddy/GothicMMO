# Death & Failure States
**Vigil — Core Systems Design Document**
*Status: Locked — Contract & Open World. Raid deferred. Downed-state mechanical specification added July 16 — see that section for solo/multiplayer distinction and full-party wipe reset contents.*

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

## Downed State — Mechanical Specification (Added July 16)

The sections above establish the sequence (downed → revival window → teammate revives or window expires). This section locks *how* that sequence behaves at the state-machine level, resolved in a design session covering the solo case and the exact contents of a full-party reset.

**Solo Contract play is not a scaled-down version of the multiplayer sequence.** With no self-revive mechanic, a solo player has no one to fill the revival window, so solo death skips the downed state entirely and goes straight to the checkpoint-revert consequence below. This is the deliberate control condition for a future A/B test against a self-revive option — solo is meant to be harsher than any group wipe, since a group gets one down per member before failing and solo gets one, period. Not a bug to soften; the intended baseline to test against.

**Multiplayer, individual player downed:**
- Triggers at 1 HP rather than 0 — the floor exists specifically so the transition into downed can reuse the existing zero-health check path without an early intercept, and so a downed player's state stays clearly distinguishable from dead.
- The 1 HP floor alone does not make a downed player safe — the damage pipeline must separately refuse to apply further damage while downed, or a second hit resolves through to 0 and blows past the state entirely. This should be enforced at damage resolution (`PostGameplayEffectExecute` or equivalent), not per-ability, so every current and future damage source is covered by one check rather than requiring each new ability to remember it.
- Downed players are safe from further damage until the window expires — not vulnerable and racing a teammate against an enemy, purely racing the clock.
- Weapons and abilities are locked out while downed; player state (position, gear, resources) is otherwise retained, not reset.
- The revival window is a `FTimerHandle`-based countdown, not a GameplayEffect duration — durations cannot pause, and this window must pause during an active revive attempt and resume, not reset, if that attempt is interrupted. Same pattern as the existing AI leash-check timer.
- Reviving is a channeled ability on the reviving player: starting the channel pauses the downed player's window; completing it clears the downed state; an interrupted channel simply leaves the window paused-then-resumed with no penalty to either player.
- If the window expires before revival, that player is eliminated for the remainder of the current attempt (per the existing rule above).

**Full-party wipe — reset contents:** when every player in the instance is simultaneously eliminated, the instance reverts to the last completed Selah checkpoint (per the existing rule above) with the following specific state restored, not a blanket reset:
- **SuperMeter is left exactly where it was at the moment of the wipe.** This is a deliberate extension of the existing standing design doctrine that SuperMeter survives individual death — a wipe does not override that rule, it inherits it.
- **All ability cooldowns are cleared** — full charge on every ability, as if freshly granted.
- **Steadfast resets to full**, matching its state immediately following a Selah collection.
- **Ammo (magazine and reserve) reverts to whatever each player had at the checkpoint**, not to a fresh/full loadout. This requires the checkpoint itself to snapshot per-player ammo state at the moment it's created, since ammo is deliberately not a GAS attribute and nothing today records it. This is new responsibility for the encounter/checkpoint system, not existing behavior being reused.

**Open implementation question, not yet resolved:** whether the decision to treat a death as "downed" (Contract, teammates present) versus "immediate elimination" (solo, or open world) belongs on the GameMode — which already has content-type context — or requires a new flag on the AttributeSet itself. Current lean is GameMode, to keep the AttributeSet unaware of content type, but this hasn't been locked.

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
