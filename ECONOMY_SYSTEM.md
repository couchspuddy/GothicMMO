# The Economy
**Vigil — Resource & Loot Design Document**
*Status: Locked — Core Structure*

---

## Overview

Vigil's player-power economy is deliberately split across three independent axes rather than collapsed into a single rarity system. Each axis answers a different question and carries a different feel:

| Axis | Question It Answers | Feel |
|---|---|---|
| Gear Quality (Ceiling) | What did the world give me? | Earning access |
| Selah (Currency) | How much have I invested? | Earning completion |
| Resonant Level | How much can I actually carry? | Earning capacity |

Because each axis is independently tunable, none of them can be rebalanced into cannibalizing another. This also means every axis of player power is narratively load-bearing — none of it is arbitrary numbers with a skin on it.

---

## Selah

**What it is:** A single, universal currency. No tiers, no types. Crystallized human essence, produced when the Prior Flame interrupts the Bleed's extraction at the moment of an Accursed's death.

**How it's earned:** Any Accursed kill where an Antecedent is present at the crystallization.

**What it's spent on:** Imbuing gear — raising a piece's current stars toward its ceiling (see Gear Quality below). Cost increases as the piece approaches its ceiling.

**Trading:** Selah is **never tradeable**, under any circumstance. No marketplace, no direct gifting between players. This keeps the currency personal and prevents RMT/bot-farming surface area, and is consistent with "Earn Everything."

---

## Gear Quality (Star Ceiling)

**What it is:** An inherent property of a gear piece, set at the moment of drop, that determines the *maximum* number of stars that piece can ever be imbued to. Not purchasable. Not changeable after drop.

**How it's earned:** Random drop from activity-specific loot pools. Higher-difficulty activities have loot pools weighted toward higher ceilings.

- Standard encounters → lower ceiling bands
- Mini-boss encounters → mid ceiling bands
- Raid / boss encounters → highest ceiling bands

**Design rule:** A player's gear ceiling is capped by their *best drop*, not their Selah stockpile. Grinding currency alone cannot buy best-in-slot — only fully realize what has already been earned.

**Rolled stats:** Standard gear drops have variable stats around a class-biased primary stat (e.g. a Warden piece leans Defense but can roll higher Defense or roll secondary stats). This creates a chase loop independent of the ceiling system — players seek not just high-ceiling pieces, but well-rolled ones.

**Why variance exists here:** Variance justifies repeated engagement with the same activity. This is the intended endgame grind loop.

---

## Resonant Level

**What it is:** A player-progression stat, separate from any individual gear piece, that caps the *total bonded resonance* a player can have equipped simultaneously across all gear.

**Function:** This is the system's soft-cap against infinite power stacking. It is the mechanical seat of **Resonance Strain** — narratively, too many active Resonant bonds pull on the Antecedent's own essence and destabilize their immunity.

**Design implication:** A player can own five 5-star items and still be unable to equip all five simultaneously until their Resonant Level rises. This makes gearing a combinatorics puzzle — chasing pieces that roll well *together* under a capacity constraint — rather than a flat "always equip the biggest number" system.

---

## Pure Selah

**What it is:** A rarer, categorically distinct drop from standard Selah. Represents a *complete* crystallization — no Bleed residue remaining at all. Standard Selah always carries some Bleed contamination because the extraction was merely interrupted, not fully resisted.

**Drop chance scaling:** Increases with enemy tier. Higher-tier Accursed had stronger wills in life — the same quality that produces the "poke-through" in Retained and full lucid awareness in Lucid also means their essence is more likely to resist Bleed contamination completely at the moment of crystallization.

**Design rule for future encounter/enemy design:** *Higher tier Accursed had stronger wills in life, so their Selah is more likely to crystallize pure.* This connects Pure Selah drop rates directly to the existing Accursed hierarchy without requiring new lore.

---

## The Pilgrimage

**Trigger:** Earning a piece of Pure Selah opens a new mission — the Pilgrimage.

**Narrative framing:** The Prior Flame, carrying the fully-crystallized human essence, guides the player toward a specific gear piece. This is not a loot roll — it is depicted as the Prior Flame recognizing something and pulling the player toward it.

**Mission structure:**
- Semi-randomized content
- Always concludes at the same fixed narrative destination
- Linear, low combat emphasis
- Lore-forward — may include environmental storytelling, discernible lore pieces, or minimal combat depending on the specific Pilgrimage
- Exact content mix to be finalized during full build-out; the destination and narrative payload are locked, the path there is flexible

**The reward:** A unique item, one of several possible per source (e.g. the Bestial Lucid may yield one of three different unique pieces). Each unique item is accompanied by a fragment of that Accursed's life before the Bleed — a piece of who they were before the inversion.

**Stats:** Pure Selah / Pilgrimage items have **static stats**. No variance, no rolling. This is a deliberate contrast to standard gear.

**Why static, not rolled:** Variance exists to encourage repetition of an activity. Pilgrimage items are one-time narrative payloads — the fragment of a life is the reward, not a stat roll. Introducing variance here would implicitly ask players to repeat a deeply personal narrative mission for a marginally better number, which corrodes the thing that makes the item meaningful. Variance belongs wherever repetition is desired; it must be absent wherever repetition would undermine meaning.

**Trading:** Pilgrimage items are **never tradeable**, under any circumstance. Each one is tied to a specific player's Prior Flame being guided to a specific fragment of a specific life — trading it would be narratively incoherent independent of any economic argument.

---

## Gear Trading — Pre-Imbue Only

**Rule:** Standard gear drops (not Pure Selah / Pilgrimage items) are tradeable **only before any Selah has been imbued into them.** Once imbuing begins, the item locks to whoever holds it.

**Rationale:** Gear is class-agnostic — any class can drop any item. A four-class party will regularly see drops that are dead weight for the roller but ideal for another party member. Restricting trading entirely would waste a large percentage of the loot pool's value. Allowing pre-imbue trades lets the *group* redistribute what it collectively earned, without letting any individual buy power they didn't earn.

---

## Loot Distribution — The Resonance Lottery

Classic Need/Greed systems rely on player self-report honesty with no mechanical enforcement, which produces community friction. Vigil replaces this with an automatic, weighted lottery.

**Eligibility:** Since gear is class-agnostic, all party members are eligible by default for any drop.

**Weighting — locked approach:** Participation-weighted, with a decaying pity counter layered on top.

- **Participation weighting** rewards players who contributed meaningfully to the encounter with better odds. This must **not** be a single metric (e.g. raw damage dealt) — doing so would silently punish support-oriented classes (Penitent, Warden) whose contribution doesn't show on a damage meter. Contribution scoring must be per-class-relevant: damage dealt, Steadfast generated for allies, vital point hits landed, enemies staggered, deaths prevented, etc., depending on what constitutes "playing that class well" in a given encounter.
- **Pity counter** ensures players who lose lottery rolls repeatedly gain incrementally better odds each subsequent loss, resetting on a win. This guarantees that over a long session, grinding eventually pays off even through short-term bad luck — consistent with "Earn Everything."

**Trade window:** The lottery winner receives the item unbound, with a trade window (until leaving the instance, or a flat time limit) during which they may hand it to another eligible party member. This does not change who "gets" the item mechanically — it hands first right of refusal to the statistically favored winner and lets party-level social trust handle redistribution from there.

**Open question — visibility of odds:** Undecided whether participation weighting and pity counter values should be player-facing.
- Visible odds are more transparent and consistent with "Respect the Player's Intelligence."
- Hidden odds prevent players from min-maxing behavior around gaming their own lottery weight rather than playing the encounter well.
- To be resolved during playtesting.

---

## Design Tenet Alignment

This economy structure was built to satisfy several existing Vigil design tenets simultaneously:

- **Earn Everything** — no axis of power is purchasable with anything but time, skill, or drops. Selah cannot buy a gear ceiling. Trading cannot manufacture power that wasn't collectively earned by the group.
- **All classes must be viable** — the Resonance Lottery's per-class contribution scoring is explicitly designed to prevent damage-meter bias from punishing support-oriented classes.
- **Every system serves the feeling** — Pure Selah and the Pilgrimage turn the rarest drop in the game into a narrative event rather than a stat upgrade, reinforcing "the Accursed are never monsters" at the exact moment a player might otherwise treat a kill as purely transactional.

---

## Open Items For Future Development

- Exact loot pool quality bands per activity tier (standard / mini-boss / raid)
- Full Pilgrimage mission content design (semi-random structure, specific lore-piece implementation)
- Resonant Level progression curve and its relationship to Resonance Strain thresholds
- Final decision on lottery odds visibility
- Whether Selah gifting (non-marketplace, direct player-to-player) should be reconsidered — currently locked as fully non-tradeable

---

*Document generated: July 2026*
*Session: Vigil Design — Economy*
