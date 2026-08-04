# Design Tenets
**Vigil — The Governing Principles Every System Is Checked Against**
*Status: Locked — Foundational*

---

## Purpose

Every design document in this project ends with a "Design Tenet Alignment" section, citing these four principles by name. Until now, that citation pointed at nothing — the tenets were established in early design conversation and used consistently ever since, but never written down as their own entry. This document is that entry. It does not introduce new rules; the first three are backfilled from how they're already applied across `ECONOMY_SYSTEM.md`, `PROGRESSION_STATS_AND_BALANCE.md`, `RESONANCE_STRAIN_AND_GEAR_LIFECYCLE.md`, `BUILD_CUSTOMIZATION_RESONANCE_FORKS.md`, and `TONE_AND_SENSORY_BIBLE.md`. The fourth was locked in design discussion on 2026-08-04, alongside the weapon perk system.

**How to use this document:** when designing any new system, check it against all four. A system that fails one isn't automatically wrong — but the failure should be a stated, deliberate trade-off in that system's own doc, not a silent gap. This is the same discipline `ITEMIZATION_AND_LOOT.md` already applies to itself when it amends `ECONOMY_SYSTEM.md`: contradictions get listed explicitly, not resolved quietly.

---

## Earn Everything

**No axis of player power is purchasable with anything but time, skill, or drops.** Currency can buy attempts at a better outcome — Selah spent on stars and re-rolls, for instance — but never the outcome itself, and never a shortcut around genuine engagement with the game's systems.

This shows up as: Selah's absolute non-tradeability, so no marketplace or RMT surface can exist around it. Resonant Level rising only from surviving Bleed pressure — never from playtime, currency spent, or crafting activity. Resonance Fork variants unlocking only through story/quest beats, never purchased or granted by time alone. Reintroduced legendary items requiring a fresh playthrough of old content for a later-converging copy, rather than being simply bought back.

**The test:** if a player with unlimited currency and unlimited time, but zero actual play skill or story progress, could acquire it — it violates this tenet.

---

## Every System Serves the Feeling

**A mechanic is not finished when it works. It is finished when it makes the player feel the thing Vigil is about** — that the Accursed were people, that what's carried has weight, that survival is never trivial. Systems built purely for genre-convention reasons, with lore applied afterward as flavor text, read as hollow even when mechanically sound.

This shows up as: Resonant Level framed as "recovering a memory the Prior Flame already knew," not "leveling up," specifically so a core RPG progression stat stays emotionally aligned with the creed instead of becoming a generic number-go-up system. The Pilgrimage being structurally impossible to farm for a better roll, because farming a person's death for a stat would corrode the exact thing that makes the item meaningful. The Resonance Strain sunset mechanism running on the same emotional logic as Selah itself — humanity carried has weight, and that weight is never trivial, even inside a gear-progression spreadsheet.

**The test:** could this system's numbers be reskinned into a generic fantasy MMO without anyone noticing? If yes, the fiction hasn't actually done any work yet.

---

## Respect the Player's Intelligence

**Every number the player can act on should be legible to them, and every system should assume the player is capable of understanding it in full**, not just enough to be steered by it. This runs in the opposite direction from "Every System Serves the Feeling" more often than it might seem — the correct resolution is legible mechanics wrapped in poetic framing, not vague mechanics excused by poetic framing.

This shows up as: narrative unlock conditions over grindy behavioral-quota checklists, because a checklist assumes the player needs to be tricked into engagement rather than trusted to want it. Threshold-based stat design and fully hand-authored Resonance Fork variants over hidden, adaptive, farmable modifier systems. Visible lottery odds at Sean, rather than obscured ones. The Resonance bar existing as a precise, always-visible readout — the Strain system's lore framing is poetic, but a player should never be confused about why an item can or can't currently be equipped.

**The test:** can a player who cares to look explain, correctly, why the system just did what it did? If the honest answer requires "trust me" or hidden math, it fails this tenet regardless of how good the fiction sounds.

---

## Tools, Not Puzzles

**Vigil doesn't have one correct answer per challenge — it has capability the player brings and applies on their own terms.** The designer's job is arming the player to overcome what the game presents, not authoring the solution and hiding it behind a gear check. A system that funnels every player toward the same loadout for the same content has quietly become a puzzle with a memorized answer, whatever else it calls itself. Vigil cannot be beaten in the narrative sense — but the player should always feel like they're beating it anyway, on a build that's theirs.

*Locked 2026-08-04, in the same discussion that shaped the weapon perk system's design philosophy.* The immediate precedent this generalizes: `PROGRESSION_STATS_AND_BALANCE.md`'s decision to keep Accursed-type resistance at low-to-moderate impact rather than a hard gear-check, specifically so players engage with each encounter's designed behavior instead of optimizing against a resistance chart. Weapon perk pool design should follow the same logic — a wide, non-converging pool where a perk like Dread Report is genuinely great for one playstyle and genuinely bad for another, *permanently*, rather than a pool that funnels toward a small set of statistically-superior "correct" picks once the playerbase solves it.

This tenet is also why perk-pool curation should optimize for **width and legibility of trade-off**, not for narrowing toward "the best" as item tier rises — narrowing toward coveted perks at high tier is the resistance-chart problem relocated to itemization.

**The test:** does this system have a solved, converged "correct" answer that every optimizing player eventually arrives at — or does it stay a live decision, indefinitely, because different playstyles genuinely value different things? If it's the former, it's a puzzle wearing a looter's clothes.

---

## Amendment Log

Kept explicit so additions to this document are visible as deliberate decisions, matching the convention already used for doctrine amendments elsewhere (see `ITEMIZATION_AND_LOOT.md`'s "Amendments to ECONOMY_SYSTEM.md").

| Date | Change |
|---|---|
| 2026-08-04 | Document created. Earn Everything, Every System Serves the Feeling, and Respect the Player's Intelligence backfilled from existing usage across the docs. Tools, Not Puzzles added as a new, fourth tenet. |

---

*Document generated: August 2026*
