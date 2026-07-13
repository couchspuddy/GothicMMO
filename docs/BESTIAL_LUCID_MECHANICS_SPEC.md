# Bestial Lucid — Boss Mechanics, Final Build Spec
**Vigil — Engine Implementation Reference**
*Status: Fully locked. Ready to build. Supplements the original Eagle's Landing Encounters doc and Eagle's Landing Blockout Scale doc.*

---

## What Already Exists (No New Work Needed)

- `BP_Enemy_BestialLucid` Blueprint created
- `VitalPointComponent` configured on her — `bShiftOnTimer = true`, timer + threshold shift both active
- `AGothicBossAIController` (base class) — generic phase bookkeeping, `CurrentPhase` tracking, `OnPhaseAdvance()`, `OnBossPhaseChanged` broadcast delegate
- `AGothicBossAIController_BestialLucid` (subclass) — listens to `OnVitalPointShifted`, checks for the designated `Phase2TriggerVitalIndex`, calls `OnPhaseAdvance()` when reached

---

## Phase 1 (~90 seconds)

**Attacks (no new moveset complexity — three total):**
- **Claw swipe** — primary attack, close range, frequent
- **Charge** — gap closer, telegraphed wind-up, covers distance, punishes standing still
- **Roar** — stagger-inducing, placeholder-acceptable quality for tonight's build

**Vital point:** standard behavior, already configured — shifts on damage threshold AND independent timer, per existing `VitalPointComponent` settings.

**Environmental behavior:** passive only — no new destruction logic needed in Phase 1.

---

## The Phase Shift — The Stillness Beat

- Triggers automatically via existing C++ when the vital reaches `Phase2TriggerVitalIndex`
- A brief pause — exact duration not locked, intended to be felt out once testable in engine rather than guessed at now
- No new logic required beyond what already exists in `AGothicBossAIController_BestialLucid::HandleVitalPointShifted` -> `OnPhaseAdvance()`

---

## Phase 2 (~90 seconds) — New Mechanics for Tonight

### 1. Vital Point Freeze

**What it does:** the vital point stops shifting entirely once Phase 2 begins - becomes a fixed, known target for the remainder of the fight.

**Design intent - locked reasoning, worth preserving:** this is a deliberate inversion, not a difficulty reduction. The player's information problem gets easier (they know exactly where to shoot) at the exact moment their execution problem gets harder (faster attacks, environmental hazard below). This gives the player a fair, felt payoff for surviving into Phase 2, while the actual difficulty increase comes from staying alive long enough to capitalize on it, not from the vital point itself.

**Implementation - this is C++, not Blueprint:** in `AGothicBossAIController_BestialLucid::OnPhaseAdvance()` (already stubbed as a comment in the existing code), add a call to disable further shifting on her `VitalPointComponent`. This requires either:
- A new public function on `UGothicVitalPointComponent` - e.g. `void FreezeVitalPoint()` - that sets `bShiftOnTimer = false` and blocks any further threshold-triggered shift, OR
- Direct property access if `bShiftOnTimer` and the threshold-shift logic are already exposed enough to toggle externally

**Flagged for tonight's C++ batching:** this needs a small addition to `GothicVitalPointComponent.h/.cpp` (the freeze function) plus the call site in `AGothicBossAIController_BestialLucid.cpp`'s `OnPhaseAdvance()` override. Small, contained, should be staged in Rider before opening the editor.

### 2. Timed Ceiling Collapse

**What it does:** during Phase 2 only, on a repeating timer (~15-20 second interval, tune to feel), one of the reserved destructible zones near a pillar (per the Eagle's Landing Blockout Scale doc) triggers a telegraph (dust/shake, brief warning), then drops debris in that area a couple seconds later, forcing the player to keep moving rather than holding one position.

**Design intent:** the actual goal for tonight is simply "the player can no longer stand still." This is intentionally the cheap, reliable version (timed and independent of her attacks) rather than tying collapse to a specific missed charge attack (a narratively tighter but more complex version, deferred as a future refinement, not needed for tonight's build).

**Implementation - mostly C++, small Blueprint-side visual component:**
- A `FTimerHandle` in `AGothicBossAIController_BestialLucid`, started in `OnPhaseAdvance()` (or wherever Phase 2 entry is finalized), calling a repeating function on the 15-20s interval
- That function needs to select one of the reserved destructible zones (could be as simple as cycling through a `TArray` of zone references, or randomly picking one) and trigger its telegraph-then-collapse sequence
- The actual telegraph/collapse visual (dust VFX, debris mesh becoming visible/blocking) is Blueprint-side - likely simplest as pre-placed debris actors that are hidden/non-colliding by default, then made visible and collision-enabled on trigger, rather than any real-time physics destruction
- Recommend a small helper function like `TriggerZoneCollapse(int32 ZoneIndex)`, Blueprint-callable from C++, keeping the actual "what does a collapse look like" logic in Blueprint where it's faster to iterate on visually, while the "when does it happen" timing logic stays in C++ alongside the rest of the phase-tracking system

**Same attack set continues in Phase 2** - no new moveset, though attack frequency/speed can be increased via existing AI parameters (attack cooldown tuning) rather than new logic.

---

## Tonight's Build Order, Given the Above

1. **C++ first (batch in Rider before opening the editor):**
   - `FreezeVitalPoint()` (or equivalent) added to `GothicVitalPointComponent`
   - `OnPhaseAdvance()` override in `AGothicBossAIController_BestialLucid` calls the freeze function
   - Timer setup for the repeating collapse trigger, plus a `TriggerZoneCollapse(int32 ZoneIndex)` Blueprint-callable function stub
2. **Blueprint/BT work, once compiled:**
   - Build `BT_BestialLucid` - Phase 1 sequence (claw/charge/roar selection logic), reads `CurrentPhase` Blackboard key to branch behavior
   - Wire the reserved destructible zone actors (hidden debris, made visible/blocking on trigger)
   - Tune the Stillness beat's actual pause duration by feel
   - Tune Phase 2 attack frequency by feel

---

*Document generated: July 2026*
*Session: Vigil Design - Bestial Lucid Final Mechanics Spec*
