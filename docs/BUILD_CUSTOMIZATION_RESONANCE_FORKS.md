# Build Customization — The Resonance Fork Model
**Vigil — Class-Agnostic Systems Design Document**
*Status: Locked — Core Structure*

---

## Purpose of This Document

This document exists as the standalone, class-agnostic reference for Vigil's build customization system. The system is referenced in the Hunter Class Kit doc (as its concrete first implementation) and in the Progression Pillar doc (as part of the broader Resonant Level structure). This document isolates the *system itself* — the reasoning, the structure, the rules — so it can be handed to anyone designing a new class's kit without requiring them to extract it from two other documents.

---

## The Problem This System Solves

Vigil needed a way to let two players with the same class play it in genuinely different ways — reflecting different playstyles, different builds, different personal expressions of the same core identity — without falling into either of two well-understood failure modes common to the genre.

**Failure mode one: the open talent-tree / point-buy system (WoW-style).** Deep build expression, but creates a combinatorial balance burden that scales far beyond what a small team can sustain. Every node interacts with every other node; the state space becomes effectively untestable. Tends to converge on a single "correct build" most players look up rather than discover, which defeats the purpose of offering choice in the first place.

**Failure mode two: fully adaptive/behavioral-learning systems.** An idea explored and explicitly rejected during design — abilities that quietly mutate based on tracked player behavior (e.g., "your ability has been used at range 60% of the time, so it's now slightly better at range"). Rejected for three specific reasons:
1. **Illegibility** — invisible per-player math means players can't reason about their own kit in real-time combat decisions.
2. **Farmability** — any trackable pattern gets reverse-engineered and farmed by the most engaged players within days, producing the opposite of organic expression.
3. **Server cost** — every damage calculation would need to read variable per-player modifier state instead of a fixed value, a meaningfully heavier hot path at MMO scale.

---

## The Adopted Model: Bounded Resonance Forks

**The core structural trick** is directly modeled on the specific mechanism that makes Destiny's Prismatic subclass work — not the subclass itself, but the underlying permission structure. Prismatic is not new content. It is a rule for which subset of *already fully-authored, already-balanced* Aspects and Fragments from other subclasses a player is allowed to bring into one loadout at once. The feeling of infinite combination comes from recombining a finite, already-tested set of parts.

**The rule, stated plainly:** Each ability slot in a class's kit has a small, closed set of variants — 2, occasionally 3. Each variant is fully hand-authored and balanced **in isolation**, on its own merits, with no dependency on which variants are selected in other slots. A player's build is simply "which variant is currently selected, per slot." There is no cross-ability synergy math to design, tune, or balance.

**Why this is cheap and safe, expressed precisely:** combining individually-safe things is usually safe. The only real risk surface in this model is unexpected *pairings* — a small, enumerable, testable space — not an open-ended build space where the number of possible combinations exceeds what any team could realistically playtest. A kit with 6 ability slots at 3 variants each produces 729 possible loadouts, generated from only 18 individually-designed and balanced pieces of content. The combinatorics create the feeling of build depth; the actual authored content requirement stays bounded and shippable.

---

## Structural Requirements for Any New Ability Variant

When designing a Resonance fork for any class, the following must hold:

1. **The variant must be a complete, standalone expression of the ability** — not a fragment that only makes sense combined with a specific choice in another slot. If a variant only feels good paired with one specific other variant elsewhere in the kit, that's a sign the two should probably be merged into a single variant, or redesigned to stand alone.

2. **The variant must be balanced against the *base* ability and its sibling variants only** — never balanced with reference to other ability slots' variants. This is what keeps the system's total testing surface bounded. The moment a variant's balance depends on "assuming the player also has variant X in another slot," the system has quietly become the open talent-tree model it was designed to avoid.

3. **The variant must express a genuine philosophical difference in playstyle, not just a stat delta.** A variant that's mechanically identical to its sibling but with bigger numbers isn't a meaningful choice — it's a trap option nobody should ever pick over the other. Every variant pair or trio should represent an actual different way of using that tool (see: The Lunge's "Gone" vs. "The Close" — evasive repositioning vs. aggressive closing, not "same dash, more damage").

---

## Unlocking Variants

**Locked principle: narrative-driven, not behavioral-quota-driven.** Unlock conditions are "complete this story or quest beat," never "perform this specific action N times." Grindy, checklist-style conditions (e.g., "throw this ability while airborne 15 times") read as chores and were explicitly rejected in favor of narrative framing during design.

**Two-gate system, shared with the gear/Resonant Level structure documented in the Economy and Progression Pillar docs:**
- **Access** — earned per-variant, through story/quest beats. Answers "have you demonstrated this way of fighting enough to have earned this specific expression of it?"
- **Capacity** — governed by Resonant Level (see Progression Pillar doc). Answers "can you currently hold this many powerful things active at once?"

This mirrors the exact structural pattern used for gear (drop determines ceiling access, Resonant Level determines total equippable capacity). This consistency is intentional and should be preserved by any future system added to Vigil — a player who understands the pattern in one system already understands it in every other system that uses it.

**Unlock cadence guidance:** not every variant needs a bespoke quest. Some should unlock through natural Resonant Level milestones alone — no special task, just "you've grown enough." Reserve dedicated narrative unlock quests for the more flavorful, build-defining variants, the way Destiny's catalysts feel special specifically *because* most weapon perks don't require one. If every variant requires its own quest, completing quests becomes the actual game, rather than playing the class.

---

## Applying This Model to a New Class

When designing a new class's kit, the process is:

1. Establish the class's identity and creed-relationship first (see the Hunter Class Kit doc's structure as the template — identity statement, Prior Flame expression, creed mapping, *then* abilities).
2. Design each ability slot's **base function** — what does this tool do, full stop, with no variants yet.
3. For each slot, ask: are there 2-3 genuinely different philosophies of *using* this tool that are worth making mutually exclusive choices? If yes, design those as Resonance forks. If the ability doesn't have a natural fork point, it's fine for it to ship with no variants — not every ability needs one.
4. Balance each variant against its base ability and siblings only, never against other slots.
5. Decide unlock method per variant: natural Resonant Level milestone, or dedicated narrative quest — reserving the latter for the class's most flavorful, identity-defining choices.

---

## What This Model Deliberately Does Not Provide

Worth stating explicitly so it isn't mistaken for an oversight: this system does not offer stat-point allocation, does not offer cross-ability synergy bonuses, and does not adapt to player behavior. Continuous, incremental character growth is handled by the separate Stat system (Resolve/Clarity/Conviction, see Progression Pillar doc) and by gear itemization (see Economy doc). The Resonance Fork model's job is specifically **discrete, legible, narratively-earned choice about how a tool is used** — nothing more, and it should not be expanded to try to do the stat system's or the economy's job as well.

---

## Design Tenet Alignment

- **Earn Everything** — every variant is earned through play, never purchased, never granted by time alone.
- **Respect the Player's Intelligence** — narrative unlock conditions over behavioral checklists; fully legible, hand-authored variants over hidden adaptive math.
- **All classes must be viable** — the bounded, per-slot balancing requirement is what keeps every class's kit individually testable and correctable, rather than any one class's balance depending on assumptions about another class's kit.

---

## Open Items for Future Development

- Exact variant count ceiling per ability slot (currently "2, occasionally 3" — worth confirming this holds once more than one class's full kit exists)
- Whether some ability slots should be permitted to ship with zero variants at launch, with variants added in later content as a way of keeping an existing class feeling fresh without new ability slots
- Formal template/checklist for evaluating whether a proposed variant pair represents a "genuine philosophical difference" (structural requirement #3) versus a numbers-only difference, to keep this consistent across designers if the team grows

---

*Document generated: July 2026*
*Session: Vigil Design — Build Customization System (Standalone Reference)*
