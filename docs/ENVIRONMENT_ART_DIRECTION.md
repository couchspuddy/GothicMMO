# Environment Art Direction — Material & Spatial Doctrine
**Vigil — Art Direction Foundation Document**
*Status: Locked — Layers 1, 2, 3 (material, spatial, surface treatment), and VFX Language. Faction-specific color signatures (Warden, Penitent) remain open pending those classes' design.*

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

## Layer Three: Surface Treatment

*Status: Locked. Verbal/conceptual doctrine only — visual confirmation against real reference imagery completed during design session; further validation should occur as actual assets are produced.*

### Color Temperature — Scarce Warmth Against Pervasive Cold

Vigil's color language is not built on a single dominant hue. It is built on **contrast between warmth and cold**, where warmth is always scarce, always maintained by someone, and never the default state of a space.

**Reference anchor:** The Order 1886's gaslamp-lit environments — warm amber/gold light sources against cold, desaturated stone and steel. The contrast, not the individual colors, is the transferable principle.

**Reference anchor:** Bloodborne's Yharnam — architecture gone cold and grey specifically because the warm light sources (candlelight, hearths, gaslamps) that would normally occupy it have receded. The palette is defined by absence of maintained warmth, not by an inherent "gloomy" hue choice.

**The rule:** any space that is actively inhabited, defended, or safe must have a real, warm, visible light source — genuine amber, gold, firelight. This warmth is the exception, not the baseline, and should read as something actively maintained rather than ambient. Anywhere abandoned or Accursed-held should default to cold, desaturated stone-and-steel tones, specifically because human maintenance of warmth has stopped there. **Practical test for any environment: if a space is meant to feel safe, its warm light source should be identifiable at a glance. If none can be pointed to, the space should not read as safe, regardless of other treatment.**

### Lighting — Dusk as Default Register, Confirmed and Generalized

Eagle's Landing was locked at dusk during encounter design. This doctrine confirms dusk (or its counterpart, an overcast grey that never resolves to storm or clear) as **Vigil's default lighting register for exterior, uncertain, or hostile spaces generally, not only for Eagle's Landing.**

**Reasoning:** full darkness reads as horror-genre aggressive — appropriate for the most severe content (Hollow-adjacent encounters) but wrong as a baseline, since it would contradict the Haunting, Not Frightening pair from the Tone & Sensory Bible. Full daylight reads as safety and resolution — wrong for a world still actively grieving (per the North Star sentence). Dusk is the only register that is *itself* the visual expression of "haunting, not frightening": uncertain, in-between, undecided, rather than definitively resolved toward either safety or danger.

**Rule:** dusk/overcast is default for exteriors and hostile or uncertain spaces. Full night is reserved for the most severe, Hollow-adjacent content only. Full daylight is reserved for genuinely safe interior/hub spaces, and even there should remain filtered and warm rather than flat and bright, consistent with the scarce-warmth rule above.

### Shaped Archetype Color Signatures

A second, closer-range color axis exists specifically for Shaped-held spaces, layered **on top of**, not in place of, the warm/cold macro rule above. This creates a deliberate two-tier information system for the player: **the warm/cold axis reads as safe/unsafe from any distance; the Shaped palette reads as threat-type identification once a player is close enough that the specific danger matters.** A player who has learned to associate a given palette with a given Shaped archetype receives a real, actionable warning before an encounter begins — this is not decorative, it is functional environmental information.

Each palette is derived directly from the specific inversion that defines its archetype, not chosen for generic horror-aesthetic reasons:

**Feral — tans and greens, moss and wood.** Confirmed against reference: overgrown, reclaimed structures, patient organic growth rather than violent damage. This is the correct register because Feral is the loss of *containment* — the Bleed removing what kept a disciplined body restrained, and by extension, what kept a space maintained against nature's slow reclamation. The growth should read as gradual and patient, consistent with Ferals "operating in cycles Hunters have learned to map."

**Predatory — reds and silvers, precious metals and gemstones.** Confirmed against reference: gothic hoarded wealth, opulent rather than violent. This is the correct register because Predatory is the inversion of someone who was *most generous* in life — silver and gemstones are the visual language of accumulation and possession, the precise opposite of generosity. Red paired with silver should read as collected, trophied, possessed — not raw violence for its own sake.

**Assembled — yellows and browns, rotted and decayed.** Confirmed against reference: organic decomposition rather than a clean, intentional color statement. This is the correct register because Assembled is not an inversion of a single virtue the way the other three are — it is an aggregation, multiple lost individual identities collapsed into one mass. A palette of rot rather than a deliberate color fits an archetype defined by loss of individuality itself, not loss of one trait.

**Wraith — greys and whites, ethereal and orderly.** Confirmed against reference: evacuated and clean rather than ruined or corrupted. This is a deliberate departure from horror-genre default (black, sickly green) and is the correct register specifically because Wraith is the inversion of someone *most outwardly focused and community-dependent*, stripped into pure interiority with nothing external remaining. A Wraith-held space should not look damaged — it should look **emptied**, which is a more disturbing register for a type whose defining trait is involuntarily projecting emotion outward because nothing external remains to hold it in.

### Texture & Weathering — Decay Process Tied to Material Tier and Cause

The Tone & Sensory Bible's Specific Decay, Not Generic Grime pair requires that wear always tell a particular story. This section makes that checkable by tying weathering process directly to material tier (Layer 1) and to *cause* — neglect versus inversion versus active maintenance are three different textures, never interchangeable.

**Tier 1 (Pre-Bleed) weathering — patina and oxidation, not damage.** Confirmed against reference: brass and wrought iron left untouched develop slow chemical aging — green-black patina on brass, dark oxidation on iron — while remaining structurally sound. This is the correct register for abandoned pre-Bleed spaces: the material was built to last, and it has lasted, simply unmaintained. **Rule: Tier 1 material should never look structurally failed unless a specific narrative cause is present (e.g., the Bestial Lucid's deliberate destruction). Absent a specific cause, Tier 1 decay is surface-only — patina, not collapse.**

**Tier 2 (Early Post-Bleed) weathering — active corrosion under structural stress.** Confirmed against reference: riveted steel showing genuine rust and stress concentrated at joints and fasteners, the look of something reinforced in haste now failing under the different pressure of time and disuse. This is the correct register for abandoned barricades and defensive structures — Eagle's Landing Encounter 1's abandoned barricades should carry this specific texture, not generic rust, because their story is "hastily built defense, now failed," which is a structural failure story, not a surface aging story.

**Tier 3 (Current-Era) weathering — repair, not decay.** Confirmed against reference: mismatched salvaged materials, visible mending, functional patchwork. This tier should almost never read as "old" or "decayed" in the way Tiers 1 and 2 do — its defining texture story is active, ongoing maintenance by people who are still alive and still repairing it. **Rule: if a Tier 3 space starts to read as decayed rather than repaired, that is a signal something has gone wrong in that location's fiction (abandonment, a failed defense) and should be an intentional narrative choice, not a default texture treatment.**

**Inversion-caused decay (Accursed-held spaces) is a fourth, separate category, already governed by the Corruption Reflects Inversion rule under Layer 2.** It should never be confused with or substituted for ordinary Tier 1/2 neglect-weathering — inversion damage is deliberate and specific to the Accursed holding the space (destruction for the Bestial Lucid, hoarding-wear for a Predatory space, and so on), while neglect-weathering is impersonal, simply the passage of time acting on an empty space.

---

## VFX Language

*Status: Locked as doctrine. Specific per-ability VFX (e.g. Warden and Penitent Prior Flame colors) remain open pending those classes' design.*

VFX is the layer most likely to silently violate every rule established above, because it is often designed by a separate discipline, frequently later, under the assumption that "reads clearly and looks good" is sufficient on its own. It is not — VFX must be checked against the same warm/cold and Shaped-signature doctrine as everything else, or a bright, ungoverned effect color can undercut two layers of otherwise-careful environmental storytelling without anyone deciding that should happen. This section exists to prevent that by establishing which colors each VFX category is permitted to use, and why.

### Three VFX Categories, Three Different Rules

**Prior Flame effects (class abilities, vital point shimmer) — a third register, neither warm-safe nor Shaped-dangerous.** Confirmed against reference: the Hunter's established blue reads correctly as supernatural and *other* — critically, this is a deliberate departure from the warm-equals-safe rule, and that departure is correct rather than a contradiction to resolve. The Prior Flame should never read as environmental safety (it is not a hearth) and should never read as Shaped-archetype danger (it does not belong to the Bleed). It occupies its own third axis: **the player's own agency acting on the world**, visually distinct from both the human-safety palette and the corruption palette. Each class's Prior Flame color, once established, should be checked to ensure it does not overlap with any Shaped signature (Feral's green, Predatory's red/silver, Assembled's yellow/brown, Wraith's grey/white) or read as a conventional warm safety-color. The Hunter's blue satisfies this. Future classes' Prior Flame colors must be chosen with the same exclusion check.

**Selah crystallization — inside the warm register, not outside it.** Confirmed against reference: amber, gold crystallization is the correct treatment specifically because Selah is the direct payoff of the warmth-scarcity rule established earlier in this document — it is rare, earned warmth, generated by interrupting the Bleed's extraction. Selah should always read as belonging to the same visual family as a hearth or a maintained gaslamp: warm, precious, human. This is the one VFX category that should actively reinforce the environmental color doctrine rather than sitting outside it.

**Combat and damage VFX (stagger effects, hit reactions, muzzle flash, impact debris) — functional and largely neutral.** These should prioritize legibility as *information* (did that hit land, is this enemy staggered) over aesthetic color-coding. Default to physically-grounded treatment — sparks, debris, blood, dust — rather than heavy stylized color, so combat VFX does not compete for visual attention with the more meaningful color languages established above (Prior Flame, Selah, Shaped signatures). An exception may be warranted for stagger specifically, since it is a state other systems (AI, other abilities) need to read — if stagger requires a visual tell beyond the enemy's own animation response, it should use a neutral, non-hue-coded treatment (e.g., white/clear flash) rather than borrowing color already assigned elsewhere.

### The Governing Test

Before finalizing any new VFX asset: **does this effect's color already belong to something else in this document?** If it matches a Shaped signature, a warmth-register color, or an existing Prior Flame color without being that thing, it will create false information for the player — teaching them to associate a color with the wrong system. This is not a matter of taste; it is a functional legibility requirement, given how much of Vigil's design already depends on color carrying real, actionable meaning at a glance.

---

- **The Accursed are never monsters** — the corruption-reflects-inversion rule ensures environment design tells the same "this was a person, and their specific tragedy is legible" story that enemy design already commits to. A held space should never just look "evil" — it should look like a specific inversion of a specific person's nature.
- **Weight before spectacle** — material logic and verticality rules both reward a player's ability to read a space correctly through structure and composition alone, rather than relying on VFX, UI markers, or spectacle to communicate danger and safety.
- **The world existed before you arrived** — the three-tier material system means every space implicitly carries a construction history (built when, by whom, defended how) whether or not that history is ever explicitly narrated.

---

## Open Items — Remaining

- Warden and Penitent Prior Flame colors, once those classes are designed — must be checked against the exclusion rule in VFX Language above (no overlap with Shaped signatures or warm-register colors)
- Whether faction-held or Warden/Penitent-associated spaces need their own color signature, parallel to the Shaped system above

---

*Document generated: July 2026*
*Session: Vigil Design — Environment Art Direction, Layers 1 & 2*
