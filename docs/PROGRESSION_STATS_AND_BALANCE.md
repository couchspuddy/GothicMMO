# The Progression Pillar
**Vigil — Stats, Abilities, and Resonant Level Design Document**
*Status: Locked — Core Structure*

---

## Overview

Vigil's progression is built on a single governing principle: **every system traces back to the creed.** Not as flavor text — as mechanical specification. Where other systems borrow genre conventions and retrofit lore onto them, this pillar was built lore-first and mechanics-second, the same way Steadfast, the Selah loop, and the Economy were.

**The creed as mechanical spine:**

| Creed Word | System | What It Means Mechanically |
|---|---|---|
| Remember | Resonant Level | Earning progression isn't learning something new — it's recovering access to something the Prior Flame already knew. The character remembers; they don't level up. |
| Endure | Abilities | The kit itself. What lets the player never falter, never surrender, once earned. |
| Repay | Economy | The visible proof, returned to the Bleed's collection mechanism, that it gets nothing. Selah, Pure Selah, gear. |

This document covers Resonant Level and Abilities in depth, and introduces the Stat system as the piece that gives Endure a continuous, numeric growth axis alongside its discrete ability unlocks.

---

## Resonant Level

**What it is:** A player-progression stat that governs two separate things:
1. **Capacity** — how much bonded Resonance (gear + ability variants) a player can have active simultaneously
2. **Access gating** — a soft gate on which higher-tier Resonance forks and gear ceilings become usable

**How it's earned — locked principle:** Resonant Level does not rise from passive time played, currency spent, or crafting activity. It rises from **surviving pressure the Bleed puts on the Prior Flame.** This is a direct extension of existing lore: *"The Bleed's failure to extract Antecedent essence actually strengthens the Prior Flame over time. Every encounter with the Accursed puts the Prior Flame under pressure it was specifically built to withstand. And in withstanding it, the flame burns slightly brighter."*

**Qualifying triggers (examples, not exhaustive):**
- Surviving significant damage thresholds, especially Absolution-saved near-deaths
- Vital point kills on higher-tier Accursed (first-time-per-tier especially)
- Selah collected under duress (e.g. the Eagle's Landing interrupted-Selah mechanic)
- First-time kills against categorically new Accursed types

**What this rules out as a trigger:** passive playtime, Selah spent, crafting activity. None of these represent the Flame under pressure — they represent investment or time, which are already covered by other systems (Selah/Economy).

**Structural model:** Milestone/qualifier-based, not a continuous XP bar. Every Resonant Level increase should be traceable to a specific, nameable pressure-event — a story beat, not a stat tick. This is consistent with the pattern used everywhere else in Vigil's design: nothing is arbitrary, everything is traceable to a specific moment.

---

## Abilities — The Resonance Fork Model

### The Core Structural Decision

Vigil rejected two more conventional models before arriving at this one. Both are documented here because the reasoning matters for future ability design:

**Rejected: Open talent-tree / point-buy system (WoW-style).** Genuinely deep build expression, but creates a combinatorial balance burden that scales far beyond what a small team can sustain — every node interacts with every other node, and the state space becomes untestable. Also tends to produce a single "correct build" that most players look up rather than discover.

**Rejected: Fully adaptive/behavioral-learning system.** The idea of abilities quietly mutating based on tracked playstyle (e.g. "your Slicer throws have been 60% at range, so it's now slightly better at range") was explored and deliberately rejected. Three failure modes drove this:
1. **Illegibility** — invisible per-player math means players can't reason about their own kit in real-time combat decisions.
2. **Farmability** — any trackable pattern gets reverse-engineered and farmed by the most engaged players within days, producing the opposite of organic expression.
3. **Server cost** — every damage calculation would need to read variable per-player modifier state instead of a fixed value, a meaningfully heavier hot path at MMO scale.

### The Adopted Model — Bounded Resonance Forks

Directly modeled on the specific structural trick that makes Destiny's Prismatic subclass work: **it is not a new subclass. It is a permission system that lets players combine already-authored, already-balanced variants that exist independently elsewhere.**

**The rule:** Each ability slot has a small, closed set of variants (2-3 per slot), each fully hand-authored and balanced in isolation. A player's build is simply "which variant is currently selected per slot." No cross-ability synergy math, no combo bonuses to balance against each other.

**Why this is cheap and safe:** Combining individually-safe things is usually safe. The only real risk surface is unexpected *pairings*, which is a small, testable space — not an open-ended build space. A kit with 6 ability slots at 3 variants each produces 729 possible loadouts from only 18 individually-designed and balanced pieces of content. The combinatorics create the feeling of infinite build depth; the actual content requirement stays bounded.

**Example (Hunter, illustrative):** Opener Slicer + Defensive Lunge + extended-duration Reckoning is a complete, coherent, personal build — assembled entirely from independently-balanced pieces, with zero interaction math required between them.

### Unlocking Variants

**Locked principle:** Variant unlocks are narrative-driven, not behavioral-quota-driven. "Complete this story/quest beat" rather than "perform this specific action N times." Grindy, checklist-style unlock conditions (e.g. "throw Slicer while airborne 15 times") read as chores and were explicitly rejected in favor of narrative framing.

**Two-gate system, mirrored from the Economy's gear model:**
- **Access** — earned per-variant, through story/quest beats. "Have you demonstrated this way of fighting enough to have earned this specific expression of it?"
- **Capacity** — governed by Resonant Level. "Can you currently hold this many powerful things active at once?"

This mirrors the exact structural pattern used for gear (drop determines ceiling access, Resonant Level determines total equippable capacity). A player who understands one system already understands the other — this consistency is intentional and should be preserved in any future system added to Vigil.

**Unlock cadence guidance:** Not every variant needs a bespoke quest. Some should unlock through natural Resonant Level milestones alone (no special task — "you've grown enough"). Reserve dedicated narrative unlock quests for the more flavorful, build-defining variants, similar to how Destiny's catalysts feel special specifically because most perks don't require one. If everything requires a quest, completing quests becomes the game instead of playing the class.

---

## The Stat System

### Why Stats Are Necessary Alongside Abilities

Abilities and Resonance forks are binary/categorical — you either have access to a variant or you don't, and choosing between variants is a discrete decision. This leaves no continuous axis for a player to feel incremental growth moment-to-moment. Stats are that axis. Additionally, the existing gear-roll variance system (established in the Economy document) requires *something* to roll variance on — stats are the missing piece that system was implicitly assuming existed.

### The Two-Tier Structure

**Primary Stats — creed-mapped, gear-budget stats (3 total):**

| Stat | Creed | Governs |
|---|---|---|
| Resolve | Endure | Damage mitigation curve, health pool scaling, Absolution health-pool size |
| Clarity | Remember | Vital point detection duration/range, ability cooldown rate, precision/crit-style bonus damage |
| Conviction | Repay | Steadfast generation rate, ability charge/resource rate, Selah yield bonus |

**Secondary Stats — flat, itemized, gear-rolled values:**
- Flat weapon/ability damage
- Movement speed
- Evasion / dodge chance
- Healing received / healing per second
- Accursed-type resistance (anti-Feral, anti-Predatory, etc.)

**Why two tiers instead of one flat list:** Nine-plus distinct systems (health, damage, mitigation, healing, cooldowns, ability damage, Steadfast generation, movement, evasion, resistance) cannot cleanly map to three stats without each primary stat silently doing triple or quadruple duty — which makes the stats *harder* to reason about and *worse* for the dump-stat problem, not better, because "maximize Resolve" would secretly mean "maximize four unrelated things at once," making it an even more obviously dominant universal choice. Splitting primary (fiction-facing, creed-mapped) from secondary (itemization-facing, flat) solves this without diluting the primary trio's clarity.

### Solving the Dump-Stat Problem

**The core failure mode:** Any stat system where each stat governs a cleanly separable, non-interacting set of effects will get mathematically solved by the playerbase, and once solved, the answer never changes — permanently deadening the other stats. This happened to Destiny's original stat system (Recovery dominance for years) and will happen here by default unless deliberately prevented.

**Mechanisms adopted to prevent this:**

**1. Threshold-gated ability behavior (primary mechanism, locked).** Rather than smooth scaling, stats should gate qualitative changes in ability behavior at specific breakpoints. Example: The Read shows one shift of vital-point prediction below a Clarity threshold, two shifts above it. This makes stat investment a genuine strategic decision about breakpoints rather than a simple slope-maximization problem. This directly extends the tiered-threshold design already proven in the Steadfast system (low/mid/high ammo tiers) — Vigil already has a successful precedent for this exact pattern.

**2. Per-class breakpoint variance (adopted, needs eventual implementation detail).** The same primary stat should have different optimal thresholds per class, because different class abilities should read the same stat differently. This is the actual mechanism behind achieving "the efficient path should differ per class" rather than a universally-agreed-upon best stat existing across the whole game. Requires class abilities to structurally key off stats differently, not just be weighted differently.

**3. Stat-pairs with trade-off resource pools (documented as a stretch goal, not a launch requirement).** A harder-to-tune, higher-payoff option: linking two primary stats to a shared soft resource pool so investing in one costs some of the other, forcing a real choice rather than independent maximization. Flagged as more complex to balance correctly — revisit post-launch if the simpler mechanisms above prove insufficient.

**Explicit acknowledgment — dump stats will always exist, and that's acceptable.** There is always a most-efficient path in any stat system; pretending otherwise is not a goal worth chasing. The actual goal, and the one this structure is built to achieve, is that the most-efficient path differs meaningfully **by class**, rather than a single stat being universally correct for every class in the game.

---

## Universal Kits vs. Activity-Specific Kits

**The question:** Should Accursed-type resistance (anti-Feral, anti-Predatory, etc.) be impactful enough that players are expected to swap gear loadouts per activity/zone?

**Decision — lean toward broadly stable kits, encounter design carries the differentiation.** Accursed-type resistance should exist as a real secondary stat, present in the fiction and itemization, but kept at low-to-moderate impact rather than being a hard-optimal gear-check.

**Rationale:** Vigil's encounter design philosophy is built on making fights feel distinct through *behavior* — the Bestial Lucid's destruction-phase shift, the interrupted Selah wave in Eagle's Landing Encounter 3, the Retained observing from a window before engaging. If Accursed-type resistance becomes a mandatory pre-fight gear-check, players will start optimizing loadouts against a resistance chart instead of reading and responding to each encounter's specific designed behavior — directly undercutting the narrative-first encounter design work already invested across the game. Let good encounter design carry the "this fight feels different" job; let gear carry a lighter, flavor-supporting version of the same idea.

---

## Diagnosing Balance Problems — Numbers vs. Encounter Design

**The trap to avoid:** Treating every overperformance complaint as automatically an encounter design failure, out of overcorrection against the more common "nerf everything interesting" anti-pattern.

**The actual diagnostic test:** If a kit/stat/weapon type is overperforming in **one specific encounter shape**, that's an encounter design problem — fix the fight, not the kit. If a kit/stat/weapon type is overperforming **universally, across every encounter shape currently in the game**, that is a numbers problem — no amount of individual encounter tuning can compensate for a kit that is mathematically ahead of its peers in every context.

**The practical tell:** Would fixing *one* encounter solve the problem, or would every encounter in the game need to be redesigned to compensate? If it's the latter, the baseline numbers are wrong and should be adjusted directly rather than pursuing a months-long encounter-redesign effort to route around a numbers problem.

---

## Live Balance Philosophy

### Core Principle: Player Data Is Ground Truth

**The philosophy, stated directly:** When players heavily favor one option and ignore another, this is not a deviation from the design to be corrected by forcing players back toward the "intended" path. It is data about the system, and the correct response is diagnosis, not automatic symmetric rebalancing.

**Why this matters:** No design team, however skilled, can out-predict a full playerbase's worth of parallel real-world experimentation at scale. Treating player divergence as "wrong" discards the single most valuable source of design information available post-launch.

**What "diagnose, don't default" looks like in practice:** The same raw usage number (e.g. "80% of Hunters run Clarity-heavy builds") can indicate three completely different underlying problems:
1. Clarity is mathematically dominant (an actual balance problem — fix the numbers)
2. Clarity-stacking feels good in a way the other primary stats don't (a design-quality problem with Resolve/Conviction, not a balance problem with Clarity)
3. Itemization has only been offering good Clarity rolls, and players haven't been meaningfully offered the alternative yet (an itemization/loot-pool problem, not a stat-design problem)

Each diagnosis requires a completely different fix. Applying the wrong one wastes development time and can actively make the system worse.

### Operational Requirements This Implies

**Telemetry must exist before the philosophy can be applied.** Concrete tracking needed at minimum:
- Primary stat allocation distribution, per class
- Secondary stat roll chase patterns (via the Resonance Lottery / trade system)
- Resonance-fork variant equip rates once unlocked
- All of the above **segmented by content type**, not only aggregate — a combo dominant in raid content but dead everywhere else is a different problem than one dead everywhere.

**Numbers alone are insufficient — qualitative player feedback is required alongside telemetry** to distinguish between the three diagnosis categories above. Forums, direct playtesting sessions, and community feedback channels are not optional supplements to this philosophy — they are load-bearing for it.

**This philosophy is a resourcing commitment, not just a stated value.** Choosing to "respond to what players show us" trades lower up-front design effort (no attempt at perfect pre-launch prediction) for higher sustained post-launch effort (regular, responsive balance patch cadence). For a small team, this trade is very likely correct — perfect pre-launch balance is not achievable regardless of effort spent chasing it — but it must be explicitly resourced and scheduled as ongoing work, not treated as something that happens automatically alongside new content production. The most common failure mode for this philosophy in real studios is stating it once and then quietly abandoning it because content production absorbs all available time.

---

## Design Tenet Alignment

- **Every system serves the feeling** — Resonant Level's "recovering a memory" framing over "leveling up" keeps even a core RPG progression stat emotionally aligned with Remember/Endure/Repay rather than being a generic number-go-up system.
- **Earn Everything** — narrative-driven ability unlocks and pressure-driven Resonant Level gains ensure nothing in this pillar is purchasable or time-gated in a way that bypasses genuine engagement with the game's fiction.
- **All classes must be viable** — per-class stat breakpoint variance is the specific mechanism intended to ensure "the optimal path" differs by class rather than converging on one universally-dominant stat across the whole game.
- **Respect the player's intelligence** — narrative unlock conditions over behavioral quota checklists; threshold-based stat design over hidden/adaptive modifier systems. Every number the player can act on should be legible to them.

---

## Open Items for Future Development

- Exact primary stat breakpoint values, per class (requires playtesting once classes beyond Hunter exist)
- Whether stat-pair trade-off resource pools are worth the added tuning complexity post-launch
- Secondary stat itemization budget per gear ceiling tier
- Formal telemetry dashboard requirements for the live balance philosophy to actually be executable at launch
- Accursed-type resistance exact magnitude tuning, once encounter design differentiation is further along

---

*Document generated: July 2026*
*Session: Vigil Design — Progression, Stats, and Live Balance Philosophy*
