# Steadfast & Selah Loop
**Vigil — Resource & Pacing Design Document**
*Status: Locked*

---

## Overview

Two systems operating at different time scales that work together to create Vigil's core resource rhythm.

**Steadfast** manages ammo scarcity *within* an encounter.
**The Selah Moment** resets ammo *between* encounters.

Neither competes with the other. Together they create a loop where the player is always resource-managing during combat and always rewarded for honoring the pause after it.

---

## Steadfast

### Definition
A passive accumulation meter that fills through combat presence and converts on demand into weapon ammunition. The mechanical expression of Endure — the longer an Antecedent stays in a fight, absorbs what comes, and refuses to break, the more they can sustain themselves within it.

### Mechanics
- Fills passively through combat presence
- No decay — persists between encounters and across the full contract
- **Tap reload** — reloads from existing reserves (standard)
- **Hold reload** — converts Steadfast into ammo reserves for the currently readied weapon
- Players may top off at any point, not just when dry
- Cost tier and output tier determined by currently readied weapon

### Ammo Tiers
| Tier | Output | Cost |
|---|---|---|
| Low | Basic improvised rounds | Low Steadfast |
| Mid | Silver rounds | Medium Steadfast |
| High | Heavy / specialized munitions | High Steadfast |

### Class Fill Conditions
| Class | Fill Condition |
|---|---|
| Hunter | Sustained engagement, methodical clearing |
| Warden | Absorbing damage, holding ground |
| Penitent | Proximity to Accursed death, Selah moments |
| Academic | TBD — perception-based |

---

## The Selah Moment — Mechanical Layer

### What It Does
After the last enemy in an encounter falls, a UI prompt appears inviting the player to collect Selah. This is a deliberate, chosen action — not automatic. The player must stop and take the moment.

**Collecting Selah:**
- Awards Selah (primary progression resource)
- Fully restocks all weapon ammo reserves across all three weapons
- Resets the player's resource state heading into the next encounter

### Why It Works
The Selah moment was already the most important five seconds in the game loop narratively. It is now also mechanically significant. Players who honor it get a clean slate. Players who rush past it carry their resource deficit into the next encounter.

The design teaches players to slow down through incentive, not instruction.

### UI Behavior
- Prompt appears only after an encounter is fully cleared — not during combat
- A single clear call to action: collect, pause, move on
- The moment the player collects, ammo restocks and Selah is awarded simultaneously
- The pause between prompt appearance and player response is the design working as intended — that hesitation is the breath the world is offering

### Current Implementation
- Full restock across all three weapons on every Selah collection
- Subject to tuning based on playtesting — proportional rewards (based on Steadfast built during encounter) are a future consideration

---

## The Combined Loop

```
Enter encounter → Steadfast fills through combat presence
                → Player manages ammo vs. Steadfast conversion mid-fight
                → Encounter ends
                → Selah prompt appears
                → Player collects → Full ammo restock + Selah awarded
                → Move to next encounter
```

**What this means for encounter design:**
- Steadfast is for managing scarcity within a single fight
- Selah is the guaranteed reset between fights
- Players arrive at each encounter with full ammo if they honored the previous Selah moment
- Players who skip Selah moments are playing a harder game by choice

---

## Narrative Alignment

The Selah moment collecting ammo is the creed made mechanical:

- **Remember** — the prompt asks the player to stop and acknowledge what just happened
- **Endure** — Steadfast rewards staying in the fight
- **Repay** — the restock is what the encounter paid back

The Accursed are never monsters. Every Selah moment is the game asking the player to acknowledge that the thing they just killed was a person. The ammo restock is what that acknowledgment earns. Rushing past it costs something real.

---

## Multiplayer Scope — Confirmed Per-Player

**Steadfast is a strictly per-player resource, never pooled or shared across a party.** Each player fills and spends their own meter independently, using their own class-specific fill triggers, regardless of party size or content type.

**Rationale:** Steadfast is designed to reward *individual presence and performance in the fight*, not collective party effort. Pooling it would let a player who contributes less to a fight draw on ammo they didn't personally earn, which directly contradicts the locked design tenet that players manage their own resource efficiency. Personal responsibility for spending Steadfast wisely is what creates its skill ceiling — diluting that into a shared pool would flatten it. This applies uniformly across solo, Contract, and (pending future design) raid content.

**What still requires tuning, not redesign:** class-specific fill *rates* may need adjustment once played in group content, since group encounters change how often a given class's fill trigger actually fires (e.g., a Warden drawing more aggro in a four-player fight fills faster than they would solo, purely as a byproduct of group combat dynamics, not because the system changed). This is a numbers-tuning task for playtesting, not an open design question.

## Open Questions
- Exact Steadfast fill rate per class (requires playtesting, now including group-content-specific tuning per above)
- Whether heavy ammo output varies by class or is universal
- Whether proportional Selah restocking replaces full restock in later content tiers
- Steadfast UI — smooth meter vs. visible tier indicators

---

*Document generated: July 2026*
*Session: Vigil Design — Encounter Systems*
