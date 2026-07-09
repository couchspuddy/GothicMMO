# Steadfast System
**Vigil — Resource Design Document**
*Status: Locked*

---

## Definition

**Steadfast** is a passive accumulation meter that fills through combat presence and converts on demand into weapon ammunition. It is the mechanical expression of the creed's **Endure** — the longer an Antecedent stays in a fight, absorbs what comes, and refuses to break, the more they can sustain themselves within it.

---

## Core Mechanics

### Accumulation
- Fills passively through combat presence
- No decay — Steadfast persists between encounters and across the full contract
- Fill rate and fill triggers vary by class, expressing each class's identity (see Class Expressions below)

### Spending
- **Tap reload** — reloads from existing ammo reserves (standard behavior)
- **Hold reload** — converts Steadfast meter into ammo reserves for the currently readied weapon
- Players may top off at any point — not restricted to when dry
- Player manages their own efficiency; the system imposes no guidance on when to spend

### Output Tier
- The currently readied weapon determines the ammo tier produced and the Steadfast cost
- Lower tier weapons cost less Steadfast, produce basic rounds
- Higher tier weapons cost more Steadfast, produce specialized or heavy munitions
- Switching weapons before holding reload changes both cost and output

### Ammo Tiers
| Tier | Output | Cost | Notes |
|---|---|---|---|
| Low | Basic improvised rounds | Low | Functional, reduced damage |
| Mid | Silver rounds | Medium | Comparable to standard loadout |
| High | Heavy / specialized munitions | High | High damage, possible Accursed-type effects |

---

## Design Rationale

### Why not ammo caches?
Lore-breaking. Who placed them, and why? If silver rounds require Prior Flame interaction to be fully effective, non-Antecedent cache placement raises questions the world cannot cleanly answer.

### Why not health-for-ammo?
Contradicts the Absolution system (which already uses health as a survival resource). Creates perverse incentives — players deliberately staying low health to maintain a free ammo source undermines the weight of health as a meaningful stat.

### Why not super meter conversion?
The super meter already has a dedicated purpose — building toward each class's Covenant ability. Splitting it into two functions dilutes both and creates cognitive load.

### Why Steadfast works
- Resource comes from the fight itself, not from external placement
- Non-Antecedents remain viable fighters using conventional munitions — they simply cannot do this
- The trade decision (fill now vs. save meter for better ammo later) is meaningful without being punishing
- No decay means the player is never penalized for thorough play or careful pacing

---

## Class Expressions

Same meter. Same output structure. Different fill conditions per class.

| Class | Fill Condition |
|---|---|
| Hunter | Sustained engagement, observation time before combat, methodical clearing |
| Warden | Absorbing damage, holding ground under pressure |
| Penitent | Proximity to Accursed death, Selah moments |
| Academic | TBD — likely perception-based, pattern recognition |

---

## Encounter Design Implications

- Steadfast must fill meaningfully within a single encounter — players should have at least one viable spend opportunity per engagement if they stay fully present
- Weapon switching becomes part of the Steadfast economy — a player may switch to a cheaper weapon specifically to fill it and preserve Steadfast for a more expensive one later
- Players entering an encounter with low reserves but high Steadfast are in a different but equally viable position than those with full ammo and an empty meter
- This variance means two players running the same contract will have genuinely different experiences based on resource management across prior encounters

---

## UI Requirements

- Steadfast meter visible at all times during combat
- Cost of filling current weapon implied by weapon readied — no tooltip required
- Players learn cost tiers through play
- Hold-to-reload input must be clearly distinct from tap-to-reload at the input binding level

---

## Open Questions

- Exact fill rate tuning per class (requires playtesting)
- Whether heavy ammo output varies by class or is universal
- Whether Steadfast has a visible tier indicator or is a smooth meter

---

*Document generated: July 2026*
*Session: Vigil Design — Encounter Systems*
