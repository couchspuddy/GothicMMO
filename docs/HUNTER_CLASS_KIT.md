# The Hunter — Class Kit Design Document
**Vigil — Class Design Document**
*Status: Locked for Vertical Slice*

---

## Identity Statement

> *"Not a rage. Not a burst of power. A conclusion. The fight was already over. This is just the part where the enemy finds out."*

The Hunter is calculating, observant, and predatory. They do not jump in headlong but they do not wait for the enemy to make the first move. They are the scouters, the trackers — the most predatory of the factions. Like a lightweight boxer finding their openings before committing.

Gruff, weathered, and direct. They speak with meaning and purpose. They annotate everything and leave the importance to someone else. In moments of crisis the Hunters are not the first to respond — because they were likely the ones to discover the crisis.

---

## The Prior Flame in a Hunter

The Prior Flame in a Hunter does not burn outward. It illuminates inward. Where the Bleed consumes humanity by stripping awareness, the Prior Flame does the opposite — it sharpens everything. They see more. They retain more. They process the field faster than anyone around them.

They are not immune to the Bleed because they are strong. They are immune because the Flame made them more human, not less. The Bleed has nothing to take because the Hunter is more fully themselves than the Bleed can erode. That is what makes them terrifying. Not ferocity. Clarity.

The passive abilities stem directly from this. The Prior Flame resists the Bleed by leaning further into humanity — not away from it. For the Hunter, that means the List. The people behind why they fight. The accumulated grief, trial, error, and loss that built them into what they are. The flame says: *those you have lost are not gone from you.* And breathes it outward.

---

## The Creed

**Endure** is the whole kit. Every ability is an expression of endurance — the patience, the observation, the preparation, the controlled engagement.

**Repay** is only the Covenant. Not repayment in fury. Repayment in certainty. The Hunter does not get angry. They get final.

---

## Weapon Agnostic Design Principle

The Hunter class is weapon agnostic. Abilities are not tied to specific weapons — they are abilities the Hunter brings to the weapon. Something intrinsic to them that expresses through whatever they are holding. The weapon is a conduit, not the source. This scales cleanly to any weapon because the ability is always about what the Hunter puts into it.

---

## Kit Overview

| Ability | Type | Status |
|---|---|---|
| The Loved and The Lost | Passive | Locked |
| Not At All | Passive | Locked |
| The Slicer | Weapon Ability | Locked |
| The Lunge | Movement Ability | Locked |
| The Read | Tactical Ability | Locked |
| The Reckoning | Covenant / Super | Locked |

---

## Passive Abilities

### The Loved and The Lost

**What it is:**
As the Hunter engages in combat, the names of those they have lost begin to manifest through the Prior Flame. The List pushing outward — the people behind why the Hunter fights becoming fuel for how they fight.

**Mechanical expression:**
- While in active combat, the Hunter gains increased ability regeneration
- Steadfast accumulation rate increases
- Both gains are capped — the Prior Flame sustains, it does not overwhelm
- Ramps up through sustained engagement, rewarding Hunters who commit to a fight rather than dipping in and out

**Design intent:**
The Hunter doesn't fight harder because they're angry. They fight more efficiently because they're not alone even when they look like they are. The passive rewards the Endure fantasy — staying in the fight, absorbing what comes, becoming more capable through presence rather than through any single action.

**Resonance potential:**
Later Resonance tiers may cause the names to manifest more literally — something the Accursed can perceive. The Prior Flame carrying the dead becoming something the Accursed actually respond to.

---

### Not At All

**What it is:**
The Hunter's answer to the question the Accursed implicitly ask. Are you afraid? Not at all. Every elimination announces the Hunter's presence on the battlefield — the Prior Flame radiating outward from a kill, the Accursed in proximity feeling something that stops them momentarily.

**Mechanical expression:**
- Each elimination has a chance to stun nearby enemies
- Larger, more deeply Bleed-corrupted enemies have a higher stun chance — more Bleed means stronger reaction to the Prior Flame's expression
- While The Reckoning is active, eliminating a stunned enemy increases The Reckoning's duration, up to a cap

**Design intent:**
Poetic justice. The Hunter doesn't hide what they are. The stun isn't fear exactly — it's the biological equivalent of a prey animal registering that a predator just made a kill nearby. The passive and the Covenant feed each other: entering The Reckoning with enemies already stunned means the conclusion lands harder and lasts longer. Setup and payoff expressed through the kit's own internal logic.

Larger enemies having a higher stun chance is counterintuitive in the best way — usually larger enemies resist staggers. Here the lore drives the mechanic. More Bleed, stronger reaction to the flame.

**Resonance potential:**
Later Resonance tiers may expand the stun radius, increase stun duration, or add additional effects to stunned eliminations.

---

## Active Abilities

### The Slicer — Weapon Ability

**What it does:**
A ranged thrown blade. Opens engagement from distance. The Hunter does not wait for the enemy to come to them — they open the fight on their terms from a position they chose.

**Mechanical expression:**
- High stagger on hit, low HP damage
- Creates the window, not the kill
- Chains naturally into The Lunge — throw to stagger, close to continue
- Stagger scales by Accursed tier:
  - Thrall — full stagger, long window
  - Retained — partial stagger, shorter window
  - Lucid — brief interrupt rather than full stagger
  - Bestial Lucid — stagger only lands during vital point exposure window

**Design intent:**
The Slicer is the predator opening the engagement. It doesn't kill — it creates the moment the Hunter uses to close distance and begin the real work. The sequence of Slicer into Lunge into follow-up is the lightweight boxer's combination made playable.

**Lore note:**
Limb removal lives in the lore and the narrative descriptor language — not in the gameplay system. Replication cost and loss of meaningful feedback in multiplayer make it non-viable at MMO scale. The stagger represents that impact without requiring a state change system.

---

### The Lunge — Movement Ability

**What it does:**
A directional, fixed-distance movement tool. The Hunter closes distance, creates distance, or repositions — in whatever direction they are currently inputting.

**Mechanical expression:**
- Input-directional — moves in the direction of current player input, not toward a target
- Fixed distance — not momentum based, not target locked
- Functions as both an offensive close and a last-second dodge
- The Hunter stays in the fight by moving through danger rather than away from it

**Design intent:**
The Lunge is neutral — it does not deal damage, it does not lock on. It is the tool that makes everything else possible. The Hunter who Slicers to open then Lunges to close is executing a sequence. The Hunter who Lunges away from a Retained's attack at the last second is staying in the fight on their terms.

**Resonance fork (future):**
- **Gone** — evasive path. The Hunter pulls the flame inward during the Lunge, briefly disappearing from Accursed perception before reappearing at the new position.
- **The Close** — aggressive path. The Lunge covers more distance and deals damage on arrival. The predator closing on prey.

---

### The Read — Tactical Ability

**What it does:**
The Hunter's Flame manifests as prediction. Activates a perception state that reveals where the next vital point location will be before the current vital shifts.

**Mechanical expression:**
- All players can see the current vital point — the subtle shimmer on the enemy
- The Read reveals the *next* location before the shift occurs
- The Hunter is always one step ahead — repositioning for where the vital is going, not where it is
- Maximum uptime on the vital without trivializing the system for other classes

**Design intent:**
Not slowed time. Not precognition as a supernatural burst. The Flame illuminating what is already there for anyone paying close enough attention — the Hunter has just been paying closer attention than anyone else. The Read is who they are expressed as a button. Observant made mechanical.

**Vital point system context:**
- Thrall — vital shifts after significant damage, slow reaction
- Retained — vital shifts faster, lower threshold
- Lucid — aggressive shift, near-zero threshold
- Bestial Lucid — shifts on both damage threshold and independent timer

**Resonance potential (deferred):**
Third Resonance tier — full vital map reveal. All possible vital locations visible simultaneously. The Hunter who has mastered The Read eventually learns to see the whole pattern. Deferred to later development.

---

## Covenant Ability

### The Reckoning

**What it is:**
The Hunter stops filtering. Everything they have been doing — the controlled strikes, the suppressed presence, the careful tool use — drops away. The Flame comes out fully. Not a rage. Not a burst of power. A conclusion. The fight was already over. This is just the part where the enemy finds out.

**Mechanical expression:**
- For a brief window, every strike and shot lands on the exact current vital point
- No misses. No glancing blows. No evasion works.
- The Flame is fully illuminating everything and the Hunter is fully present in a way that is simply not survivable for whatever they are targeting
- Duration extended by Not At All — eliminating stunned enemies during The Reckoning adds time, up to a cap
- When it ends: the Hunter writes it down. Moves on to the next one.

**Design intent:**
The Reckoning is Repay. Everything that came before it — every hit absorbed, every position held, every name on the List — resolves in this moment. Not dramatically. Inevitably. The duration extension from Not At All rewards setup — the Hunter who enters The Reckoning with the battlefield already disrupted gets more out of it. The kit's internal logic feeding itself.

---

## Visual Language

**The List gauge:**
As The Loved and The Lost accumulates — damage dealt, kills registered — a blue vignette builds gradually around the Hunter's weapon. Faint at low charge, intensifying toward threshold. At threshold the vignette is at full intensity and the damage increase activates. The weapon itself is the UI. Peripheral vision catches the blue building. Eyes stay on the fight.

**The Read activation:**
The Hunter's eyes pulse with a subtle blue. Consistent with the blue of The List — same Prior Flame expression, same visual language. The flame doing two different things but speaking the same way.

---

## Kit Summary

| Ability | Type | Core Function | Creed Expression |
|---|---|---|---|
| The Loved and The Lost | Passive | Regen and Steadfast gains through sustained combat | Remember — the names fuel the fight |
| Not At All | Passive | Elimination stunts, Reckoning extension | Endure — presence announced, not hidden |
| The Slicer | Weapon | Ranged stagger, engagement opener | Endure — control the opening |
| The Lunge | Movement | Directional reposition and dodge | Endure — stay in the fight |
| The Read | Tactical | Vital point prediction | Endure — observe before acting |
| The Reckoning | Covenant | Conclusion, every strike finds the mark | Repay — the ledger closes |

---

## Open Questions

- Exact threshold values for The Loved and The Lost gains (requires playtesting)
- Not At All stun duration and radius (requires playtesting)
- The Reckoning base duration and extension cap per stunned elimination
- The List gauge visual — whether it appears on all weapon types or adapts per weapon category
- A/B test: Version A (Read as passive, always-on at base level) vs Version B (Read as active, predicts next vital) — vertical slice will determine which version serves the class identity better

---

## Resonance System Note

Both passives and The Lunge have documented Resonance fork potential. Resonance tiers are not implemented in the vertical slice. All Resonance paths are design notes only at this stage — locked identity, deferred implementation.

---

*Document generated: July 2026*
*Session: Vigil Design — Hunter Class Kit*
