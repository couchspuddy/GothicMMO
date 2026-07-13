# Eagle's Landing — Level Blockout Scale Reference
**Vigil — Engine Reference Document**
*Status: Working reference for tonight's blockout pass. Numbers are starting points — tune to feel once walkable in engine.*

---

## Method

All geometry uses Engine BasicShapes Cube scaled in world space, consistent with the original Eagle's Landing blockout approach (not Modeling Mode). World coordinates below are cumulative offsets — each area's origin is placed at the previous area's exit point plus the listed gap, so the level reads as one continuous walk rather than disconnected rooms.

---

## Encounter 1 — Intersection

**Purpose:** First real fight. Open enough for 5 Thralls to spread without feeling like a shooting gallery, small enough to still feel like a contained space, not open world.

- **Floor:** Scale X:25, Y:25, Z:0.4 (~2500x2500 units)
- **World origin:** Player spawn / tree line, offset ~500 units into the intersection from entry point
- **Blocking walls (side streets):** Scale X:1, Y:15, Z:6, positioned to read as building continuation rather than an obvious dead-end
- **Forward path opening:** ~8-10 unit gap in the far wall

**Gate:** Geographic — no alternate route exists, so the walls above ARE the gate, not a separate mechanism.

---

## Encounter 2 — Collapsed Building

**Purpose:** Confined, bad sightlines, the Z-route forced by the street collapse. Ground floor ambush, second floor Retained encounter.

- **World origin:** ~1500 units forward from Encounter 1's exit (accounts for street walk + the collapse event itself)
- **Ground floor footprint:** Scale X:12, Y:12, Z:0.4 (~1200x1200 units — noticeably smaller than Encounter 1)
- **Ground floor wall height:** Scale Z:6-8 (standard interior ceiling, lower than the boss chamber's grand scale)
- **Staircase:** 3-4 step sections, total rise ~Z:150-200 (simpler version of the original Area transition's Z:330→130→-70 pattern)
- **Second floor footprint:** Scale X:10, Y:10 (slightly smaller than ground floor — reads as a single room)

**Gate 1 (collapse → forced Z-route):** Scripted event, not a static block — street collapse triggers on approach or on a set trigger volume.
**Gate 2 (ground floor → second floor):** Natural chokepoint — the staircase itself.
**Building entrance door:** Locked, opens via a Thrall exiting the building once Encounter 1's Selah moment completes (scripted spawn/animation trigger tied to the Encounter 1 clear event).

---

## Encounter 3 — Plaza

**Purpose:** Largest standard encounter. Grid opening up toward the city center. Room for 8-10 Thralls plus a second wave arriving from an unexpected direction.

- **World origin:** ~1800 units forward from Encounter 2's second-floor exit
- **Floor:** Scale X:35-40, Y:35-40 (~3500-4000 units — largest standard encounter space)
- **Second wave entry point:** placed behind cover or around a corner relative to the player's likely position after Wave 1 — a side street or alley opening, not visible from the main engagement area

**Gate:** Trigger volume at the plaza's far exit, deactivated only once all enemies (both waves) are cleared and the interrupted Selah moment has completed.

---

## Approach to City Hall

**Purpose:** Transition space, not a full encounter area. Builds dread through silence and environmental tells (claw marks, darkened windows, structural warping) before the boss reveal.

- **World origin:** ~1200 units forward from Encounter 3's exit
- **Length:** ~1500-2000 units of straight or gently curving path
- **Width:** Scale X:8-10 (narrower than the plaza — funnels the player, reinforces "no other option")
- **No enemies placed here** — this space is empty by design, the silence itself is the content

**Gate:** One-way entrance at City Hall's threshold — once inside, no retreat, per the design doc's "commitment" requirement.

---

## Boss Den — City Hall Rotunda

**Purpose:** Bestial Lucid's two-phase fight. Base geometry mostly carries over from the original Area 3 plan, with one addition for Phase 2's shrinking-space mechanic.

- **World origin:** ~800 units forward from the Approach's one-way entrance
- **Floor:** Scale X:80, Y:60, Z:0.4, sunken at Z:-60 (unchanged from original plan)
- **Walls:** Scale Z:40, west wall removed for entrance (unchanged from original plan)
- **Pillars:** Four, Scale X:3, Y:3, Z:10 (unchanged from original plan)
- **New — reserved destructible zones:** do not fill the full 80x60 footprint with uniform open floor. Reserve 2-3 zones near the edges (debris piles, partially-collapsed sections) that can visually "close in" during Phase 2, consistent with the design doc's environmental destruction and shrinking-fight-space requirements. Exact placement and destruction mechanism (toggled visibility, physics-simulated debris, etc.) is an implementation detail to work out once the base geometry is walkable.

---

## Total Approximate Level Length

Tree line entry to boss den entrance: roughly **6,800-7,300 units** of forward progression across all areas and gaps, before accounting for the boss chamber's own 80-unit depth. Worth walking this end-to-end once blocked out to sanity-check pacing against the original 10-minute Contract target (2-3 minutes traversal, 4-4.5 minutes standard encounters, ~3 minutes boss).

---

## Gates Summary (Cross-Reference)

| Gate | Mechanism | Build Note |
|---|---|---|
| Intersection exits (left/right) | Static walls | Plain blocker, no narrative needed |
| Street → collapsed building | Scripted collapse event | Triggers the Z-route |
| Building entrance door | Locked, scripted unlock | Opens via Thrall exiting on Encounter 1 Selah completion |
| Ground floor → second floor | Natural chokepoint | Staircase itself, no additional gate needed |
| Plaza exit | Trigger volume | Deactivates on full clear + interrupted Selah completion |
| City Hall entrance | One-way trigger | No retreat once crossed |

---

*Document generated: July 2026*
*Session: Vigil Design — Eagle's Landing Blockout Scale Reference*
