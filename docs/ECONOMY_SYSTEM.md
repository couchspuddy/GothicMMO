# The Economy
**Vigil — Resource & Loot Design Document**
*Status: Locked — Core Structure. **Amended July 16, 2026** — see ITEMIZATION_AND_LOOT.md, which supersedes this document on four points: Pilgrimage item variance, the scope of the "Selah cannot buy best-in-slot" rule, Pure/Unique drop sourcing, and rarity naming. Amended passages below are marked inline. Everything else here stands.*

*Draft addenda, August 4, 2026 (**DRAFT FOR REVIEW 2026-08-04** — numbers proposed, not locked): weapon damage now scales by the weapon's own tier and rolled weapon perks are proposed as the weapon variance axis (WEAPON_ARCHETYPES.md); Attack Power and Defense scale with a summed armor Gear Score and damage multiplies off them (ITEMIZATION_AND_LOOT.md); first-pass per-rarity Strain costs give the 100 cap teeth (RESONANCE_STRAIN_AND_GEAR_LIFECYCLE.md). None of it changes this document's axes — gear quality remains the ceiling, Selah the completion, Strain the capacity — those drafts put numbers under the second and third axis.*

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

**Scope clarified July 16, 2026 — the rule stands, its boundary is now explicit.** Selah does two things at the Binder: raise stars, and **re-roll secondary lines**. Re-rolling is technically grinding currency toward a better piece, so the rule needs a sharper edge: **Selah buys attempts, not outcomes.** Re-rolls are random and never directed — a player cannot select a stat. The drop still sets the ceiling, luck still sets the roll, and a player with a mountain of Selah and a bad piece still has a bad piece.

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

**The reward:** A **Pure** item — one of three possible per source (e.g. the Bestial Lucid yields one of three different Pure pieces). Each is accompanied by a fragment of that Accursed's life before the Bleed.

**Locked July 16, 2026 — the three fragments are three pieces of the same life.** Recovering all three of a source's Pure items is how a player comes to understand who that Accursed was before the inversion. This makes the item chase and the lore chase the identical action.

**Locked July 16, 2026 — the Pilgrimage is the only path to Pure gear.** Pure items do not drop from any loot pool. Lucid enemies top out at Resonant and instead carry the highest Pure Selah drop rate, which the tier-scaling rule above already justifies. This closes the long-open question of Pure Selah's role relative to the top gear tier: they are not parallel systems competing for the same design space — Pure Selah is the *access mechanism* for Pure gear, and nothing else grants it.

**Stats:** ~~Pure Selah / Pilgrimage items have **static stats**. No variance, no rolling.~~ — **AMENDED July 16, 2026.** Pure items roll secondary lines and re-roll at Sean. See ITEMIZATION_AND_LOOT.md.

**Why this changed:** the static rule was derived from the premise *"Pilgrimage items are one-time narrative payloads."* That premise no longer holds — the Pilgrimage is now repeatable content with three destinations per Accursed source, and is the **only** path to Pure gear. This document's own principle, *"variance belongs wherever repetition is desired,"* therefore now argues **for** variance here rather than against it.

**The original concern is still correct and is still honored** — just structurally instead of by prohibition. The fear was that variance *"would implicitly ask players to repeat a deeply personal narrative mission for a marginally better number."* It cannot: the Pilgrimage is architecturally incapable of improving a roll on an item you already hold. New rolls come from Sean, who costs Selah and nobody's memory. The Pilgrimage grants only new items and new fragments. Each activity does one job and cannot be made to do the other's.

**The fragment is never farmable.** Each source yields one of three Pure items, each carrying a fragment of that Accursed's life; the three fragments are three pieces of the same person's story, and collecting all three is how you come to understand who they were. Once all three are recovered, that source's Pilgrimage has nothing left to give. The item chase and the lore chase are the same action — you re-run her Pilgrimage to finish knowing her, not to shave a number.

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

- ~~Exact loot pool quality bands per activity tier~~ — **CLOSED July 16, 2026.** Drop eligibility is now gated by *enemy tier* rather than activity tier: Thrall → Salvage/Kept, Retained → Kept/Remembered, Lucid → Remembered/Resonant. See ITEMIZATION_AND_LOOT.md.
- Full Pilgrimage mission content design (semi-random structure, specific lore-piece implementation). **Now higher priority than when filed** — the Pilgrimage is the sole source of top-tier gear, not a side narrative, so this is endgame activity design.
- Resonant Level progression curve and its relationship to Resonance Strain thresholds — *partially advanced 2026-08-04: first-pass per-rarity Strain costs are drafted for review in RESONANCE_STRAIN_AND_GEAR_LIFECYCLE.md; the Resonant Level access curve itself remains open*
- Final decision on lottery odds visibility
- Whether Selah gifting (non-marketplace, direct player-to-player) should be reconsidered — currently locked as fully non-tradeable
- **Hollow reward currency** — the Hollow are pre-human and drop no gear. What they *do* drop is undesigned.
- **Re-roll cost model** — flat-per-tier recommended in ITEMIZATION_AND_LOOT.md; not locked.

---

*Document generated: July 2026*
*Session: Vigil Design — Economy*
