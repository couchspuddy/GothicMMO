# Weapon Archetypes

*Generated July 22, 2026. Eleven archetypes, first numeric pass.*

Prior to this document the eleven archetypes existed only as names in a single
line of `PRODUCTION_STATUS_TRACKER.md`. This fills in the numbers under them.

Every value here lives in a `UGothicWeaponData` data asset. Nothing below
requires a recompile to retune.

---

## Calibration Reference

All numbers are set against a **Thrall at 100 HP** — the `UGothicAttributeSet`
default, which every enemy currently inherits. Times-to-kill quoted below assume
body shots on a Thrall with no armor mitigation, since armor mitigation is not
yet in the damage pipeline.

Two system gaps shape these numbers and are called out rather than designed
around:

- **There is no damage falloff.** `PerformFireTrace` is a single hitscan against
  `TraceRange` at flat damage. A Sawed-Off authored at 70 damage deals 70 at its
  full 900cm reach, with a hard cliff to zero beyond it. The short-range
  archetypes (Sawed-Off, Breacher) are balanced by that hard range cliff instead
  of by a falloff curve, which is cruder but honest to what the code does.
- **There is no pellet spread.** A shotgun is one trace, not eight. Sawed-Off is
  therefore tuned as a single high-damage close-range shot rather than a spread
  weapon. If pellets arrive later its damage must be divided across them.

---

## Slot Doctrine

The three slots are not a power ladder — they are three different relationships
with the Steadfast economy. `SteadfastRefillCost` is the mechanism.

| Slot | Refill cost | What the slot is for |
|---|---|---|
| **Sidearm** | 1 charge | The weapon you can always afford to feed. Low commitment, low ceiling. |
| **Piece** | 2 charges | The workhorse. A refill costs a real defensive option. |
| **Rig** | 3 charges | The panic button. Refilling leaves you at zero charges — this is the tension the tiering exists to create. |

A Rig refill mid-fight should feel like a decision you might regret. If it
doesn't, lower `SteadfastRefillAmount` on the Rigs before touching the cost.

---

## Sidearms — `IntendedSlot = Sidearm`, `SteadfastRefillCost = 1`

### DA_Weapon_Revolver
The baseline every other weapon is described against. Six shots, deliberate,
rewards precision.

| Field | Value |
|---|---|
| `Damage` | 22 |
| `VitalDamageMultiplier` | 2.5 |
| `TraceRange` | 4500 |
| `MagazineCapacity` | 6 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 24 / 24 |
| `SteadfastRefillAmount` | 12 |
| `RecoilPitch` / `RecoilYawSpread` | -0.8 / 0.15 |
| `CrosshairType` | Pistol |
| Fire cooldown | 0.45s |
| Mesh | `SM_Pistol`, baseline transform |

5 body shots or 2 vitals to kill a Thrall.

### DA_Weapon_RepeatingPistol
Trades the Revolver's punch for volume and forgiveness. The archetype for
players who miss.

| Field | Value |
|---|---|
| `Damage` | 11 |
| `VitalDamageMultiplier` | 2.0 |
| `TraceRange` | 3500 |
| `MagazineCapacity` | 12 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 48 / 48 |
| `SteadfastRefillAmount` | 24 |
| `RecoilPitch` / `RecoilYawSpread` | -0.3 / 0.5 |
| `CrosshairType` | Pistol |
| Fire cooldown | 0.16s |
| Mesh | `SM_Pistol`, ~0.9 scale, tighter offset |

9 body shots to kill, but a full magazine kills without reloading.

### DA_Weapon_Derringer
Two shots that matter enormously, then you are holding nothing. The highest
per-shot damage in the Sidearm slot, on the smallest magazine in the game.

| Field | Value |
|---|---|
| `Damage` | 45 |
| `VitalDamageMultiplier` | 2.5 |
| `TraceRange` | 2000 |
| `MagazineCapacity` | 2 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 8 / 8 |
| `SteadfastRefillAmount` | 6 |
| `RecoilPitch` / `RecoilYawSpread` | -1.6 / 0.2 |
| `CrosshairType` | Pistol |
| Fire cooldown | 0.6s |
| Mesh | `SM_Pistol`, ~0.6 scale, pulled in close |

Two vitals kill a Thrall exactly. That is the whole pitch.

---

## Pieces — `IntendedSlot = Piece`, `SteadfastRefillCost = 2`

### DA_Weapon_LeverActionRepeater
The default Piece and the most generally correct weapon in the game.

| Field | Value |
|---|---|
| `Damage` | 26 |
| `VitalDamageMultiplier` | 2.5 |
| `TraceRange` | 7000 |
| `MagazineCapacity` | 8 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 32 / 32 |
| `SteadfastRefillAmount` | 16 |
| `RecoilPitch` / `RecoilYawSpread` | -0.9 / 0.1 |
| `CrosshairType` | Rifle |
| Fire cooldown | 0.5s |
| Mesh | `SM_Rifle`, baseline transform |

### DA_Weapon_BoltActionRifle
The precision option. Slowest fire rate outside the Rigs, longest reach in the
game, and the highest vital multiplier — the only weapon where the vital system
is the entire point.

| Field | Value |
|---|---|
| `Damage` | 60 |
| `VitalDamageMultiplier` | 3.0 |
| `TraceRange` | 12000 |
| `MagazineCapacity` | 5 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 20 / 20 |
| `SteadfastRefillAmount` | 10 |
| `RecoilPitch` / `RecoilYawSpread` | -1.5 / 0.0 |
| `CrosshairType` | Rifle |
| Fire cooldown | 1.1s |
| Mesh | `SM_Rifle`, pushed forward, ~1.1 scale |

Zero yaw spread is deliberate — a rifle that wanders horizontally cannot be the
precision weapon. It is the only weapon in the set with a perfectly vertical kick.

One vital shot kills a Thrall. Pairs with The Read by design.

### DA_Weapon_SawedOff
Close-range deletion with a hard range cliff. See the falloff note above — the
cliff is doing the work a curve normally would.

| Field | Value |
|---|---|
| `Damage` | 70 |
| `VitalDamageMultiplier` | 1.5 |
| `TraceRange` | 900 |
| `MagazineCapacity` | 2 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 12 / 12 |
| `SteadfastRefillAmount` | 8 |
| `RecoilPitch` / `RecoilYawSpread` | -2.2 / 0.6 |
| `CrosshairType` | Pistol (see crosshair note) |
| Fire cooldown | 0.8s |
| Mesh | `SM_Rifle`, ~0.7 scale, pulled back |

Low vital multiplier is intentional: a weapon this forgiving at range zero should
not also reward precision.

### DA_Weapon_Carbine
Sustained mid-range fire. The Piece slot's answer to the Repeating Pistol, with
enough magazine to hold a corridor.

| Field | Value |
|---|---|
| `Damage` | 18 |
| `VitalDamageMultiplier` | 2.0 |
| `TraceRange` | 6000 |
| `MagazineCapacity` | 20 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 60 / 60 |
| `SteadfastRefillAmount` | 30 |
| `RecoilPitch` / `RecoilYawSpread` | -0.35 / 0.35 |
| `CrosshairType` | Rifle |
| Fire cooldown | 0.12s |
| Mesh | `SM_Rifle`, ~0.85 scale |

---

## Rigs — `IntendedSlot = Rig`, `SteadfastRefillCost = 3`

### DA_Weapon_GatlingRig
Sustained suppression. Lowest per-shot damage in the game against by far the
largest magazine — it exists to answer the Cry-spawned Thrall packs.

| Field | Value |
|---|---|
| `Damage` | 12 |
| `VitalDamageMultiplier` | 1.5 |
| `TraceRange` | 5000 |
| `MagazineCapacity` | 60 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 120 / 120 |
| `SteadfastRefillAmount` | 60 |
| `RecoilPitch` / `RecoilYawSpread` | -0.25 / 0.8 |
| `CrosshairType` | Rifle |
| Fire cooldown | 0.07s |
| Mesh | `SM_Rifle`, ~1.4 scale, lowered offset |

Highest yaw spread in the set — accuracy degrades as a natural consequence of
holding the trigger, without needing a heat or bloom system.

### DA_Weapon_BombThrower
Three shots of very large damage. Vital multiplier is 1.0 because an explosion
does not find a vital point.

| Field | Value |
|---|---|
| `Damage` | 90 |
| `VitalDamageMultiplier` | 1.0 |
| `TraceRange` | 6000 |
| `MagazineCapacity` | 3 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 9 / 9 |
| `SteadfastRefillAmount` | 6 |
| `RecoilPitch` / `RecoilYawSpread` | -2.0 / 0.2 |
| `CrosshairType` | Throwable |
| Fire cooldown | 1.4s |
| Mesh | `SM_GrenadeLauncher`, baseline transform |

Currently a hitscan like everything else. When projectiles and AoE arrive this
is the archetype that changes most.

### DA_Weapon_Breacher
One shot, then a long wait. The single highest-damage trigger pull in the game,
at the shortest usable range outside melee.

| Field | Value |
|---|---|
| `Damage` | 110 |
| `VitalDamageMultiplier` | 1.5 |
| `TraceRange` | 1500 |
| `MagazineCapacity` | 1 |
| `MaxReserveAmmo` / `StartingReserveAmmo` | 6 / 6 |
| `SteadfastRefillAmount` | 4 |
| `RecoilPitch` / `RecoilYawSpread` | -3.0 / 0.4 |
| `CrosshairType` | Pistol (see crosshair note) |
| Fire cooldown | 1.8s |
| Mesh | `SM_GrenadeLauncher`, ~1.2 scale, rotated |

One-shots a Thrall on a body hit. Four reserve rounds per Steadfast refill, at
three charges, makes this the most expensive weapon in the game to feed.

### DA_Weapon_HeavyMelee
**`bUsesAmmo = false`.** The only weapon in the set that carries no ammo, never
reloads, and never costs Steadfast. Its cost is that it has no range at all.

| Field | Value |
|---|---|
| `Damage` | 55 |
| `VitalDamageMultiplier` | 2.0 |
| `TraceRange` | 250 |
| `bUsesAmmo` | **false** |
| Magazine / reserve / refill fields | ignored — leave at defaults |
| `RecoilPitch` / `RecoilYawSpread` | -0.5 / 0.0 |
| `CrosshairType` | Melee |
| Fire cooldown | 0.9s |
| Mesh | none suitable — see below |

Two body hits kill a Thrall. It is the answer to running dry, which is why it
must never itself run dry.

---

## Summary Table

| Archetype | Slot | Dmg | Vital× | Range | Mag | Reserve | Cooldown | Refill | Crosshair | Mesh |
|---|---|---|---|---|---|---|---|---|---|---|
| Revolver | Sidearm | 22 | 2.5 | 4500 | 6 | 24 | 0.45s | 1 | Pistol | `SM_Pistol` |
| Repeating Pistol | Sidearm | 11 | 2.0 | 3500 | 12 | 48 | 0.16s | 1 | Pistol | `SM_Pistol` |
| Derringer | Sidearm | 45 | 2.5 | 2000 | 2 | 8 | 0.60s | 1 | Pistol | `SM_Pistol` |
| Lever-Action Repeater | Piece | 26 | 2.5 | 7000 | 8 | 32 | 0.50s | 2 | Rifle | `SM_Rifle` |
| Bolt-Action Rifle | Piece | 60 | 3.0 | 12000 | 5 | 20 | 1.10s | 2 | Rifle | `SM_Rifle` |
| Sawed-Off | Piece | 70 | 1.5 | 900 | 2 | 12 | 0.80s | 2 | Pistol | `SM_Rifle` |
| Carbine | Piece | 18 | 2.0 | 6000 | 20 | 60 | 0.12s | 2 | Rifle | `SM_Rifle` |
| Gatling Rig | Rig | 12 | 1.5 | 5000 | 60 | 120 | 0.07s | 3 | Rifle | `SM_Rifle` |
| Bomb Thrower | Rig | 90 | 1.0 | 6000 | 3 | 9 | 1.40s | 3 | Throwable | `SM_GrenadeLauncher` |
| Breacher | Rig | 110 | 1.5 | 1500 | 1 | 6 | 1.80s | 3 | Pistol | `SM_GrenadeLauncher` |
| Heavy Melee | Rig | 55 | 2.0 | 250 | — | — | 0.90s | — | Melee | none |

---

## Authoring Notes

**Fire rate is a GameplayEffect, not a float.** `CooldownEffect` is a
`TSubclassOf<UGameplayEffect>`, so each distinct cooldown above needs a GE asset
with that duration. Weapons sharing a rate can share one GE — the eleven
archetypes above use eleven distinct rates as written, so either author eleven
`GE_WeaponCooldown_*` assets or round rates together to reduce the count. Rounding
is the cheaper call for a first pass.

**Crosshair coverage is thin — four reticles for eleven weapons.**
`EGothicCrosshairType` currently offers exactly `Melee`, `Pistol`, `Rifle`, and
`Throwable`. Every archetype above is assigned one of those four, so the table is
authorable as written, but the Sawed-Off and the Breacher are both wearing the
Pistol reticle purely for lack of a better one. Adding a `Shotgun` entry is worth
doing before authoring.

If you add one, **append it at the end of the enum, never insert it mid-list.**
`EGothicCrosshairType` has no explicit values, so every entry after an insertion
point shifts by one and every already-authored asset silently reads a different
reticle. This is the same trap that `EGothicEquipSlot` was given explicit values
to escape; the crosshair enum has not had that treatment yet and is still exposed
to it.

**Each archetype needs two assets, not one** — a `UGothicWeaponData` and a
`UGothicItemDefinition` whose `WeaponData` points at it and whose `EquipSlot`
matches `IntendedSlot`. `EquipItem` now refuses the equip and logs both values if
they disagree.

**Heavy Melee has no mesh.** The three available meshes are a pistol, a rifle,
and a grenade launcher; none reads as a melee weapon at any scale. Either author
this archetype last, or ship it holding `SM_GrenadeLauncher` and accept that it
looks wrong until an asset exists.

---

## DECIDED (2026-08-04) — Weapon Damage Scales by the Weapon's Own Tier

**DRAFT FOR REVIEW 2026-08-04.** *The decision is settled; every number below is
a first-pass proposal for redline.*

**Option 1 wins, applied per-instance.** A weapon's damage scales by its own
instance's Gear Power, not by anything the wearer has on:

```
FinalWeaponDamage = WeaponData.Damage
                  × (WeaponInstance.GearPower / 100)
                  × (1 + ArchetypeBonusPct / 100)
```

**The armor-average GearFloor is removed from the weapon formula.** Since this
document's first pass, an interim implementation scaled weapon damage by the
*armor-average* Gear Power — so the diagnosis preserved below became half-stale:
weapon damage did start reading gear, but the **wrong** gear. Ten armor slots
voted on how hard a Revolver hits while the Revolver's own rarity still did
nothing, and the average diluted any single armor upgrade to a tenth of its
face value. Both problems end here. Armor tier now expresses through Gear Score
→ Attack Power (see the Gear Score draft in `ITEMIZATION_AND_LOOT.md`); weapon
tier expresses through the weapon itself. Each axis pays off on the object the
player actually farmed — which is the entire point of a loot chase.

Why option 1 over its rivals:

- **A pure multiplier preserves archetype identity.** Relative DPS ordering is
  unchanged at every tier; the Derringer's pitch is the Derringer's pitch at
  Tier 1 and Tier 5. Balance work done once holds everywhere.
- **Option 2 (a rolled damage secondary) dies on an existing ruling** —
  itemization explicitly rejected flat damage from the rollable pool, and a
  weapon-side damage roll is the same tax with a different collector. Weapon
  per-drop variance is instead carried by rolled *perks* (see the proposal
  below), which pass the "who does this help" test that damage never can.
- **Option 3 (accept it) forfeits the weapon-side loot chase** at the exact
  moment the rarity ladder makes weapons droppable at five rarities.

Since `GetGearPower() = GearTier × 100` with no per-drop variance (deliberate),
the multiplier **is** the tier: ×1 at Tier 1, ×2 at Tier 2, ×3 at Tier 3, ×5 at
Tier 5. **Salvage weapons floor at ×1.0** — Salvage has no tier and no upgrade
path, so a Salvage weapon fires at book value, identical to Tier 1. That keeps
the Calibration Reference at the top of this document true as written: every
authored `Damage` value is the Tier-1/Salvage number.

### Worked Damage Table — Per Shot, `ArchetypeBonusPct = 0`

| Archetype | Base | Tier 1 (×1) | Tier 2 (×2) | Tier 3 (×3) | Tier 5 (×5) |
|---|---|---|---|---|---|
| Revolver | 22 | 22 | 44 | 66 | 110 |
| Repeating Pistol | 11 | 11 | 22 | 33 | 55 |
| Derringer | 45 | 45 | 90 | 135 | 225 |
| Lever-Action Repeater | 26 | 26 | 52 | 78 | 130 |
| Bolt-Action Rifle | 60 | 60 | 120 | 180 | 300 |
| Sawed-Off (Scattergun) | 70 | 70 | 140 | 210 | 350 |
| Carbine | 18 | 18 | 36 | 54 | 90 |
| Gatling Rig | 12 | 12 | 24 | 36 | 60 |
| Bomb Thrower (Censer Launcher) | 90 | 90 | 180 | 270 | 450 |
| Breacher | 110 | 110 | 220 | 330 | 550 |
| Heavy Melee | 55 | 55 | 110 | 165 | 275 |
| Oversurge Repeater* | 4 | 4 | 8 | 12 | 20 |

*\*The Oversurge Repeater postdates this document's first pass (12 weapons now,
not 11); it is listed here for the tier table only. Its identity lives in its
hooks — 12% stun chance and a streak damage multiplier — which the tier
multiplier scales alongside the base 4.*

Tier 4 stays a skipped step for weapons exactly as it is for armor: no rarity's
ceiling lands there, and the ×3 → ×5 jump is the same deliberate cliff.

**Enemy-side counterpart is not designed here.** A Tier-5 Breacher hits for 550
against enemy pools authored for the Tier-1 chart; current enemies are
effectively Tier-1 content. The enemy stat/HP tiering that must grow opposite
this curve is flagged as an open sub-question in the Gear Score draft
(`ITEMIZATION_AND_LOOT.md`).

### The Original Question, Preserved for the Record

*Superseded 2026-08-04 by the decision above; kept because the diagnosis is the
rationale.*

A weapon's damage came entirely from its `UGothicWeaponData`. Rarity, gear tier,
star ceiling, and rolled stats all live on the `UGothicItemDefinition` and its
`FGothicItemInstance`; at the time of writing nothing in `PerformFireTrace` read
them *(later interim state: it read the armor-average Gear Power — now also
superseded)*.

The consequence: **a Salvage Revolver and a Pure Revolver hit for exactly 22.**
Two players with the same archetype have identical weapons regardless of what
either of them farmed. For armor this is fine — armor's whole contribution is
its rolled stats. For weapons it means the drop chase has no weapon-side payoff.

Three ways out were on the table: **(1) scale damage by the instance's Gear
Power — CHOSEN**; (2) let weapons roll a damage secondary — rejected, see above;
(3) accept it — rejected, see above.

---

## PROPOSAL — Rolled Weapon Perks (Not Yet Decided)

**SUPERSEDED 2026-08-04 by `WEAPON_PERK_TABLES.md`** *(user curation pass:
bucket structure, exclusions, per-weapon tables)*. The 14-perk catalog that
briefly lived here was the input to that pass; everything that survived it —
plus the 3-roll bucket structure, the categorical/threshold exclusion rules,
the per-weapon curation table, and the Practiced Motion cut — now lives there.
Weapon Strain participation is confirmed; the weapon-side draft numbers are in
`RESONANCE_STRAIN_AND_GEAR_LIFECYCLE.md`.

---

*Document generated: July 22, 2026*
*Numbers are a first pass, calibrated against a 100 HP Thrall. Expect a retune
after the first playtest that has more than two weapons in it.*
