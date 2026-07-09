# UI/UX Design Doctrine
**Vigil — Interface Design Document**
*Status: Living document — governing principles and combat-critical surfaces locked (Health, Steadfast, Ability States, Selah Collection); remaining surfaces in progress*

---

## Purpose and Origin

This document exists because of a specific, deliberately-recorded design correction. Mid-project, while beginning UI/UX design, it became clear that resource display (health bars, ability cooldowns) had been unconsciously inherited from genre convention (Destiny-style FPS MMO) without ever being deliberately evaluated against Vigil's own doctrine — the same scrutiny already applied to progression systems, encounter design, and environment art had never been applied to UI. This document exists to correct that and prevent it from recurring on remaining unaddressed surfaces.

**The governing test for every UI decision in this document:** does this element's shape, placement, and behavior actually express something about Vigil specifically, or is it the industry-standard container that any game's information could be poured into? Every surface below was evaluated against this test before being locked.

---

## Foundational Principle: Legibility Scope Determines Design Freedom

**Anything a party member needs to read on another player must be a legible, conventional HUD element. Anything purely personal and non-safety-critical has real room to depart from genre convention.**

This single rule, surfaced while designing Health and Steadfast side by side, governs every subsequent UI decision in this document:

- **Party-legible information** (health) fails the "go diegetic" test immediately — a diegetic effect on one player's screen (e.g., screen-edge color shift) communicates nothing to teammates who need to read that same information from across the room. This is not a matter of taste; it's a hard functional requirement.
- **Purely personal information** (Steadfast, ability state) has genuine freedom to break from convention, precisely because no one else's decisions depend on reading it correctly.

Apply this test first, before any other consideration, when designing any UI surface not yet covered in this document.

---

## Foundational Principle: Binary States Require Binary Signals

**A state that is functionally binary (can I use this or not) must never be represented with a purely continuous visual signal (a gradual fill, swipe, or gradient) as its primary readability channel.**

This principle was locked after identifying a specific, common genre frustration: gradual radial cooldown fills make "99% ready" and "100% ready" nearly indistinguishable at a glance, even though the actual game state is a hard binary. Continuous fill is acceptable as *secondary* planning information (how much longer until ready), but the primary answer to "can I press this button right now" must be conveyed through a discrete, unambiguous state change — not an approach toward one.

**Practical application:** ability icons should snap to a fully distinct "ready" visual state (brightness, saturation, and/or a border/outline present only in that state) rather than gradually brightening as cooldown completes. This principle applies with the highest stakes to Covenant abilities specifically — every class's Covenant is deliberately designed around "save it for the moment that matters" tension (The Reckoning, The Wall Remains, Grief Given Voice), and that entire design goal is undermined if a player cannot tell with total certainty, at a glance, mid-fight, whether it is currently available.

**Compounding states must remain visually distinct from each other.** Where an ability has two separate binary-ish states (e.g., off cooldown, versus currently synergy-primed by a passive condition — Last Rites being both castable and, separately, currently amplified by Be At Peace), each state needs its own distinct visual treatment. Conflating "ready" and "ready and enhanced" into a single ambiguous glow recreates the same legibility failure this principle exists to prevent, just with two states instead of one.

---

## Foundational Principle: Reserve Quiet for What Has Earned It

Derived directly from the Tone & Sensory Bible's **Earned Quiet, Not Ambient Calm** pair (originally an audio principle): if the Selah moment's silence is meant to be the one true silence in the game, UI behavior during that moment should reinforce it rather than compete with it. HUD elements should simplify or recede during Selah collection, the visual equivalent of the sound design's rule that ambient sound never fully rests except in that one moment. Exact implementation (full fade vs. partial simplification) is not yet locked — see Open Items.

---

## Locked Surfaces

### Health

**Legibility class:** Party-critical. Must be a conventional, precise, always-visible HUD element — not diegetic, not ambiguous. This was tested directly against a proposed diegetic alternative (screen-edge color/vignette shift reflecting damage) and rejected specifically because it fails the party-legibility test: a personal screen effect communicates nothing to teammates who need to read a player's health to make their own decisions (e.g., a Warden deciding whether to use Draw to protect a low-health ally).

**Locked specification:**
- Traditional bar/numeric display, always visible, precise
- Party-frame visible (teammates can read each other's current health)
- Color treatment tied to the Environment Art Direction warm/cold doctrine rather than a generic red-yellow-green gradient: **full health reads as bright red (warm, vital), shifting toward blue/grey (cold) as health depletes**

**Why this is correct, not a concession:** this is one of the rare cases where genre convention and functional necessity align — the shape and legibility requirements are kept fully conventional, while the *color language* is made specifically Vigil's own, tying player survival directly into the same warmth-as-precious doctrine governing the rest of the world's visual identity.

---

### Steadfast

**Legibility class:** Purely personal. No party-legibility requirement — Steadfast management is an individually-earned skill-expression resource by design (see Steadfast System doc), which gives this surface real freedom to depart from convention.

**Locked specification:**
- Integrated directly into or immediately below the reticle — not a separate corner-of-screen meter
- Displayed as **chunked segments**, corresponding directly to the low/mid/high ammo output tiers already locked in the Steadfast System doc, rather than a smooth undifferentiated fill
- **On weapon swap:** automatically previews how much of current Steadfast would be consumed if converted for that weapon — shown to all players by default, not gated behind actually holding the reload input

**Design rationale — placement:** Steadfast must be checkable constantly, mid-combat, without breaking aim. A traditional corner HUD meter would force an attention-split a fast-paced FPS shouldn't demand for a resource meant to be managed fluidly. Reticle integration means the information arrives in the same eye-movement as aiming itself.

**Design rationale — chunked segments:** visually teaches the existing tier system passively. A player never needs to memorize what Steadfast quantity equals which ammo tier — the chunk count shows it directly, every time they glance at their own reticle.

**Design rationale — automatic swap preview:** this was deliberately decided to be shown to all players regardless of skill level, rather than gated behind the deliberate hold-to-reload action, per the **Respect the Player's Intelligence** design tenet. That tenet is interpreted here specifically as giving every player the same clear, honest information rather than gating useful mechanics behind discovery — consistent with how the vital point system teaches itself through play rather than a tutorial popup.

**Establishes a reusable pattern:** this is the first concrete implementation of "preview a hypothetical future state before the player commits to an action," a pattern also recommended (but not yet built) for the Resonance/encumbrance bar in the Resonance Strain & Gear Lifecycle doc (hovering a gear item to preview its effect on the equipped Resonance total before committing). Future UI work involving any preview-before-commit interaction should reference this pattern rather than reinventing it.

---

### Ability State Display (Cooldowns and Synergy States)

**Legibility class:** Purely personal — same freedom as Steadfast.

**Locked specification:**
- Not a generic row of icons with radial cooldown swipes as the primary readability signal (see Binary States principle above)
- Each ability icon requires a distinct, discrete "ready" visual state, separate from any continuous fill used only for secondary planning information
- Each ability icon requires a **second, separate visual layer** representing synergy-primed states — abilities whose effect is currently amplified by a passive condition (e.g., Last Rites when Be At Peace is active; potentially Draw relative to I Am the Wall's charge level, if that interaction is deemed meaningful enough to surface)
- The two states (cooldown-ready, synergy-primed) must remain visually distinguishable from each other, including when both are simultaneously true

**Design rationale:** Vigil's kits are not mechanically uniform in how their actives gate — some are pure cooldown, some key off passive states with no cooldown-timer identity of their own in their amplified mode. A flat, identical cooldown-swipe treatment across all ability icons would visually flatten genuinely different underlying systems into looking the same, hiding real, actionable combat information (whether an ability is *currently in its powered-up state*) that is often more important moment-to-moment than raw cooldown remaining.

**Highest-stakes application:** Covenant abilities specifically. The entire "save it for the moment that matters" design intent behind The Reckoning, The Wall Remains, and Grief Given Voice depends on total, unambiguous certainty of availability at a glance — this is the single most costly surface for a legibility failure to occur on.

---

### Selah Collection

**Legibility class:** Party-relevant in staged/raid content (shared moment, all players affected simultaneously); party-wide reward but individually-triggered in open world.

**Status note:** this surface required a full redesign mid-project after Selah's underlying function changed significantly from its original single-context design (see Steadfast & Selah Loop doc for the original version). The mechanic now operates across three genuinely distinct trigger contexts. This section reflects the current, final design.

**The three trigger contexts:**

- **Open World** — chance-based drop from defeated enemies. Triggered individually (whoever lands the kill), but rewards the entire Kindle party-wide regardless of location. Does not affect Steadfast.
- **Staged Content (Contracts)** — guaranteed, single shared collection moment for all players in the activity after every encounter clear. Rewards a variable amount of Selah based on encounter/activity difficulty. Affects Steadfast (full restock, per the original Steadfast & Selah Loop design). Functions as the checkpoint system for the activity (see Death & Failure States doc).
- **Raid Activity** — same shared-moment structure as staged content, generally longer duration. Affects Steadfast; may restock revive kits/tokens depending on final raid failure-state design (see Death & Failure States doc, raid-scale section — still deferred pending raid design).

**The unifying UI principle — one system, one intensity scale, not three separate treatments.** Rather than designing distinct visual languages per trigger context, Selah collection uses a **single prompt and interaction across all contexts**, with one continuous variable governing its presentation: **intensity**, driven by how significant the triggering event was — enemy tier in open world, encounter/activity difficulty in staged and raid content.

**What intensity scaling affects, uniformly across all three contexts:**
- Vignette darkness (deeper for higher-significance events)
- Depth of silence/audio ducking (quieter for higher-significance events, reinforcing the Earned Quiet, Not Ambient Calm principle above)
- HUD transparency (more transparent/receded for higher-significance events)
- Duration of the pause itself

**Why open world does not get a separate, permanently "lighter" treatment.** An earlier draft of this system treated open world as a fixed, always-minor tier distinct from staged/raid's fixed, always-major tier. This was corrected: intensity is governed by **what was defeated, not what kind of content the player is standing in**. A Thrall killed in the open world produces a minor moment. A world boss killed in the open world can produce a moment of equal or greater intensity than a staged Contract's encounter-clear Selah, because the significance of the kill — not the content category — is what the system is actually measuring. This keeps Selah governed by one consistent rule (significance of the death) applied identically everywhere, rather than requiring players to learn different visual languages for functionally the same moment occurring in different places.

**Design rationale — this is Significant, Not Celebrated applied to UI intensity, not narrative beats.** A world boss's death does not get a fanfare or unique spectacle treatment — it gets a deeper, quieter, longer version of the exact same pause every other death produces. Restraint scaling with weight, rather than escalating into spectacle, is the correct expression of that Tone & Sensory pair specifically at the UI layer.

**Checkpoint significance is communicated explicitly, not through visual weight.** Rather than attempting to make the intensity scale also silently convey "this collection point is now your checkpoint" (which would overload one system with two different jobs), this functional fact is stated directly via tooltip or equivalent explicit UI text, once, in staged and raid content specifically. This keeps the intensity scale doing exactly one job (communicating how significant this moment was) rather than trying to simultaneously encode a separate, purely functional fact players need to *know* rather than *feel*.

---

- Exact visual treatment for the ability state-layer system (colors, border/glow specifics, how simultaneous states combine visually) — principle locked, specific execution not yet designed
- Selah collection UI behavior — fully resolved above; exact intensity-scale calibration values (what specific enemy tiers/activity difficulties map to what vignette/silence/duration numbers) deferred to playtesting
- Resonance/encumbrance bar — conceptually described in the Resonance Strain doc, not yet run through the same UI doctrine rigor as Health/Steadfast/Abilities
- Passive charge state displays not yet covered by the ability state-layer system (e.g., how I Am the Wall's own accumulation is shown to its owner, separate from how it affects Draw's icon state)
- Interaction prompts (previously flagged in the Eagle's Landing implementation notes as functionally built but visually absent)
- Downed/revival state UI
- Party frame design beyond the health-legibility requirement already locked
- All out-of-combat/menu-level UI: character select, inventory, gear management, party/Kindle formation tools, map (if any)
- Whether any UI surfaces beyond the ones covered here have inherited unexamined genre-default assumptions — this document's own origin is a reminder to keep checking

---

## Design Tenet Alignment

- **Respect the Player's Intelligence** — directly cited as the deciding rationale for Steadfast's automatic swap preview; the same standard should be applied to any future gating decision between "always shown" and "discovered through deliberate action."
- **Weight Before Spectacle** — the Binary States principle indirectly serves this tenet by prioritizing clear function over the more visually satisfying (but less honest) continuous-fill convention most action games default to.
- **Every system serves the feeling** — Health's warm-to-cold color treatment is the clearest example in this document of a purely functional element still being made to carry Vigil's specific emotional and visual doctrine rather than defaulting to generic genre color language.

---

*Document generated: July 2026*
*Session: Vigil Design — UI/UX Doctrine*
