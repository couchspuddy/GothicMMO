# Weapon Perk Tables
**Vigil — Rolled Weapon Perks: Bucket Structure, Curated Pool, and Per-Weapon Exclusions**
*Status: Draft for review — 2026-08-04. Structure and roll counts are decided; per-weapon exclusion calls are first-pass and open to correction; numeric coefficients (Strain costs, Marksman's Due refund amount, etc.) are explicitly deferred pending playtesting, consistent with the Live Balance Philosophy in `PROGRESSION_STATS_AND_BALANCE.md`.*

*This document supersedes the "PROPOSAL — Rolled Weapon Perks" section of PR #37 in `WEAPON_ARCHETYPES.md`. That section's 14-perk catalog is the starting point for everything below; this document adds the bucket structure, the exclusion schema, and the full per-weapon curation pass PR #37 explicitly deferred.*

---

## Governing Philosophy

Weapon perks exist to answer a question armor's stat budget doesn't: *"what is this particular weapon good at."* Deterministic tier damage (`WEAPON_ARCHETYPES.md`, DECIDED 2026-08-04) already answers "how strong is this object" — perks are the per-drop variance axis on top of that, same job rolled secondary stats do for armor.

**This system is checked against `DESIGN_TENETS.md`'s Tools, Not Puzzles tenet specifically.** The perk pool must stay wide and non-converging — no perk should be a mandatory pick, and no tier should weight toward a narrow "best" set as it rises. A perk being excellent for one playstyle and actively bad for another is the intended shape, not a flaw to balance away.

---

## Roll Structure

| Rarity | Rolls |
|---|---|
| Salvage / Kept / Remembered | 0 — unchanged from `ITEMIZATION_AND_LOOT.md` |
| **Resonant** | **3 total** — 1 from Fine-Tune, 1 from Verb Bucket A, 1 from Verb Bucket B |
| **Pure** | Fixed class-conditional trait **+ all 3** of the above (1 Fine-Tune + 1 A + 1 B) |

**Why 3 rolls from 3 discrete buckets, not a flat pool:** two failure modes surfaced when the pool was tested as one undifferentiated list of 14. First, two Fine-Tune-weight perks landing on the same weapon left it feeling mechanically inert — no verb-level identity, just two small numeric adjustments. Second, several perks exist as genuine opposite pairs (see Steady Read/Moving Target, Frugal Hand/Overcharge, Muffled Work/Dread Report below) — a flat roll could land both halves of a pair on one weapon, permanently wasting half the roll since the two effects can never both pay off. Guaranteeing exactly one roll per bucket prevents both failure modes by construction rather than by luck.

**Confirmed 2026-08-04: weapons cost Resonance Strain at Resonant/Pure rarity, same fixed 100-point pool as armor.** A committed weapon build now directly competes with armor depth for the same capacity. Exact weapon Strain costs are not yet drafted — `RESONANCE_STRAIN_AND_GEAR_LIFECYCLE.md`'s "Strain Costs — First Numbers" section currently only covers armor and needs a weapon-side pass before this is fully specified.

**Pure weapon trait design is explicitly benched.** Locked as its own category requiring separate design attention (2026-08-04); do not extrapolate Pure weapon identity from the Resonant perk tables below.

---

## Cut From the Original Catalog

**Practiced Motion** (reload 20% faster) — cut outright, not deferred. Fails the mandatory-pick test: there is no weapon or playstyle it's bad for, which means it's never a real decision under Tools, Not Puzzles. Steadfast's hold-reload economy already does this system's job.

---

## Fine-Tune Bucket (1 rolls, capped)

Numeric-only perks that don't create a playstyle decision on their own — real, but not worth spending a full verb-bucket slot on. Parked here rather than cut, since a future minor/tertiary roll slot could reasonably draw from this pool later.

| Perk | Effect |
|---|---|
| Dead Hand | Recoil pitch −30% |
| True Bore | Yaw spread −50% |
| Quick Hands | Swap-to speed +25% |
| Deep Reserves | +50% max reserve ammo |
| Extended Magazine | +25% magazine capacity, rounded up |

---

## Verb Bucket A — In-Fight Behavior

Perks that change what happens *during* an engagement — proc timing, positioning commitment, vital-hit payoff.

| Perk | Effect |
|---|---|
| Jolt | 8% chance per hit to stagger target 1s |
| Drumbeat | Every 8th consecutive unmissed hit on one target staggers it. **Reload does not reset the streak** — only a miss or a weapon swap does, matching the live Oversurge Repeater's `GetConsecutiveHits()` behavior (`Character/GothicPlayerCharacter.cpp`) |
| Steady Read | +0.25 `VitalDamageMultiplier` while stationary (no movement input in the last 0.5s) |
| Moving Target | Vital hits while sprinting/strafing: flat +20% damage (not a multiplier stack, deliberately smaller ceiling than Steady Read) |
| Marksman's Due | A vital hit returns 1 round to the magazine. **Excluded on `MagazineCapacity ≤ 3`** — see Numeric-Threshold Exclusions below |
| Kindling | `SuperGainOnHit` +60% (5 → 8) |

---

## Verb Bucket B — Economy & Utility

Perks that shape how an engagement is sustained around the player — ammo economy, Steadfast cost, aggro control.

| Perk | Effect |
|---|---|
| Well-Tended | Steadfast refill restores 50% more reserve ammo |
| Charitable Toll | Steadfast refill costs 1 fewer charge (minimum 1) |
| Frugal Hand | Hold-reload always yields one ammo tier lower, at half Steadfast cost |
| Overcharge | Hold-reload always yields one ammo tier higher, at 1.5× Steadfast cost |
| Spent Well | Covenant activation instantly refills Steadfast to full |
| Muffled Work | Hearing-aggro radius ×0.5 |
| Dread Report | Hearing-aggro radius ×1.5 |

**Steadfast scope, confirmed 2026-08-04:** the meter accumulates globally through combat regardless of equipped weapon; spending (tap/hold reload, and any perk interaction with cost/output) is scoped to the currently equipped weapon. This means Spent Well is fully eligible on every weapon including Heavy Melee — refilling the shared meter while melee is equipped still pays off the moment the player swaps to a gun.

---

## Categorical Exclusion Rules

Two exclusion patterns apply across multiple weapons for a shared underlying reason. Recommended as engine-level tags rather than hand-maintained per-weapon lists, so future weapons inherit the correct exclusions automatically.

### Vital-dependent perks (`bCanScoreVitalHits` or equivalent)

Excluded wherever a weapon cannot register a vital hit at all — currently only **Bomb Thrower** (`VitalDamageMultiplier = 1.0`; per its own doc entry, "an explosion does not find a vital point").

**Excludes:** Steady Read, Moving Target, Marksman's Due.

### Ammo/Steadfast-dependent perks (`bUsesAmmo == false`)

Excluded wherever a weapon has no ammo or Steadfast interaction — currently only **Heavy Melee**.

**Excludes:** Deep Reserves, Extended Magazine, Charitable Toll, Well-Tended, Frugal Hand, Overcharge. Marksman's Due is also non-functional here (no magazine to refund into) but doesn't cleanly fit the tag — see Open Items.

**This leaves Heavy Melee's effective pool meaningfully thinner than every other weapon's** — 6 of the 13 core perks are structurally dead on it, plus Marksman's Due. Flagged as an open item, not silently accepted.

### Numeric-threshold exclusions

Distinct from the categorical tag rules above: a threshold check rather than a fixed list, so it requires no hand-maintenance as new weapons are added.

**Marksman's Due excluded where `MagazineCapacity ≤ 3`.** Refund-as-percentage-of-magazine breaks cleanly at this line:

| Weapon | Mag | Refund % | Eligible? |
|---|---|---|---|
| Breacher | 1 | 100% | No |
| Derringer | 2 | 50% | No |
| Sawed-Off | 2 | 50% | No |
| Bomb Thrower | 3 | 33% | No (also vital-dependent) |
| Bolt-Action | 5 | 20% | **Yes** |
| Revolver | 6 | 16.7% | **Yes** |
| Lever-Action | 8 | 12.5% | Yes |
| Repeating Pistol | 12 | 8.3% | Yes |
| Carbine | 20 | 5% | Yes |
| Gatling | 60 | 1.7% | Yes |

The line is drawn at ≤3 specifically to preserve Bolt-Action and Revolver eligibility — both are the perk's strongest thematic matches (Bolt-Action: *"pairs with The Read by design"*; Revolver: *"2 vitals kill a Thrall exactly, that is the whole pitch"*) and sit at a materially milder 17–20% refund rate than the excluded cluster.

---

## Per-Weapon Curation — Named-List Exclusions and Standouts

Beyond the categorical/threshold rules above, these are first-pass, hand-curated judgment calls — flagged as open to correction on playtesting, per Live Balance Philosophy.

| Weapon | Excluded (archetype-tier) | Standout pairing |
|---|---|---|
| Revolver | Dead Hand, True Bore (single deliberate shots, nothing compounds/sprays) | Steady Read, Marksman's Due |
| Repeating Pistol | — | Dead Hand, True Bore, Jolt, Kindling, Well-Tended (true automatic, matches doc's own examples) |
| Derringer | — (Extended Magazine excluded, archetype-warping — see Fine-Tune notes) | Quick Hands, Steady Read (doc's own punctuation-weapon example) |
| Lever-Action Repeater | — | none singled out — the "generally correct" baseline weapon, broadly eligible everywhere by design |
| Bolt-Action Rifle | Dead Hand, True Bore (doc: *"already kicks perfectly vertical"*), Jolt (weak, slow cooldown) | **Steady Read** — strongest single pairing in the catalog |
| Sawed-Off | True Bore, **Steady Read** (excluded on design-intent grounds — doc: *"should not also reward precision"*), Marksman's Due (low vital mult + contrary design intent) | Moving Target, Dread Report |
| Carbine | — | Dead Hand, True Bore, Jolt, Kindling, Extended Magazine, **Well-Tended** (doc's own cited example) |
| Gatling Rig | True Bore (excluded on design-intent grounds — doc: *"accuracy degrades as a natural consequence of holding the trigger,"* deliberate) | Jolt, Kindling, **Charitable Toll** (doc's own cited example), Frugal Hand/Overcharge/Spent Well (highest-stakes Rig economy) |
| Bomb Thrower | Steady Read, Moving Target, Marksman's Due (vital-dependent, see categorical rule) | Deep Reserves, Dread Report |
| Breacher | Dead Hand, True Bore, Extended Magazine (most extreme archetype-warping case, 1→2), Jolt (doc's own example — *"near-worthless on the Breacher"*), Drumbeat (design-feel exclusion, not mechanical — technically legal but absurdly slow), Marksman's Due (mag ≤3 rule) | Quick Hands (doc's own example), **Charitable Toll, Spent Well** (doc's own example — most expensive weapon to feed) |
| Heavy Melee | Deep Reserves, Extended Magazine, Charitable Toll, Well-Tended, Frugal Hand, Overcharge (all ammo/Steadfast-dependent), True Bore (yaw already 0.0) | Drumbeat (cleanest possible fit — nothing ever interrupts the streak), Spent Well |

---

## Open Items

- **Heavy Melee's thin effective pool.** Needs a decision: author bespoke melee-flavored perks (a Stamina-refund equivalent to Marksman's Due, etc.), or accept it as the one archetype with meaningfully less build variance by design.
- **Marksman's Due on Heavy Melee.** No magazine exists to refund into. Doesn't cleanly fit the ammo/Steadfast tag (it's specifically about the *refund target*, not ammo/Steadfast cost generally) — needs either a melee-specific redesign or an explicit standalone exclusion.
- **Weapon Strain costs.** Confirmed weapons participate in the same 100-point Strain pool as armor; exact per-rarity/per-star costs not yet drafted. `RESONANCE_STRAIN_AND_GEAR_LIFECYCLE.md`'s existing "Strain Costs — First Numbers" schedule is armor-only and needs a weapon-side pass.
- **Pure weapon trait design.** Explicitly benched as its own design space.
- **Implementation constraint, carried over from PR #37:** perks must live on the item instance, not the shared `UGothicWeaponData` asset. Today's per-weapon hooks that this system generalizes (Oversurge's stun/streak fields included) live on the shared asset, meaning two drops of the same archetype currently cannot differ. The seam: `FGothicItemInstance` needs a perk list, and `GA_Fire` (plus the Steadfast/reload paths) needs to read the equipped instance's perks instead of only the shared asset's. No code specified here — recorded so implementation doesn't assume the data can already carry this.
- **Marksman's Due refund amount** on small-mag weapons (Derringer, Bolt-Action, Breacher) flagged for numeric tuning attention once real playtesting exists — the current flat "1 round" may be worth scaling down or gating by proc chance on the smallest mags even within the eligible range.

---

*Document generated: August 2026*
*Session: Vigil Design — Weapon Perk System*
