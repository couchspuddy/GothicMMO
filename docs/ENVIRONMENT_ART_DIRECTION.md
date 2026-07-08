# Environment Art Direction — Material & Spatial Doctrine
**Vigil — Art Direction Foundation Document**
*Status: Locked — Layers 1 & 2 (Material Logic, Spatial Logic). Layer 3 (surface treatment: color, lighting, texture reference) deferred to a follow-up session.*

---

## Purpose of This Document

This document establishes the structural rules governing how Vigil's environments are built, before any discussion of color palette, lighting mood, or specific texture reference. The premise: environment art direction operates on three layers — **material logic** (what the world is made of, and why), **spatial logic** (how spaces are composed and what that composition communicates), and **surface treatment** (color, lighting, texture — the layer most references address, and the layer that is meaningless without the first two being solid). This document locks the first two. Surface treatment is intentionally deferred, since it benefits from a working session built around actual reference imagery rather than written doctrine alone.

**The governing principle behind everything in this document:** environments should tell the same inversion story enemy design already tells — beautiful, deliberately-built things, now failing or corrupted, in a way that reflects the specific nature of what's wrong with them. Nothing in Vigil's world should look damaged or dangerous *generically*. Damage and danger should always be legible as *specific*.

---

## Layer One: Material Logic

### The Three Material Tiers

Vigil's material palette is not a free aesthetic choice — it is constrained by the setting's actual technological starting point (Gilded Age-adjacent, 1900–1905 divergence) plus two centuries of survival-driven adaptation since. Three tiers exist, and **which tier dominates a space should communicate when it was built and by whom, without requiring any text.**

**Tier 1 — Pre-Bleed Materials ("The Before").** Dressed stone, wrought iron, brass, glass. The genuine Gilded Age palette — ornate, because craftsmanship still had the leisure to exist. This is what remains in places built before the Bleed and never significantly altered since: grand civic architecture, old wealth, abandoned zones frozen at the moment of the Bleed's arrival. Eagle's Landing's City Hall is built from this tier.

**Tier 2 — Early Post-Bleed Materials ("The Scramble").** Riveted steel plate, salvaged iron, anything reinforced in haste rather than crafted with care. This is wall-building and barricade material — the visual language of active or recent defense. Eagle's Landing Encounter 1's abandoned barricades are this tier, and their presence already implies a defense that failed or was abandoned, without needing to state it.

**Tier 3 — Current-Era Materials ("Two Hundred Years of Scarcity").** Patchwork, reused, repurposed. Nothing is manufactured new at Gilded Age quality anymore — everything current is built from salvage of the first two tiers. This is the dominant material logic anywhere people actually live now: the Hearth, walled settlements, any space defined by ongoing human habitation rather than history or abandonment.

### The Rule This Creates

**Material tier composition should let a player read a space's age, safety, and history at a glance, before any narrative context is given.**

- A space dominated by intact Tier 1 material reads as either a protected, cared-for settlement, or an abandoned place of old wealth and status — context (enemy presence, decay level) disambiguates which.
- A space dominated by hastily-applied Tier 2 material reads as a contested frontier — actively being defended, or a place where defense recently failed.
- A space dominated by Tier 3 salvage reads as lived-in, current, human-scale, safe by virtue of active habitation.

This is a structural rule, not a stylistic suggestion — it should govern material selection in every environment built for Vigil going forward, including areas not yet designed.

---

## Layer Two: Spatial Logic

### Corruption Reflects Inversion, Not Generic Damage

Already implicitly established through the Bestial Lucid's design (a builder whose inversion turned her into something that compulsively destroys rather than constructs, leaving City Hall's rotunda dismantled rather than merely decayed) — this document generalizes that into a standing rule:

**Accursed-held spaces should show physical evidence of the specific inversion of whatever holds them, not generic damage or decay.** A space held by a Predatory-inverted Accursed might show signs of hoarding or draining rather than destruction. A Wraith-held space might show almost no physical damage at all — stillness and wrongness rather than visible harm, which can read as more disturbing than damage. This rule means environment design and enemy design are always reinforcing the same specific story through different means, without requiring new unique assets — only intentional, consistent composition of what already exists.

### Verticality as Safety-Signaling

Given Vigil's world is built around walled, defended settlements, and the Warden's entire identity is built around holding a line — verticality should carry real, consistent meaning across the whole game: **height signals safety, ground level signals exposure.**

- Watchtowers, parapets, upper floors, elevated and enclosed spaces — where the living build, defend, and gather.
- Ground level, particularly in contested or abandoned zones — where the Bleed operates and where danger concentrates.

**Practical application for encounter and level design:** encounters intended to feel exposed and dangerous should pin the player to ground level with limited verticality (as Eagle's Landing's street-level Thrall encounters already do). Safe hub spaces should read as elevated, enclosed, or defensible from above by their basic geometry, independent of any lighting or color treatment layered on top later.

### Silhouette Legibility Over Detail Density

Every major structure in Vigil should be identifiable — what kind of place it is, and that something is wrong with it — from its silhouette alone, at long sightline distance, before any surface detail is visible. This is the actual mechanism behind Dark Souls-style environmental storytelling, and it is frequently the piece imitators miss in favor of texture density.

**This is a direct, load-bearing requirement for content already designed:** Eagle's Landing's approach to City Hall depends on the building reading as "grand civic architecture, now wrong" from a distance, specifically because the encounter design's escalating dread (claw marks, darkened windows, structural warping, all described as tells noticed *before* the player enters) only works if the base silhouette is legible enough to carry that escalating wrongness. If the silhouette doesn't read correctly at range, the entire approach sequence's design intent is undermined regardless of what detail work happens up close.

### Scale Discontinuity as an Escalating Horror Signal

The Hollow is already established in lore as spatially impossible — "bigger inside than outside," architecture that stops being honest the closer content gets to it. This document generalizes that into a **graduated visual ladder across the full Accursed hierarchy**, giving environment design a consistent way to escalate wrongness as tier increases:

- **Thrall-held spaces** — architecturally honest, simply decayed. What you see is what's there.
- **Retained-held spaces** — architecturally honest, but showing the specific inversion (per the corruption rule above).
- **Lucid-held spaces** — architecturally mostly honest, but with subtle scale or geometry inconsistencies beginning to appear — nothing overt, but something a careful player might notice feels slightly wrong.
- **Hollow-held spaces** — architectural logic breaks down fully. Interior scale does not have to obey exterior scale. This should be reserved exclusively for Hollow-adjacent content (the Cathedral of Chains raid) and never used for lower-tier content, so that when it does appear, its wrongness is maximally distinct rather than a device the player has already grown used to.

---

## Design Tenet Alignment

- **The Accursed are never monsters** — the corruption-reflects-inversion rule ensures environment design tells the same "this was a person, and their specific tragedy is legible" story that enemy design already commits to. A held space should never just look "evil" — it should look like a specific inversion of a specific person's nature.
- **Weight before spectacle** — material logic and verticality rules both reward a player's ability to read a space correctly through structure and composition alone, rather than relying on VFX, UI markers, or spectacle to communicate danger and safety.
- **The world existed before you arrived** — the three-tier material system means every space implicitly carries a construction history (built when, by whom, defended how) whether or not that history is ever explicitly narrated.

---

## Open Items — Deferred to Layer 3 (Surface Treatment) Session

- Color palette doctrine (per material tier, per zone type, per Accursed-hierarchy tier)
- Lighting mood and time-of-day doctrine (dusk was locked for Eagle's Landing specifically — does this generalize to all zones, or is it zone-specific?)
- Specific texture and weathering reference imagery
- VFX language for the vital point shimmer, Selah crystallization, and Prior Flame visual effects, and how these interact with the environmental palette without breaking silhouette legibility

---

*Document generated: July 2026*
*Session: Vigil Design — Environment Art Direction, Layers 1 & 2*
