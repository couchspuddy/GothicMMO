# The Warden — Class Kit Design Document
**Vigil — Class Design Document**
*Status: Locked — Full Kit Designed, Zero Implementation*

---

## Identity Statement

The wall made flesh. Not a person who stands in front of danger — a person who becomes the thing danger breaks against. Where the Hunter creates conditions and moves through them, the Warden creates conditions by refusing to move.

The Warden's moment-to-moment feeling is immovable presence: the world comes apart around the Warden and the Warden remains. Not aggression, not precision — presence. Event to event, the Warden absorbs what the encounter gives and returns it as force, not rage — directed, controlled, the wall doesn't rage at the water, it holds and the water breaks. At mission scale, the oath is the mission and the mission is the oath — the Warden doesn't complete missions, they maintain them.

---

## The Creed

**Endure** governs nearly the entire kit — presence, stillness, absorption, holding position under pressure.

**Repay** lives in the Covenant specifically, the same structural pattern established for the Hunter: everything endured across the fight resolves in one moment, not as fury, but as refusal. The wall does not break.

---

## Kit Overview

| Ability | Type | Core Function |
|---|---|---|
| Held Ground | Passive | Reduced damage and increased threat while stationary or moving slowly |
| I Am the Wall | Passive | Damage taken builds charge; at full charge, Warden and nearby allies gain Steadfast generation and flinch resistance while damage continues |
| Shrapnel | Tactical | Thrown fragmentation explosive; stuns enemies in radius, amplifies Held Ground if Warden is caught in the blast |
| Draw | Weapon | Taunt, self-mitigation, and increased I Am the Wall fill rate — the kit's one deliberately selfish tool |
| Hold the Line | Movement | Burst movement speed while Held Ground's effect remains active despite motion |
| The Wall Remains | Covenant | Death-defiance floor at 1 HP, self-buff to aggro/mitigation/damage for the duration |

Six slots, matching the Hunter's structure exactly: two passives, two actives, one movement, one Covenant. Uniform button count preserved across classes per the locked design principle.

---

## Passive Abilities

### Held Ground

**What it is:** The Warden's stillness is not passivity — it is the wall doing its job. While stationary or moving slowly, incoming damage is reduced and threat generation increases.

**Design intent:** This is the Warden's baseline identity expressed as a passive with no activation required — the class simply *is* more dangerous to attack and more capable of absorbing attacks the less it moves. This inverts the assumption most action combat design makes (mobility equals survivability); for the Warden, stillness is the survivability tool.

---

### I Am the Wall

**What it is:** As the Warden takes damage, they accumulate charge. At full charge, the Warden and allies within a radius gain increased Steadfast generation and flinch resistance — and the effect persists for as long as the Warden continues taking damage, rather than firing once and ending.

**Design intent:** The Warden becomes, briefly, the thing a settlement's wall actually is — not a one-time reward for tanking, but an ongoing state that holds only as long as the holding continues. This deliberately inverts most tank-passive design, which typically rewards mitigating damage as much as possible; here, once the threshold is crossed, continued damage becomes productive rather than something to avoid.

**Solo viability:** the Warden is explicitly included in "allies within radius" — the ability does not go inert in solo or open-world content. A wall protecting an empty courtyard is still a wall.

**Cessation behavior:** the buff lingers briefly after the Warden stops taking damage rather than cutting off immediately, avoiding flicker during natural combat lulls. Exact grace-period duration deferred to playtesting rather than locked in advance.

---

## Active Abilities

### Shrapnel — Tactical Ability

**What it does:** A cobbled-together throwable explosive. Shatters into fragments on impact. Enemies within a generous radius are stunned for a duration. If the Warden is within that same radius when it detonates, Held Ground's effect is temporarily increased.

**Design intent:** Two legitimate, opposite uses rather than one fixed purpose. Thrown ahead of the Warden, it's a standard crowd-control opener against a pack. Thrown at the Warden's own feet mid-fight, it becomes a defensive reset — a stun window on nearby enemies combined with a spike to the Warden's own survivability at exactly the moment it's needed. The connection to Held Ground (rather than existing as a standalone effect) ties the ability into the kit's internal logic instead of functioning in isolation.

**Material identity:** deliberately framed as improvised and industrial rather than a clean magical effect, consistent with the Environment Art Direction doctrine's Tier 2 material logic (hastily-reinforced steel, salvaged plate) — this is a defensive tool built the same way Eagle's Landing's abandoned barricades were built.

---

### Draw — Weapon Ability

**What it does:** The Warden's eyes glow bright and they cry out, taunting nearby enemies. Increases the Warden's own damage mitigation for the duration. Increases I Am the Wall's fill rate for the duration.

**Design intent:** The kit's one deliberately selfish ability. Every other Warden tool protects or empowers others in some way — Draw is the single moment the Warden is allowed to make the fight entirely about themselves, because drawing an enemy's full attention is inherently a solitary act. This is the direct mechanical expression of "over here uglies" from the Warden's original narrative — dark humor that is also completely sincere.

---

## Movement Ability

### Hold the Line

**What it does:** The Warden gains a burst of movement speed for a duration. While active, Held Ground's effect continues to apply regardless of the Warden's actual movement state.

**Design intent:** Deliberately not a dash. Early design explored short forward-dash concepts (with conditional taunt triggers tied to nearby wounded allies), but these were rejected for functionally duplicating the Hunter's Lunge under different narrative dressing — both were burst-movement-then-done tools occupying the same mechanical space regardless of trigger conditions.

Hold the Line instead inverts the Warden's own core rule on demand: Held Ground normally requires stillness to function, and this ability temporarily lifts that requirement rather than simply relocating the Warden. This is mechanically distinct from any dash — it doesn't move the Warden *instead of* being the wall, it lets the Warden be the wall *while* moving. Framed narratively as "breaking the shackles" — the thing that normally constrains the Warden (stillness) is briefly lifted, and they move freely while remaining fundamentally what they are.

**Resonance forks — conceptually locked, specifics deferred:** ending the speed-burst near an ally versus near an enemy is intended to branch into different follow-up effects (an ally-proximity protective effect versus an enemy-proximity offensive/control effect), mirroring the Hunter's Gone/The Close fork on The Lunge. Exact mechanical specifics of each fork are parked for future design, not yet locked.

---

## Covenant Ability

### The Wall Remains

**What it does:** While active, if the Warden would drop below 1 HP, they instead remain at 1 HP. The Warden cries out, enveloped in Prior Flame — increased aggro generation, damage mitigation, and damage output for the duration.

**Design intent:** The Warden's version of Repay. Not a release of built-up pressure (an earlier draft, "The Pressurized Release," was explicitly rejected for reading as relief/venting rather than endurance — the wrong emotional register for this class entirely). "The Wall Remains" is a refusal, not a release: the wall does not break, full stop, for the duration.

**Design purpose beyond the moment:** intentionally built as a death-defiance tool specifically to create high-stakes resource-management tension in demanding content — a Covenant a party may choose to hold in reserve for the moment a wipe is otherwise guaranteed, particularly in raid-scale content. This mirrors the same kind of save-it-for-when-it-matters tension the token/med-kit revival economy (see Death & Failure States doc) is intended to create at raid scale, giving raid design two independent levers that both reward disciplined resource conservation under pressure.

**Development history — scope correction:** an earlier version of this ability additionally included an ally-facing buff at range and an end-of-duration damage-reflect effect against nearby enemies, bundling four distinct mechanical jobs (death-defiance, self-buff, ally-buff, delayed AoE) into a single ability. This was deliberately cut down to its core two components (death-defiance plus self-buff) for the same reason The Reckoning stays mechanically simple despite being the Hunter's most powerful moment — a Covenant's power should come from precision, not from stacking simultaneous effects that are difficult for both the player and the designer to reason about at once.

**Resonance forks — conceptually locked, specifics deferred:** the cut ally-buff-at-range and end-of-duration reflect-damage ideas are preserved as candidate Resonance fork directions for this ability, rather than discarded outright. Not yet built or specified.

---

## Kit-Internal Cohesion

With the exception of Draw (intentionally standalone, see above), every ability in this kit connects mechanically to at least one other ability in the kit:

- Shrapnel → amplifies Held Ground
- Draw → amplifies I Am the Wall's fill rate
- Hold the Line → sustains Held Ground's effect through motion
- The Wall Remains → the Covenant payoff of enduring what Held Ground and I Am the Wall are built to survive

This internal interlocking mirrors the same design quality the Hunter kit achieves through The List (Passive 1) feeding player behavior and Not At All (Passive 2) feeding directly into The Reckoning's duration — no ability in either kit exists in total isolation except by deliberate, stated design choice (Draw for Warden).

---

## Design Tenet Alignment

- **All classes must be viable** — I Am the Wall's explicit inclusion of the Warden in its own "allies" radius ensures the passive functions correctly in solo and open-world content, not only in group play.
- **Every system serves the feeling** — Hold the Line and The Wall Remains were both substantially reworked mid-design specifically because their initial versions, while mechanically functional, didn't express Warden identity precisely enough (a generic dash; a "release" framing that contradicted the class's core refusal-not-relief nature).
- **Respect the player's intelligence** — The Wall Remains' death-defiance mechanic is a legible, visible state change (Prior Flame envelopment) rather than a hidden save-mechanic, consistent with the same UI/legibility standard set for Resonance Strain's encumbrance bar.

---

## Open Items for Future Development

- Exact I Am the Wall grace-period duration on cessation (deferred to playtesting, per design discussion)
- Hold the Line's ally-proximity and enemy-proximity Resonance forks — conceptually locked, mechanics unspecified
- The Wall Remains' ally-buff and reflect-damage Resonance forks — conceptually locked, mechanics unspecified, salvaged from an earlier overloaded draft of the base ability
- Warden Prior Flame color — must be selected and checked against the exclusion rule in the Environment Art Direction doc's VFX Language section (no overlap with Shaped signatures, warm-register colors, or the Hunter's established blue)
- Exact numerical tuning across the full kit (damage, stun durations, charge thresholds) — entirely unaddressed, pending implementation and playtesting

---

*Document generated: July 2026*
*Session: Vigil Design — Warden Class Kit*
