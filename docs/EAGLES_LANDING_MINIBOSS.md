# Eagle's Landing Mini-Boss — The Feral Retained
**Vigil — Enemy Design Document**
*Status: Locked — Design Complete, Implementation Pending*

---

## Overview

Appears in **Eagle's Landing, Encounter 2**, on the second floor of the collapsed building (see Eagle's Landing Encounters doc). Alone, at the window, having watched the player clear the ground floor and navigate the collapse before the fight begins — roughly sixty seconds of observation per the original encounter design.

This enemy does not carry the narrative weight the Bestial Lucid boss does. Classification and tier are sufficient identity for this encounter — no backstory, no name, no life-before-the-Bleed narrative layer. Scope intentionally kept lean.

---

## Classification

**Shaped Archetype:** Feral
**Tier:** Retained

This is a deliberate combination, not a simplification of either category. A Thrall-tier Feral would be pure animal instinct with no complexity. This enemy is Retained-tier, meaning enough awareness survived the Bleed's extraction that she is not just reacting on impulse — she has the cognitive depth to wait, track, and choose her moment with intent, even though her dominant expression is primal and instinctual rather than calculating.

**What this means in practice:** the window-watching beat is not cold assessment (that would be pure Retained behavior, more collector-like). It is a predator with just enough awareness to know that patience produces a better opening than an early pounce. She is hunting, not studying — but hunting well requires exactly the kind of restraint pure Feral instinct alone wouldn't provide.

---

## Design Principle: Scripted Intent, Not Adaptive AI

**The problem this section solves:** the encounter design implies she "understands" the player — specifically, that she recognizes the player's ranged weapon as a threat and positions to neutralize it. This does not require actual behavioral learning or adaptive AI. It requires **scripted behavior and room geometry that produce that outcome mechanically**, while the fiction (sixty seconds of observation) gives the player permission to read intent into it.

This is the same design principle used for the Bestial Lucid's environmental destruction — script behavior that *reads* as psychology, rather than simulating the psychology directly. No AI cost, same narrative payoff.

---

## Fight Structure

### Isolation as the Encounter Design

No adds. Pure 1v1. The room's emptiness is not incidental — it is the point. Nothing to hide behind that isn't already part of the space, nothing diluting the engagement. This is a clean skill check, not an attrition fight.

### Opening — Immediate Closure, No Ranged Phase

She closes distance the moment the player enters the space, using the tight interior geometry already established for this room in the encounter doc. This is what makes "she neutralized your ranged advantage" true mechanically without requiring any actual target-tracking or player-behavior logic — the room is short-sightlined and close-walled by design, and her immediate aggression means range is never actually available to the player at any point in the fight. The fiction says she understood the threat and closed the gap. The mechanics simply never open a window where ranged play was viable in the first place.

### Vital Point Behavior

**Standard Retained-tier vital shifting** — faster shift rate, lower damage threshold than Thrall-tier, per the locked Vital Point System (Hunter Class Kit doc).

**No simplification for this fight.** Initial instinct was to consider reducing vital point complexity given the fight's speed, but the isolation (no adds, no competing attention demands) makes this exactly the right context for a moving vital point to be legible rather than overwhelming — unlike Encounter 3's plaza, where tracking a shifting vital against 8-10 Thralls would add noise rather than challenge. Here, with only her to track, the vital point is the whole puzzle, not one demand among many.

**Exact shift threshold and movement feel are explicitly deferred to hands-on tuning during implementation** — this is expected to require iteration once the fight is playable, not something to lock numerically in advance.

### Win Condition — Decisive, Not Attritional

This fight is a reflex-and-execution check, not a resource-management check. Whoever reads the exchange correctly ends it quickly — Slicer stagger to interrupt her closing charge, Lunge to reposition rather than attempting to create range that the room doesn't allow, vital point pressure landing cleanly. Whoever doesn't read it correctly loses quickly. No extended middle phase, no Steadfast conservation puzzle. This deliberately contrasts with the Bestial Lucid boss fight, which *is* about resource management and sustained phases — the mini-boss tests something different: immediate, correct response under pressure, with no room to hesitate or fall back on a safer range-based playstyle.

---

## What This Fight Is Testing, Design Intent

This is the encounter that specifically checks whether the player has internalized the Hunter's core identity: **the Hunter creates the opening, they do not wait for one.** Waiting is not a viable strategy in this fight — there is no room to retreat into and no time to stall for. A player who tries to backpedal and re-establish ranged engagement will find the geometry doesn't allow it. The only way through is forward, using the kit's tools to interrupt and reposition rather than disengage.

---

## Open Items for Implementation

- Exact vital point shift threshold and movement feel — deferred to hands-on tuning in engine
- Her specific melee attack pattern/moveset — not yet designed, needs a pass once base AI behavior (immediate closure, no ranged phase) is functional
- Whether her closure-on-sight behavior needs a brief telegraph/wind-up for fairness, or whether the sixty-second observation beat is sufficient warning on its own
- Health pool and damage values — not yet set, will depend on Hunter kit damage output once fully tuned

---

*Document generated: July 2026*
*Session: Vigil Design — Eagle's Landing Mini-Boss*
