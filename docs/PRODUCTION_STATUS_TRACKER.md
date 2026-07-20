# Tracker Update — July 20, 2026

## New "Last Updated" Entry (prepend to existing)

*Last updated: July 20, 2026 — Large session: Phase 2 blocker closed, HUD combat feedback built, Selah name cycling wired. **`BP_GA_BestialLucid_WallPound` created and verified in PIE** — `Granting 4 abilities` confirmed, Phase 2 transition completes end-to-end (Roar → Find Nearest Pillar → Move To → Wall Pound → `OnPhaseAdvance`). `ApproachOffsetDistance` verified working — she paths near the pillar, not into its collision. **Damage numbers built** via `DrawHUD()` canvas rendering: white body hits, gold vitals at 1.4× scale, black outline, tunable in BP_GothicHUD. `MulticastOnHit` centralized in `ApplyDamageToTarget` on the base ability class — no damage site can be silent by omission anymore, structurally closing the `GA_HuntersStrike` `MulticastOnHit` deletion defect. **Enemy health bars rebuilt** — `WidgetComponent` approach replaced entirely with `DrawHUD()` canvas rendering (same system as damage numbers). Background tile + health fill + Accursed name text, centered on projected world position, auto-expire after 5s, distance-culled at 3000 units. The `WidgetComponent` (`Screen` space drift, `World` space orientation, both broken) is no longer used for rendering. **Selah name cycling** — `UGothicSelahPromptWidget` built as C++ base for `WBP_SelahPrompt`; encounter volume collects `AccursedName` from all dead enemies at completion, stores on `AGothicGameState::SelahNames` (replicated, capped at `MaxSelahNames = 6`), widget cycles names via timer with `OnNameRevealed`/`OnNameFadeOut`/`OnSelahMomentComplete` BIEs. Blueprint wiring complete. `bCanSeeTarget` permanently-true defect fixed — `CheckLeash` now queries actual perception state every 2s. `MeleeAttackRange` capsule radius defect fixed — `IsTargetInAttackRange` now surface-to-surface. Vital point randomization confirmed already done in repo (`RollNextVitalIndex`); tracker was stale. `bRoarReady` gate dropped again — second time, needs ruling. **Seventh session running where the hard architecture was correct.** Also noted from the July 19 commit (post-tracker): enemy animation system (`UGothicEnemyAnimInstance`) built, montage lifecycle refactored into `UGothicGameplayAbility` base class, `ApplyDamageToTarget` expanded with impact point and vital flag. `BP_Enemy_Draugr` renamed to `BP_Enemy_Thrall_Feral`.*

---

## Status Table Changes

### Core Loop & Level

| Item | Old Status | New Status | Updated Notes |
|---|---|---|---|
| Eagle's Landing Boss (Bestial Lucid) | 🟡 | 🟢 | **Upgraded July 20.** `BP_GA_BestialLucid_WallPound` created, Phase 2 transition completes end-to-end. `ApproachOffsetDistance` verified — she paths near pillars, not into collision. Phase 1 weighted action pool, Phase 2 zone collapse, and all four abilities (Charge, Claw, Roar, Wall Pound) confirmed functional. `bRoarReady` gate missing — see Open Defects |

### Core Combat Systems — new row

| Item | Status | Notes |
|---|---|---|
| Damage numbers (dealt damage) | 🟢 | Built July 20 via `DrawHUD()` canvas. White body hits, gold vital hits at 1.4× scale, black outline. Floats up and fades over 1.2s. `DamageAmount` threaded through `MulticastOnHit` from all five damage sites. Tunable in BP_GothicHUD |
| Enemy health bars (HUD-drawn) | 🟢 | Built July 20, replacing `WidgetComponent` approach. Background tile + health fill + Accursed name rendered via `DrawHUD()`. Registered on hit via `MulticastOnHit`, auto-expires after 5s, distance-culled at 3000 units. All tunable in BP_GothicHUD |

### Progression & Economy — update existing row

| Item | Old Notes | Updated Notes |
|---|---|---|
| Selah (currency) | Functional in engine (collection working) | Functional in engine. **Selah name cycling built July 20** — `UGothicSelahPromptWidget` base class for `WBP_SelahPrompt`, encounter volume collects dead enemies' `AccursedName` into `AGothicGameState::SelahNames` (replicated, capped at 6). Names cycle one at a time during the Selah moment with `OnNameRevealed`/`OnSelahMomentComplete` BIEs. Blueprint wired. `SkipToEnd()` exposed for hold-to-skip |

---

## Defects — Move from Open to Closed

### Close these Open rows:

| Defect | Resolution |
|---|---|
| **`BP_GA_BestialLucid_WallPound` does not exist** | **Closed July 20.** Blueprint created in `Content/Blueprints/AI/Feral/`, `AssetTags` set correctly (not `AbilityInputTag`), Cooldown GE assigned, added to `BP_Enemy_BestialLucid`'s `StartupAbilities`. PIE confirms `Granting 4 abilities`. Phase 2 transition reaches `OnPhaseAdvance`. |
| **`bCanSeeTarget` is permanently true once set** | **Closed July 20.** `CheckLeash` (2s timer) now queries `UAIPerceptionComponent::GetActorsPerception` for the target and writes the actual stimulus state to `bCanSeeTarget` every tick. Same query pattern `LogAIState` already used. |
| **`MeleeAttackRange` may not account for capsule radii** | **Closed July 20.** `IsTargetInAttackRange` now reads both characters' `GetScaledCapsuleRadius()` and compares `Distance <= MeleeAttackRange + CombinedRadius`. `MeleeAttackRange` now means "reach past her own body." |
| **The Read predicts a deterministic sequence** | **Closed — tracker was stale.** `RollNextVitalIndex()` already exists with `FMath::RandRange(0, Num - 2)` excluding the active index. The sequential `(index + 1) % Num()` referenced in this defect no longer exists in the codebase. |
| **Slicer and Hunter's Strike applied damage silently** (partial — `MulticastOnHit` deletion reopened July 18) | **Structurally closed July 20.** `MulticastOnHit` call centralized in `UGothicGameplayAbility::ApplyDamageToTarget` — every ability that routes damage through the base class inherits hit feedback automatically. No new damage site can be silent by omission. The July 19 refactor moved `GA_HuntersStrike`'s melee trace to use `ApplyDamageToTarget`, which calls `MulticastOnHit` internally. |

### New Open Defect:

| Defect | Evidence | Status |
|---|---|---|
| **`bRoarReady` gate dropped for the second time** | `grep -rn "bRoarReady" Source/` returns zero results. The tracker noted July 18: "Roar's own readiness gate (`bRoarReady`) got dropped and had to be restored once already — worth double-checking." | Open. Needs a ruling: if Roar's Cooldown GE is sufficient to gate activation timing, this property is unnecessary and can be formally retired. If `bRoarReady` served a purpose beyond cooldown (e.g. requiring a damage threshold before she can roar), it needs restoring. Check the Bestial Lucid spec for the design intent. |

### Update existing Open row:

| Defect | Updated Status |
|---|---|
| **The current vital point has no player-visible indicator** | **Partially addressed July 20.** Blueprint tick on `BP_Enemy_Thrall_Feral` positions the `VitalPointShimmer` Niagara component at `GetCurrentVitalWorldLocation()` every frame. The shimmer tracks the bone. **Remaining:** Niagara asset still needs real content (currently placeholder or empty), and wiring needs to be replicated to `BP_Enemy_BestialLucid` and `BP_Enemy_FeralRetained`. |

---

## Gate Checklist Updates

### Gate 4 (Minimum weapon/ability visual and audio feedback):
Add: **July 20: damage numbers built, enemy health bars built.** Two of three feedback channels now exist (damage dealt, enemy health state). **Remaining:** impact VFX on `OnHitFeedback` branched on `bWasVital` — the vital point system's first visual confirmation channel on the enemy. Blocked on vital point fix / The Read work (Nathan's stated next priority).

### Gate 5 (Enemy death feedback beyond ragdoll):
Add: **July 20: Accursed names now cycle during the Selah moment** via `UGothicSelahPromptWidget`. The name-reveal mechanic gives the kill a human identity before the reward appears. **Remaining:** visual death feedback on the enemy itself (death VFX beat before the Selah prompt).

### Gate 9 (HUD elements actually rendering):
Add: **July 20: enemy health bars now render via DrawHUD.** The `WidgetComponent` approach (empty component, broken in both `Screen` and `World` space) is replaced. Scale/opacity pass still pending as a solo visual-tuning task.

### Gate 10 (Mini-boss and boss end-to-end playtest):
Update: **July 20: Phase 2 transition verified working.** Wall Pound Blueprint created, `ApproachOffsetDistance` confirmed. The boss fights end-to-end through both phases for the first time. Close this gate item fully.

---

## Housekeeping additions

- **`BP_Enemy_Draugr` renamed to `BP_Enemy_Thrall_Feral`** — correctly aligns with the Accursed hierarchy naming. Fix up redirectors on the old path.
- **Health bar billboard code on `BP_Enemy_Thrall_Feral` Blueprint tick** — the `FindLookAtRotation → SetWorldRotation` chain targeting `HealthBarWidget` is now dead code; health bars render through `DrawHUD()`. Remove that half of the tick; keep the vital point shimmer positioning.
- **`HealthBarWidget` component on `AGothicEnemyBase`** — still exists as a component but no longer drives rendering. Can be removed in a cleanup pass; not urgent since it costs nothing at runtime with visibility off.
- **July 19 commit (not in tracker):** `UGothicEnemyAnimInstance` built (locomotion + combat state caching + directional hit reacts). Montage lifecycle (`PlayOptionalMontage → OnMontageHitWindow → OnMontageEnd`) refactored from `GA_HuntersStrike` into `UGothicGameplayAbility` base class — any ability can now opt into montage-driven hit windows by assigning `MontageToPlay`. `GA_BestialLucidRoar` also migrated to this pattern. `ApplyDamageToTarget` signature expanded to include `ImpactPoint` and `bWasVital`, with centralized `MulticastOnHit` call.
