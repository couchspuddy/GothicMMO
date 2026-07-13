# Sound Design Doctrine
**Vigil — Audio Design Document**
*Status: Governing structure locked. Complements Music Direction — sound and music are related but distinct disciplines, this document does not replace or duplicate the Music Direction doc.*

---

## Purpose and Relationship to Music Direction

Music Direction established the score's structural relationship to place and narrative. This document covers everything else audio-related: ambient texture, footsteps, combat feedback, and enemy audio identity. Music is mood and structure. Sound design is primarily **information** — the layer players use, consciously or not, to understand what's happening around them, especially where vision can't cover it (behind the player, obscured by geometry, happening in a moment their eyes are elsewhere).

This distinction is worth stating precisely: sound design should be treated as a real information channel with the same seriousness as the HUD, not as secondary atmosphere. This follows directly from the HUD Doctrine's core reframe — information enables player agency rather than replacing it. It would be inconsistent for every other system in the project to treat fast, accurate information as the foundation of skillful play while sound design remained purely decorative.

---

## The Two-Layer Structure

### Layer One — Ambient/Diegetic

**Governs:** wind, distant structural creaks, low ambient hum, footsteps on different surfaces, general world presence.

**Locked rule (pre-existing, restated here as this document's foundation):** ambient sound should never fully rest, anywhere in the game, except during the Selah moment — the one true silence in the game. This layer's job is constant, low-grade world presence.

**New extension — footsteps should reflect Material Logic tier.** Per the Environment Art Direction doctrine's three material tiers, footstep audio should differ by what a player is walking on: Tier 1 (dressed stone, pre-Bleed) should sound solid and resonant; Tier 2 (riveted steel, hastily reinforced) should sound harder and more metallic; Tier 3 (salvaged patchwork, current-era) should sound looser, more varied, less uniform. This gives players an additional, unconscious layer of "where am I, and what era of this world am I standing in" information, reinforcing the visual material doctrine through a second sense.

### Layer Two — Combat Information

**Governs:** vital point hits, stagger confirmation, off-screen enemy proximity, Steadfast tier changes, any sound that conveys actionable mechanical state.

**Locked rule — binary states require binary signals, extended from the HUD Doctrine.** A vital point hit must sound unmistakably different from a normal hit — not subtly different, not "a little more impactful." The same failure mode identified in the HUD Doctrine's ability-readiness problem (a gradual visual fill making "almost ready" indistinguishable from "ready") applies identically to audio: a vital hit sound that's only slightly distinct from a normal hit risks players misjudging whether they're actually landing vitals. This needs a hard, discrete audio cue — a distinct pitch, a distinct layered element — not a volume or intensity gradient.

**Off-screen/behind-player threat audio** is this layer's clearest unique value over vision-based systems — sound can convey "something is approaching from outside your field of view" in a way no HUD element can replicate without breaking immersion (a radar or off-screen indicator would be a genre-default addition this project has deliberately avoided). This should be a deliberate design target, not an incidental byproduct of enemy audio existing.

---

## Enemy Audio: "Haunting, Not Frightening" Applied to Sound

**Locked rule:** enemy audio cues should be readable and trackable, not designed to startle. A player paying attention should be able to use sound to anticipate a threat, the same way silhouette reading rewards attention in the Character Art Direction doctrine. Sound design that exists primarily to make a player jump — sudden volume spikes, unmotivated stingers — contradicts Haunting, Not Frightening as directly as a cheap jump-scare visual would, and is explicitly rejected as a design tool here, extending the same rule already locked for music.

---

## Shaped Archetype Audio Signatures

Extends the same two-tier information system already established for color (Environment Art Direction) and silhouette (Character Art Direction): a macro read at distance, a specific confirmation up close. Each signature is derived the same way the color and silhouette doctrines were — from the specific virtue each archetype's inversion took away, not from generic horror-genre monster-sound convention.

**Feral.** Low, physical, breath and movement sounds dominant over vocalization. The loss of containment reads sonically as barely-restrained physical presence — heavy breathing, joint and movement sounds through debris — rather than any clear vocal "monster" sound. Consistent with the hunched, weight-forward silhouette and the tan/green color signature: the sound of something losing its held-together shape.

**Predatory.** Deliberately **quieter** than a Thrall, not louder — a direct extension of the elegant, composed silhouette decision. Minimal incidental noise, controlled movement sound. This is a deliberate departure from horror-genre convention, where a more dangerous enemy is typically louder; a Predatory being quieter is more unsettling precisely because it inverts that expectation, consistent with the silhouette doctrine's explicit acceptance that a Predatory reading as non-threatening at a distance is correct, not a risk to be designed away.

**Wraith.** Near-silence, with occasional involuntary emotional leakage breaking through — a fragment of a whisper, a brief cry — rather than any sustained vocalization. Matches "emptied, not damaged" directly. This ties to the silhouette doctrine's "easy to overlook" instruction: audio should be equally easy to miss until proximity or a leaked moment gives it away.

**Assembled.** The one archetype where **layering multiple distinct sound sources simultaneously** is correct — implying multiple people within one entity, mirroring the silhouette's "mass that doesn't resolve correctly." Where the other three archetypes should sound like one coherent (if wrong) source, Assembled should sound like several overlapping ones that don't quite align.

---

## Design Tenet Alignment

- **Respect the Player's Intelligence** — treating sound as a genuine information channel, on par with the HUD, extends this tenet into a discipline that's frequently treated as secondary or purely decorative in other games.
- **The Accursed are never monsters** — every Shaped audio signature is derived from what was taken from that person, not from generic creature-sound libraries, extending the same discipline already applied to color and silhouette.
- **Weight Before Spectacle** — the rejection of jump-scare stingers and volume-spike tactics keeps sound design in service of sustained atmosphere and legibility rather than momentary shock value.

---

## Open Items

- Exact audio treatment for tier-based footsteps (specific sound sources per material tier) — principle locked, asset-level execution not yet specified
- Exact pitch/layering specifics for the vital-hit binary audio cue — principle locked (must be unmistakably distinct), specific sound design not yet built
- Off-screen threat audio — confirmed as a design target, specific implementation (directional audio cues, proximity thresholds) not yet detailed
- Whether Warden/Penitent player-character audio (footsteps, ability sounds) needs its own doctrine pass, distinct from enemy audio — not yet addressed, this document's Shaped section covers enemies only
- Interaction between Selah's silence rule and combat information layer — does the combat information layer (vital hits, stagger) also go silent during Selah, or does only the ambient layer rest? Not yet decided.

---

*Document generated: July 2026*
*Session: Vigil Design — Sound Design Doctrine*
