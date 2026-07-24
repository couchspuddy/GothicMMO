# Itemization & Loot
**Vigil — Gear Slots, Rarity Ladder, Stat Budgets, and Drop Sources**
*Status: Locked — Core Structure. Extends The Progression Pillar (PROGRESSION_STATS_AND_BALANCE.md) and The Economy (ECONOMY_SYSTEM.md), closing the "Secondary stat itemization budget per gear ceiling tier" open item in the former and the "Pure Selah's role relative to Unique drops" open item in the latter.*

*This document consolidates two July 14 sessions — stat totals/itemization and the non-Selah loot ladder — which were designed but never committed. It supersedes both. Where it contradicts ECONOMY_SYSTEM.md, this document is newer and wins; the specific amendments are listed at the bottom so the contradiction is visible rather than silent.*

---

## Purpose

`ECONOMY_SYSTEM.md` answers "what are the axes of player power and why." `PROGRESSION_STATS_AND_BALANCE.md` answers "what are stats and how do we stop them being solved." This document answers the layer between them: **what a piece of gear actually is** — what slot it occupies, what rarity band it dropped from, how many stat points it carries, which stats it can roll, and how much of that a player can wear at once.

It is meant to be handed to engine work directly. Numbers here are first-pass and tunable; the structure is locked.

---

## Gear Slots

**Ten armor slots:** Head, Neck, Left Arm, Right Arm, Wrist, Chest, Back, Left Leg, Right Leg, Feet.

**Three weapon slots:** Primary, Secondary, Heavy.

**Weapons are excluded from the armor stat budget entirely.** Weapon identity expresses through its own perks, its weapon class, and Steadfast behavior — not by rolling Resolve. The ten armor slots are the whole stat surface.

**Slots are class-agnostic, without exception.** No slot is flavored, gated, or reserved for a class. This is load-bearing rather than incidental: class-agnostic gear is the entire rationale for the pre-imbue trading rule in `ECONOMY_SYSTEM.md` — a four-class Kindle regularly sees drops that are dead weight for the roller and ideal for someone else. A class-locked slot would create a drop category that can never reach the person who wants it.

**Class identity is expressed two ways instead**, neither of which requires a special slot:
- **Primary stat weighting per drop** — a piece that drops for a Warden weights its primary points toward Warden's two identity stats (see Primary Split below).
- **Class-conditional effects on Pure items** — the item reads which class bound it (see Pure below).

---

## Rarity Ladder

Renamed from the generic Common/Uncommon/Rare/Legendary/Unique to sit inside the fiction. The ladder's real axis is **how much of a person is still legible in the object**, and its hard mechanical boundary is whether the Prior Flame engages with it at all.

| Rarity | Meaning | Selah | Strain | Primary stats |
|---|---|---|---|---|
| **Salvage** | Nobody's in it. Scavenged, jury-rigged | Never | Zero | **None — locked out** |
| **Kept** | Someone maintained this for two centuries. A person is implied, not present | Never | Zero | Yes |
| **Remembered** | A trace of a life is legible. Last tier before the Flame engages | Never | Zero | Yes |
| **Resonant** | Selah-bound, bonded to the Prior Flame | Bindable | Per-slot, scales with imbuement | Yes |
| **Pure** | Complete crystallization. Pilgrimage-only | Bindable | Per-slot, scales with imbuement | Yes |

**Salvage through Remembered are the mundane gear system** — flat stat-sticks, no Selah interaction, zero Strain. This resolves the long-open "what does non-Selah gear look like" question by construction: those three tiers *are* the answer.

**Salvage rolls no primary stats, and this is narrative, not a nerf.** Salvage predates the Prior Flame's awakening — no Resonant Level exists yet. Primary stats becoming available *is* the mechanical expression of the Flame starting to remember. Free coherence, no extra work.

**The bottom three names are mundane-world nouns on purpose.** They're the tiers where the Flame is asleep. The top two are words the game already uses.

---

## Rarity vs. Tier — Two Separate Axes

**Rarity is what bucket a drop came from. Tier is how far Selah investment has pushed that specific piece.** This mirrors the Access/Capacity split already locked for Resonance Fork unlocks — a player who understands one already understands the other.

| Rarity | When it drops | Resonant Level gate | Tier ceiling |
|---|---|---|---|
| Salvage | Early game | None | No upgrade path — fixed |
| Kept | Late-early → early-mid | RL 1 obtainable | Tier 1–2 |
| Remembered | Mid → early endgame | Early RL | Tier 3 |
| Resonant | Endgame | RL approaching cap | Tier 5 |
| Pure | Pilgrimage only | RL capped | Tier 5, class-conditional effect |

**Tier 4 is a deliberate skipped step.** No rarity's natural ceiling lands on it. It exists purely as a numeric waypoint so the jump from Tier 3 to Tier 5 *reads* as a cliff rather than one more increment.

**Resonant Level governs access only.** It does not contribute stat points. It gates which tier ceilings a player can currently reach. All power comes from gear itself.

---

## Per-Piece Stat Budget

Total stat points on one piece, split primary (Resolve/Clarity/Conviction) vs. secondary (itemized):

| Tier | Total budget | Primary | Secondary |
|---|---|---|---|
| Salvage | 6 | 0 — locked out | 6 |
| 1 | 12 | 5 | 7 |
| 2 | 20 | 8 | 12 |
| 3 | 32 | 13 | 19 |
| 4 | 48 | 19 | 29 |
| 5 | 100 | 40 | 60 |

**Full 10-slot loadout totals:** Salvage 60 → T1 120 → T2 200 → T3 320 → T4 480 → **T5 1,000**.

The T5 jump (480 → 1,000, ~2.1×) is the visible cliff — steeper than any prior step, without demanding enemy health pools built around a literal 5× spike. **The Selah cost curve to reach T5 is deliberately a separate, much steeper curve** and is tuned independently. Conflating stat growth with cost growth would force every enemy in the game to assume a power spike that only exists in the wallet.

### Primary Split — 40/40/20

Within a piece's primary budget, the two identity stats for the class it dropped for share 80% roughly evenly; the off-stat takes 20%. A Warden piece at Tier 3 (13 primary) lands near **5/5/3**.

**Open, flagged and unresolved:** the class docs imply *high/medium/low*, not *high/high/low* — the Hunter is documented as "Endure is the full kit; Repay is only the Covenant/super." Whether 40/40/20 is universal or needs to flex per class can't be answered until Warden and Penitent get the same treatment.

---

## Secondary Stat Catalog

**The test every entry passes:** it must have a real "who does this help / who does it not help" answer. A stat that helps everyone equally is a mandatory pick, which is not a choice — it's a tax with extra steps.

**Flat universal weapon/ability damage was explicitly rejected for failing this test.** Raw power lives outside the rollable pool entirely.

| Group | Stats | What it rewards |
|---|---|---|
| **Weapon-class damage** | Melee, Pistol, Rifle, Throwable | Whichever weapon type is actually equipped. Maps directly onto the existing `EGothicCrosshairType` enum, so abilities inherit their weapon class automatically (Hunter's Strike is Melee, Slicer is Throwable) with no extra tagging |
| **Covenant** | Covenant Power, Covenant Duration | Investment in the Covenant/super specifically, separate from weapon-class scaling |
| **Per-ability (narrow)** | Templated roll — cooldown or damage on one named ability | A sharp bet on one piece of the kit; worthless unless that exact ability/variant is equipped |
| **Weapon handling** | Reload speed, magazine size, Steadfast conversion efficiency | Sustained-fire vs. burst/melee playstyles |
| **Survivability** | Movement speed, evasion, healing received | Defensive/mobility-lean builds |
| **Resistance** | Predatory, Feral, Wraith, Assembled | Content-specific prep — dead weight against the wrong Shaped type, so never universal |

**Gear Power sits outside this pool.** Raw effectiveness is baked flatly into Tier, guaranteed, not chosen. This is what actually solves the "wouldn't flat damage always be priority one" problem: nobody *picks* Gear Power, because it isn't a decision. Choice-stats answer "what am I good at." Gear Power answers "how strong is this object."

~16 rollable stats across 6 groups. Any single piece rolls only 2–4 lines. **The pool being wide is what makes chasing a specific piece meaningful; any one item staying narrow is what keeps it legible at a glance.**

---

## Roll Variance — Never Zero

**This reverses the July 14 ruling** that variance shrinks to zero at Tier 5. That rule existed to match "Pure Selah gear is static," which this document removes — so it was orphaned regardless. It's being reversed on its own merits, not just by dependency.

| Tier | Lines rolled | Variance |
|---|---|---|
| Salvage | 1, fixed | None — no chase intended |
| 1 | 2 | ±40% |
| 2 | 2–3 | ±35% |
| 3 | 3 | ±30% |
| 4 | 3–4 | ±25% |
| 5 | 4 | **±20% — never zero** |

**Why variance persists at the top.** A deterministic endgame has no chase. If every Tier 5 piece of a given base is identical, the only goal is "reach Tier 5," and the moment you do, gear is over. Randomness is what makes the ceiling personal — the player decides where their upper echelon is, and that decision is only real if a better roll is always theoretically out there.

**This also resolves a quiet disagreement between two locked systems.** Variance→0 promised a completed item at the same time the sunset lifecycle promised nothing is ever completed. Strain rises, the piece grows too heavy, you set it down. Nothing in this game is finished — not the roll, not the tenure. The two rules now agree.

**Line selection is the larger variance axis, not the ±%.** With 4 lines drawn from ~16 stats, *which* four you got matters far more than whether they rolled high. The perfect-roll chase is primarily "did I get the right stats," and only secondarily "did they roll well."

---

## Re-Rolling at Sean

Selah has two jobs at the Binder: **stars** (completion — pushing a piece toward its ceiling) and **re-rolls** (refinement — a fresh roll of the secondary lines).

**The rule that makes this work: re-rolls are random, never directed.** You pay Selah, you get a fresh roll of the whole rollable pool, you keep it or you pay again. **You never pick a stat.**

This is not fussiness — it's the difference between the system doing its job and doing the opposite. Directed re-rolling gets solved in a week: every player of a spec runs the identical spread, looked up rather than discovered, and the chase becomes a chore performed once per drop. It doesn't create a chase; it deletes one and bills for the privilege. Random re-rolling is the only version where "the player determines their upper echelon" is a true statement, because the real decision is **when to stop**, and that's personal.

**Pure traits never re-roll.** Only secondary lines. The class-conditional, build-defining trait is what the item *is*. Sean can sharpen a Pure item. He cannot change what it does.

**Amendment to ECONOMY_SYSTEM.md's "grinding currency alone cannot buy best-in-slot":** the rule stands, with its scope made explicit. **Selah buys attempts, not outcomes.** The drop still sets the ceiling, luck still sets the roll, and a player with a mountain of Selah and a bad piece still has a bad piece.

**Open — re-roll cost model.** Recommended: cost scales with the piece's **tier**, flat per attempt. Rejected alternative: escalating cost per attempt on the same piece, which punishes exactly the player who found a piece worth committing to, and Strain already prices deep commitment.

---

## Drop Sources

**Why the Accursed drop gear at all:** Thrall, Retained, and Lucid are transformed humans, not spawned monsters. Their drops are recovered remnants of who they were before the Bleed took them. **The Hollow are pre-human and drop categorically different rewards** — not gear.

| Enemy tier | Drops |
|---|---|
| Thrall | Salvage, Kept |
| Retained | Kept, Remembered |
| Lucid | Remembered, **Resonant** |
| Hollow | Not gear — reward currency undesigned |

**Lucid no longer drops Pure.** This is the change that closes the Pure Selah / Unique overlap. Lucid tops out at Resonant and instead carries the highest Pure Selah drop rate — which `ECONOMY_SYSTEM.md` already justifies: *higher-tier Accursed had stronger wills in life, so their Selah is more likely to crystallize pure.* Same enemy, same reward pressure, one less path.

---

## Resonant vs. Pure

The distinction is **how much** versus **how**.

**Resonant** items are stat-stick monsters — best-in-slot numbers, additive perks. Reference: Destiny legendaries, Diablo legendaries. They make you stronger at what you already do.

**Pure** items are semi-fixed, with a build-defining trait that changes player *behavior*, not just numbers. Reference: Destiny exotics, WoW's Windfury. Explicitly rejected: Final Fantasy's all-stat-stick approach, which makes the rarest item a bigger version of a common one.

**Pure traits are class-conditional** — the item reads which class bound it and its effect changes accordingly. No reference game does this. The consequence is deliberate and valuable: **an off-class Pure drop retains real trade value inside a Kindle** rather than being dusted for currency.

**Bonded pieces** are the set-bonus equivalent — multiple items recovered from **the same Accursed**. The bonus is the fiction: you have gathered more of one person.

---

## The Pilgrimage — The Only Path to Pure

**Locked this session, closing the open item.** Pure gear does not drop. It is earned exclusively through the Pilgrimage, triggered by Pure Selah.

This makes both halves better. Pure Selah stops being a currency in search of a job and becomes the only door to the top tier. The Pilgrimage stops being a one-off narrative cul-de-sac and becomes endgame content. Nothing else in the game does what either now does.

### Three Fragments, One Life

Each Accursed source yields **one of three possible Pure items**, each carrying a fragment of that Accursed's life before the Bleed. **The three fragments are three pieces of the same person's story. Collecting all three is how you come to understand who they were.**

This is the load-bearing decision of the whole system, because it makes the item chase and the lore chase **the same action**. You re-run the Bestial Lucid's Pilgrimage not for a better number, but to finish knowing her.

### Why Farming the Pilgrimage Is Structurally Impossible

`ECONOMY_SYSTEM.md` warned that variance on Pilgrimage items *"would implicitly ask players to repeat a deeply personal narrative mission for a marginally better number, which corrodes the thing that makes the item meaningful."* That warning is correct and is honored here — not by banning variance, but by **making the Pilgrimage unable to grant a roll you already have access to improving.**

| Activity | Currency | Yields | Never yields |
|---|---|---|---|
| **Pilgrimage** | Pure Selah | A new Pure item + a fragment of a life | Better numbers on something you already own |
| **Sean** | Selah | Stars, and re-rolls | New items, new lore |

Each activity does one job and **cannot be made to do the other's**. Nobody ever grinds a person's death for a stat, because the Pilgrimage can't give them one. The roll chase is permanent and lives entirely at the Binder, where it costs Selah and no one's memory.

Once all three of a source's fragments are recovered, that source's Pilgrimage has nothing left to give — and Pure Selah is spent elsewhere, on someone whose story you don't have yet.

---

## Resonance Strain

- Strain is **per slot**, not a flat loadout tax.
- Strain **increases as Selah is imbued** — a function of investment depth, not of merely wearing bound gear.
- **Salvage, Kept, and Remembered cost zero Strain**, since they never touch Selah.

**Consequence, and the reason ten slots works:** under a flat per-item tax, ten slots against a fixed cap of 100 would mean every item is trivially affordable and the cap never bites. Under per-slot investment-scaled strain, the question changes from *"which slots can I fill?"* (all ten, from day one, with mundane gear, at zero cost) to **"which slots do I commit to?"** Ten slots is the right size for that question — a five-slot loadout would make the commitment decision too small to be interesting.

Pushing one piece to its full ceiling can cost more Strain than lightly imbuing two. A lightly-imbued Pure item with a build-defining trait may be cheaper to wear than a maxed Resonant with better raw stats. That is a real build decision arising from arithmetic, not from a designer's hand.

**Open:** whether Bonded armor receives a Strain discount for pieces from the same Accursed. Current ruling: per-slot, no discount. Revisit if Bonded sets feel punishing against cherry-picked best-in-slot once in playtesting.

---

## Progression Summary — Three Independent Questions

1. **"How strong can this specific item become?"** → Rarity / star ceiling, set at drop.
2. **"Can I afford to make it that strong?"** → Selah, spent on stars and re-rolls.
3. **"Can I use all my strong gear at once?"** → Resonance Strain, gated by Resonant Level.

Kept deliberately separate. None can be rebalanced into cannibalizing another.

---

## Amendments to ECONOMY_SYSTEM.md

Listed explicitly so the contradictions are visible and deliberate. That document is marked *Locked*; these edits are a decision, not a drift.

| Was | Now | Why |
|---|---|---|
| "Pure Selah / Pilgrimage items have **static stats**. No variance, no rolling." | Pure items roll, and re-roll at Sean. | The rule was derived from "Pilgrimage items are one-time narrative payloads." The Pilgrimage is now repeatable content with three destinations per source. The doc's own principle — *"variance belongs wherever repetition is desired"* — now argues for variance, not against it. The corrosion it feared is prevented structurally instead (see above). |
| "A player's gear ceiling is capped by their best drop, not their Selah stockpile." | Unchanged in force; scope clarified. **Selah buys attempts, not outcomes.** | Re-rolling is grinding currency toward a better piece. Randomness is what keeps the rule's teeth. |
| Lucid enemies enable Unique drops. | Lucid tops out at Resonant. Pure is Pilgrimage-only. | Closes the Pure Selah / Unique overlap — two systems doing one job. |
| Rarity ladder unnamed / generic. | Salvage, Kept, Remembered, Resonant, Pure. | Names now carry the fiction's actual axis and make the Flame-engagement boundary visible. |

---

## Open Items for Future Development

- **Hollow reward currency** — undesigned. The Hollow are pre-human and drop no gear; what they *do* drop is a real gap.
- **Re-roll cost model** — flat-per-tier recommended above; not locked.
- **Primary split per class** — whether 40/40/20 is universal or flexes to high/medium/low per class doctrine. Blocked on Warden and Penitent receiving the same treatment.
- **Selah cost curve** for stars and re-rolls — the only original Progression doc open item still unfilled.
- **Shaped-direction loot modulation** — deferred until Shaped are implemented.
- **Bonded armor Strain discount** — deferred pending playtest feel.
- **Academy as a gear source** — whether it relates to this drop-table system at all, or is a purely crafted, non-random path.
- **Per-stat value-per-point and roll ranges** — the numbers under the budgets.

---

*Document generated: July 2026*
*Session: Vigil Design — Itemization & Loot (consolidation of two uncommitted July 14 sessions, plus Pilgrimage/Pure reconciliation)*
