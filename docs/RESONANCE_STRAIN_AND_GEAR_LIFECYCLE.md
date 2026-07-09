# Resonance Strain & The Gear Lifecycle
**Vigil — Resonance Strain, Legendary Sunset/Reintroduction Design Document**
*Status: Locked — Core Structure*

---

## Overview

Resonance Strain was named in early lore as the danger of bonding too many Resonant items at once, but had no mechanical definition until this document. This system gives it a concrete function: it is the single mechanism governing gear capacity, legendary gear lifecycle, and long-term itemization relevance — replacing the need for a separate rising power-cap (Resonant Level capacity growth) entirely.

**Core design decision:** Resonant capacity is a **fixed, permanent cap** (e.g. 100 Resonance). It never rises with expansions. All pressure in the system lives on the **item side** (strain cost per item), never on the player-cap side. This deliberately collapses what could have been a two-variable system (rising cap + rising item cost) into a single-variable system (fixed cap, item cost only), which is dramatically more legible to both designers and players — "can I equip this" becomes pure arithmetic against one static number, never a moving target.

---

## Why This Replaces Rising Resonant Level Capacity

Earlier discussion considered raising the Resonant Level cap with each expansion (classic MMO power-treadmill model) to create chase pressure. This was rejected in favor of putting all pressure on item-side strain cost, for two reasons:

1. **Legibility.** A fixed cap with variable item costs means a player only ever needs to reason about one number changing (the item's cost), never two numbers moving relative to each other (cap growth vs. item cost growth). This avoids the "two variables doing one job" trap that makes gear systems hard to balance and hard to explain over a live service's lifespan.

2. **Protects static Pilgrimage items without contradiction.** Pilgrimage items (see Economy document) are static-stat by design, specifically to protect their narrative weight from becoming disposable stat-chase fodder. If power instead decayed via item stats over time, that would silently violate the same protection through a different mechanism. Putting decay on **strain cost, not stats,** means the item's identity and effectiveness never change — only how much of the player's fixed capacity it demands.

---

## The Sunset Mechanism

**How it works:** Legendary and Pilgrimage-tier items have a strain cost that rises over time (tied to content cycles, not raw calendar time — see Open Items). When an item's strain cost exceeds the fixed Resonance cap, it becomes **unequippable.** Not deleted, not devalued in any other way — simply too heavy to currently carry.

**The lore justification — this is load-bearing, not decorative:** The crystallized humanity within a Resonant item does not stay static forever. The weight of what it carries — a life, a suffering, a piece of someone the Bleed took — grows harder to bear over time. Whether this is the fragment itself yearning for release, or the toll of carrying that much preserved humanity weighing on the Prior Flame that bonds to it, the effect is the same: eventually, the burden becomes too heavy to carry alongside everything else. The player does not "outgrow" the item. The item's grief accumulates until it needs to be set down.

**Narrative framing for the sunset moment:** *Thank you. Thank you, whoever you were before the Bleed took you. You have done your part. It's time for someone else to take up the mantle.*

---

## Reintroduction

**How it works:** A sunset item can be reintroduced in later content at a **reduced strain cost** — enough to be equippable again, but not restored to its original ease of use. The burden has softened. It has not lifted.

**Narrative framing for reintroduction:** *Hello, old friend. It's your turn again, to turn the tide against the Bleed once more.*

**Convergence:** Once reintroduced, an item's strain cost is not fixed at its new lower value indefinitely — it resumes aging, and converges with the strain-cost curve of current-tier items, eventually decaying alongside them at the same rate. This means:

- **New copies of a reintroduced item** (freshly earned by replaying old content during the reintroduction window) get more usable time before their next sunset, because they enter the convergence curve later.
- **Original, long-held copies** re-enter at the reduced cost but converge back toward unusability faster, since they're further along the same curve.
- This is an intentional asymmetry: **reintroduction is a demo, not a comeback.** It restores enough capacity to test a build combination or two — not enough to run a full loadout indefinitely. Veterans get a genuine, meaningful taste of relevance and nostalgia. New players are never permanently behind, because nothing reintroduced is competitive with current tier for long.

---

## Why This Model Was Chosen Over Alternatives

Two other models were considered and rejected:

**Rejected: Indefinite catch-up parity (pure Model A).** Old gear stays fully relevant forever via a permanent catch-up currency. Rejected because it requires perpetual catch-up-economy maintenance every expansion, and risks catch-up currency demand competing with new-content currency demand for player attention and design focus.

**Rejected: Scheduled hard rotation (pure Model B).** Gear is fully unusable for a fixed real-world period, then fully returns, on a public schedule (e.g. "Year 1 gear returns in Year 3"). Rejected specifically for **combat-functional gear** because it removes player agency entirely for a scheduled blackout window with no way to bridge it — a harder violation of "Earn Everything" than a rising cap would have been, since a cap increase at least leaves a path to catch up through effort. (Rotational scarcity remains a good fit for **cosmetics and non-combat flourishes**, which were not restricted by this reasoning — see Open Items.)

**Adopted: Fixed cap, item-side strain decay, reduced-cost reintroduction with convergence.** Chosen because:
- It never invalidates a static item's stats or story (protects Pilgrimage item integrity)
- It rewards veterans without creating permanent inequity for new players (the reintroduction window is explicitly temporary and non-competitive with current tier)
- It gives players a genuine reason to replay old content during a reintroduction window (new copies converge later than old ones)
- It is a single-variable system, dramatically easier to reason about and balance than any model requiring a rising player-side cap

---

## Emergent Build-Crafting Value

A previously-sunset legendary can become relevant again not just through its own reintroduction, but through **new items changing what it's worth building around.** Because Pilgrimage/legendary items have static, fixed identities, a new item introduced in later content can create a synergy that didn't exist before — suddenly making an old, otherwise-irrelevant piece the missing part of a build that "hums." This can happen without ever touching the old item directly. This is the same mechanism that keeps decade-old unique items feeling alive in long-running itemization-driven games: the item never changes, but the system around it does, and that's enough to resurrect its relevance on its own.

---

## UI Requirements

**Explanatory UI — the Resonance bar.** A persistent, always-visible UI element functioning like an encumbrance meter: total available Resonance cap vs. currently equipped strain. Must be precise and legible at all times — players should never feel bamboozled by an unclear cap.

**Provisional preview (recommended refinement).** When hovering an unequipped item in inventory, the Resonance bar should preview what it *would* look like if equipped, before the player commits. This turns the bar from a static readout into an active planning tool, which is where the actual "does this build combination work" decision-making lives. The number matters less than the ability to test combinations cheaply before committing.

**Diegetic UI — visual treatment for reintroduced items.** Reintroduced items should carry a distinct visual treatment (e.g. a duller shimmer compared to current-tier items) so players understand at a glance, without reading a tooltip, that "this one is different" — running on borrowed time rather than being a fully current piece. This is intentionally a fast, low-cognitive-load signal layered on top of the precise numeric readout in the Resonance bar, not a replacement for it.

**Explicitly deferred:** Whether the diegetic "dulled" visual persists permanently after an item fully re-converges with current-tier decay (a permanent tenure/prestige marker) or only applies during the active convergence window (a pure state indicator) is **explicitly out of scope for now.** This decision requires asset-driven UI infrastructure that doesn't exist yet and should not be resolved prematurely. Leaning toward state-based (temporary) for production sustainability reasons, but not locked.

---

## Design Tenet Alignment

- **Earn Everything** — reintroduction requires replaying content for a fresh, later-converging copy; nothing is purchasable, and even loyal players' original copies are subject to the same convergence curve as everyone else's.
- **Every system serves the feeling** — the entire sunset/reintroduction cycle is built on the same emotional logic as Selah and the Hunter's list: humanity carried has weight, and that weight is never trivial, even in a gear-progression system.
- **Respect the player's intelligence** — explicit, precise UI (the Resonance bar) ensures no player is ever confused about why an item can or can't be equipped, even though the underlying lore framing is poetic rather than literal.

---

## Open Items for Future Development

- Exact strain-cost values and decay-rate curve per content tier (requires playtesting once multiple content cycles exist)
- Exact convergence rate for reintroduced items (how quickly a reintroduced copy catches up to current-tier decay)
- Whether rotational scarcity (Model B logic) should be formally adopted for **cosmetics and non-combat flourishes** as a separate, parallel system to this one (tentatively yes, not yet designed)
- Permanent vs. state-based diegetic visual treatment for reintroduced items — deferred until asset-driven UI work begins
- Whether the Resonance cap value itself (currently placeholder "100") should differ by class, or be universal

---

*Document generated: July 2026*
*Session: Vigil Design — Resonance Strain & Gear Lifecycle*
