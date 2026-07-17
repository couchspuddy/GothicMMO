# Production Status Tracker
**Vigil — Living Project State Document**
*Last updated: July 15, 2026 (night) — HUD and Selah session. Ammo renders on the HUD for the first time; the Selah prompt renders for the first time, closing gate item 6. `OnFire` converted to `GA_Fire`, a real GAS ability. **The single most consequential fix: weapon traces ran on `ECC_Pawn` and therefore stopped at the capsule — `Hit.ImpactPoint` had never touched the mesh.** That is the root cause of both the "random" vital point detection and the July 14 boss-takes-no-hitscan-damage defect, which was papered over by resizing her capsule. New `ECC_Weapon` channel; the mesh blocks it, capsules ignore it. Three dead systems deleted, six silent-failure defects closed, two new standing gotchas filed. The July 25 mini-boss half is still untouched.*

*Prior entry: July 15, 2026 (evening) — the Bestial Lucid is fightable end-to-end for the first time. Four of the six defects filed this morning are closed, one is fixed-by-redesign, one remains. Phase 2 now triggers on health rather than vital index; the vital resolves to the heart on freeze. Encounter volumes became actual volumes and now own aggro. Vital points populated on the trash enemy blueprint. The July 25 milestone — boss and mini-boss fightable end-to-end — is met on the boss side, ten days early; the mini-boss half has still never been playtested.*

*Earlier entry: July 15, 2026 (morning) — reconciled against repo after three sessions of drift (this document had not been substantively edited since July 8; the July 13 touch was the docs/ folder consolidation only). Eagle's Landing blockout and the Bestial Lucid two-phase Behavior Tree struck from the gate. Death/respawn and the multiplayer-safe Selah encounter system added. First real PIE pass on the boss surfaced six defects.*

---

## Purpose

Every other document in `/docs` answers "what is this system and why." This document answers a different question: **what is the current build status of everything that's been designed, and what order should remaining work happen in.** Design docs are largely static once locked. This document is meant to change constantly — treat it as the single place that reflects the actual current state of the project, not the intended-eventually state.

**Status legend:**
- 🟢 **Built & Functional** — exists in engine, works, playtested at least informally
- 🟡 **Partially Built** — some engine work exists but incomplete
- 🔴 **Designed Only** — fully specified in a doc, zero engine implementation
- ⚪ **Not Yet Designed** — acknowledged gap, no doc exists yet
- ⚫ **Explicitly Out of Scope** — deliberately not being pursued right now

---

## Core Loop & Level

| Item | Status | Notes |
|---|---|---|
| Full vertical slice loop (Title → Character Select → Hearth → Eagle's Landing → Boss → Hearth) | 🟢 | Functional one-way loop confirmed working |
| Hearth → Eagle's Landing re-entry | 🟡 | Known bug, non-blocking, deferred |
| Eagle's Landing Encounter 1 (Edge of Town) | 🟢 | Built and playable |
| Eagle's Landing Encounter 2 (Collapsed Building / Retained ambush) | 🟢 | Built and playable |
| Eagle's Landing Encounter 3 (Plaza, interrupted Selah) | 🟢 | Built and playable |
| Eagle's Landing full blockout (Intersection → Collapsed Building → Plaza → Approach → Boss Den) | 🟢 | All five spaces blocked out. Encounter 2 split into two encounter volumes across floors; Encounter 3 carries the multi-wave interrupted Selah |
| Eagle's Landing Mini-Boss (Feral Retained) | 🟡 | Fully configured in engine — vital points, AI Perception, dedicated BT_FeralRetained (no patrol, immediate aggressive pursuit). Not yet playtested end-to-end |
| Eagle's Landing Boss (Bestial Lucid) | 🟢 | **Fightable end-to-end as of July 15.** Reachable, aggros on den entry via encounter volume, takes hitscan and vital damage, transitions at 50% health, freezes her vital to the heart, collapses the room on a 17.5s cycle, and the collapse damages. Remaining work is tuning and one open defect (perception disengagement), not wiring |
| Death & respawn (open world) | 🟢 | Built and verified — force-leave-combat on death, enemy CombatTarget cleared, SuperMeter cached on PlayerState and restored post-respawn, HUD notified of pawn change. Corpse currently destroyed immediately (placeholder; final design defers to respawn) |
| Selah encounter system (multiplayer-safe) | 🟢 | AGothicEncounterVolume + AGothicGameState. Tracks per-encounter deaths, caches reward data at last-enemy-death, replicates a shared prompt via ActivePromptCorpse, collection via ServerCollectEncounterSelah RPC — first Server RPC in the project. Multi-wave extension via AGothicEnemySpawnPoint. **July 15: now an actual volume** — UBoxComponent root and overlap, `bAggroEnemiesOnOverlap` (EditInstanceOnly, default false) sets every enemy in the encounter onto the first player to enter. Aggro is now a level-design decision |
| Interaction prompt UI (text display on interactables) | 🟡 | Interaction functions; visible prompt text not implemented for Sean/interactables. Corpse interaction is now separate and done — see Selah collection UI |

---

## Core Combat Systems

| Item | Status | Notes |
|---|---|---|
| Vital point system (shimmer, threshold shift, damage bonus) | 🟢 | **The "randomness" is root-caused and fixed — it was never this component.** `IsVitalPointHit` compares `Hit.ImpactPoint` against a bone transform. `OnFire` traced `ECC_Pawn`, which the capsule blocks and the `CharacterMesh` profile ignores — so the impact point sat on a *cylinder surface* roughly a capsule-radius (34-42cm) from any bone, against a 30cm `HitDetectionRadius`. It registered only when the angle and `LocalOffset` happened to conspire. Fixed via the `ECC_Weapon` channel (see below). Verified: `bHit=1 Comp=CharacterMesh0 Bone=head`, `BoneIdx=143`, `Dist=20.9 Radius=30.0 Hit=1`. Both silent-failure candidates ruled out — bone names match the rig, no `INDEX_NONE`. **Still wants the feel pass, and now it can be honest:** 30cm was compensating for a systematically wrong measurement; a deliberate head shot landing 20.9cm from the vital and counting is far too generous. Tune against the shimmer's actual size |
| ADS | 🟢 | Functional, smooth FOV interpolation, movement penalty |
| Camera / locomotion | 🟢 | Fixed after extended debugging session; stable |
| Steadfast (fill, hold-to-reload conversion) | 🟢 | Fully built and confirmed working — combat-state-driven fill, tap/hold reload distinction, Steadfast-to-ammo conversion via GothicSteadfastComponent |
| Ammo & Reload system | 🟢 | Magazine/reserve tracking, tap reload from reserves, hold reload converts Steadfast. **Now renders on the HUD** — `FOnAmmoChanged` native delegate on the character → `AGothicHUD::UpdateAmmo` → `OnAmmoChanged` BIE. Deliberately built against a *notification*, not the variable, so the eventual move behind a `ServerFire` RPC + `OnRep_Ammo` will not touch the UI layer at all. Ammo stays `int32` on the pawn, not a GAS attribute — magazine ammo is per-*weapon* state and attributes are per-character singletons; it moves to a weapon instance when weapon slots exist. Reserve currently sits in Zone 2 next to the magazine, which contradicts the HUD Doctrine's Zone 3 contextual-flash rule — see Open Defects |
| Primary fire (`GA_Fire`) | 🟢 | **`OnFire()` is gone.** Was a raw input-bound C++ function entirely outside GAS: no `ActivationBlockedTags` (a dead or stunned player could fire), no cooldown, no `HasAuthority()` before `ApplyGameplayEffectSpecToTarget`, and no Server RPC — the whole chain was standalone/listen-host only. Now a `UGothicGameplayAbility` with `LocalPredicted` net policy, so cosmetics fire instantly on the owning client (`OnFireCosmetic` BIE, ready for muzzle flash) while the server alone runs the trace and damage. Ammo gates via a `CanActivateAbility` override rather than a `CostGameplayEffect` — costs require an *attribute*, and ammo deliberately isn't one. `Context.AddInstigator` added, which `OnFire` never did — should close `Killer: Unknown`, unverified. **Ammo authority is still open:** `ConsumeRound` runs on both predicting client and server with no reconciliation |
| Combat State tracking (State.InCombat) | 🟢 | GothicCombatStateComponent built, wired into damage pipeline on both dealer and receiver |
| Kill Confirmation (Event.Kill.Confirmed) | 🟢 | Wired into GothicAttributeSet, fires on confirmed kills, drives Not At All |
| Selah collection UI/prompt | 🟢 | **Renders. The most important five seconds in the game are visible for the first time.** `BP_GothicGameState` implements `OnEncounterPromptActivated`/`OnEncounterPromptCollected`, creating and removing `WBP_SelahPrompt`. Screen-space via `Get Player Controller (0)` — a knowing single-player compromise; world-space on the corpse is the design (the `PromptCorpse` pin exists for it) and is deferred. **Required a C++ fix first:** the events fired from `OnRep_ActivePromptCorpse` only, so they never fired in standalone or for a listen-host — see standing gotcha #3 |
| Death & failure states (Contract-scale) | 🔴 | Fully designed, zero implementation |
| Death & failure states (open world) | 🔴 | Fully designed, zero implementation |
| Death & failure states (raid-scale) | ⚪ | Explicitly deferred pending raid design |
| Enemy animation integration (Khaimera/Feral placeholder — locomotion, attack montages, hit reactions, death) | 🔴 | Fully specified in `ENEMY_ANIMATION_AND_THRALL_PACK_AI.md`. Zero engine implementation. Currently why enemies T-pose |
| Thrall pack AI (hit-and-retreat, synchronized regroup-on-kill, vital-guard pose) | 🔴 | Fully specified in `ENEMY_ANIMATION_AND_THRALL_PACK_AI.md`. Several tuning values (arc offset, retreat duration, min separation) explicitly left open pending playtesting. Does not apply to `BT_FeralRetained` — separate scope. Depends on the perception disengagement defect being resolved or excluded, see that doc's Dependencies section |

---

## Hunter Class

| Item | Status | Notes |
|---|---|---|
| The Slicer | 🟢 | Built and functional — projectile, centralized damage/stagger application via GA_Slicer |
| The Lunge | 🟢 | Built and wired — directional, fixed-distance, no target lock |
| The Read | 🟢 | Built, wired, rapid-activation delegate bug fixed |
| The Reckoning (Covenant) | 🟢 | Built and wired — GE_ReckoningState grants State.Reckoning; guaranteed-vital-hit wiring lives in `GA_Fire` as of July 15 night (was `OnFire`, now deleted); the ability reads `State.Reckoning` off the ASC directly rather than through the pawn, since the ASC lives on the PlayerState and the tag is its state, not the character's. Slicer and HuntersStrike confirmed as intentionally not vital-point-interactive (Slicer is a stagger tool by design; HuntersStrike is a deliberately simple, always-available light attack — no systemic integration, minimal damage, no Resonance mechanics — consistent with Draw's role in the Warden kit as "the one deliberately simple tool") |
| The Loved and The Lost (passive) | 🟡 | C++ built; Blueprint created but RampEffect blocked — Steadfast attributes now exist, but this ability's actual regen/rate targets still need definition and a real GameplayEffect built against them |
| Not At All (passive) | 🟢 | Built and wired — stun-on-kill, Reckoning duration extension confirmed functional |
| GA_HuntersStrike | 🟢 | Built earlier in project, functional. Confirmed design intent: Hunter-specific light attack, quick and minimal damage, deliberately no interaction with vital points, stagger, or Resonance mechanics — always-available low-commitment option, not a Resonance-fork-eligible ability |
| Resonance fork variants (Gone / The Close, etc.) | 🔴 | Designed as concept and framework; specific variant kits not detailed |

---

## Other Classes

| Item | Status | Notes |
|---|---|---|
| Warden — identity, creed mapping, Prior Flame expression | 🔴 | Locked in lore, full kit now designed |
| Warden — ability kit (Held Ground, I Am the Wall, Shrapnel, Draw, Hold the Line, The Wall Remains) | 🔴 | Fully designed, zero implementation |
| Penitent — identity, creed mapping, Prior Flame expression | 🔴 | Locked in lore, full kit now designed |
| Penitent — ability kit (Be At Peace, Here Now Present, Antecedent Blood Vial, Last Rites, Recede, Grief Given Voice) | 🔴 | Fully designed, zero implementation |
| Academics — faction identity | 🔴 | Barely sketched, not launching with game |

---

## Progression & Economy

| Item | Status | Notes |
|---|---|---|
| Resonant Level (earning mechanism, milestone model) | 🔴 | Fully designed, zero implementation |
| Primary stats (Resolve / Clarity / Conviction) | 🔴 | Fully designed, zero implementation |
| Secondary/itemized stats | 🔴 | Fully designed, zero implementation |
| Resonance Fork build customization (standalone system) | 🔴 | Fully designed, zero implementation |
| Selah (currency) | 🟢 | Functional in engine (collection working) |
| Pure Selah / Pilgrimage system | 🔴 | Fully designed, zero implementation |
| Gear ceiling / imbuing / loot pools | 🔴 | Fully designed, zero implementation |
| Resonance Lottery (loot distribution) | 🔴 | Fully designed, zero implementation |
| Resonance Strain / gear sunset-reintroduction lifecycle | 🔴 | Fully designed, zero implementation |
| Live balance / telemetry philosophy | 🔴 | Fully designed as philosophy; no actual telemetry infrastructure exists |
| Sean the Binder — Selah exchange interaction | 🔴 | Placeholder NPC exists in Hearth; no functional exchange built |

---

## Party, Social & Multiplayer Structure

| Item | Status | Notes |
|---|---|---|
| Party/Kindle formation model (3-tier: open / premade / none) | 🔴 | Intent locked, zero implementation, deep design deferred to raid design phase |
| LFG / high-level content finder tool | ⚪ | Design intent noted; no detailed design yet |
| Group-scale Steadfast (per-player confirmed) | 🔴 | Design decision locked; depends on Steadfast implementation |
| Social/Hold (guild-equivalent) system | 🔴 | Designed — HOLDS_AND_SOCIAL_SYSTEMS.md. Zero implementation, correctly deferred |
| Solo activity | ⚪ | Not yet designed |
| PvP | ⚫ | Explicitly out of scope, no current plans |

---

## Art & Audio Direction

| Item | Status | Notes |
|---|---|---|
| Tone & Sensory Bible (cross-discipline governing document) | 🟢 | Locked as living document; new pairs expected as other disciplines are addressed |
| Environment art direction — Material Logic (Layer 1) | 🟢 | Fully locked as doctrine |
| Environment art direction — Spatial Logic (Layer 2) | 🟢 | Fully locked as doctrine |
| Environment art direction — Surface Treatment (Layer 3: color, lighting, texture) | ⚪ | Deferred, needs reference-image working session; should be revisited now that Tone & Sensory Bible exists above it |
| Character/enemy art direction | 🟢 | Locked — CHARACTER_ART_DIRECTION.md. Tier-based posture-as-control-loss, silhouette-first identification across all tiers, Shaped silhouettes derived from inversions, Thrall-tier pre-echo tells |
| VFX language (vital point shimmer, Selah, Prior Flame effects) | ⚪ | Dependent on Layer 3 environment work. Note: minimum placeholder hit/death VFX is a *gate* item (see checklist 4-5) and does not wait on this |
| Musical direction | 🟢 | Locked — MUSIC_DIRECTION.md. Zone-based not combat-reactive; Hearth the only major-key cue; staged content follows escalate-and-reset mirroring Selah's checkpoint structure |
| Voice acting direction | ⚪ | Not yet designed |
| Sound design doctrine | 🟢 | Locked — SOUND_DESIGN_DOCTRINE.md. Ambient never rests except during Selah; combat information as a real channel; binary-signal vital hit audio; Shaped signatures derived from each archetype's inversion |
| HUD design doctrine | 🟢 | Locked — HUD_DESIGN_DOCTRINE.md + UI_UX_DOCTRINE.md. Zone map from screen geometry, decision-speed hierarchy, horizontal peripheral reserved for contextual-only, fixed-location variable-prominence |

## Narrative & World

| Item | Status | Notes |
|---|---|---|
| Full cosmology (Lethe, Pantheon, Ember Court, Accord, Bleed, Prior Flame, Selah) | 🟢 | Fully locked in Narrative Bible |
| Accursed hierarchy (Thrall/Retained/Lucid/Hollow + Shaped archetypes) | 🟢 | Fully locked |
| The Weight (primary BBEG) | 🟢 | Fully locked, full arc designed |
| Three-act story structure | 🟢 | Fully locked at outline level |
| Eagle's Landing narrative framing (Philadelphia/City Hall) | 🟢 | Fully locked |
| Cathedral of Chains raid — encounter concepts | 🔴 | Conceptually designed in early lore sessions, not connected to current raid-token/failure-state work |

---

## Noted Future Task — Art Direction Consolidation

**Not yet built.** As of this note, Vigil's visual doctrine exists across multiple separate documents (Environment Art Direction, Tone & Sensory Bible, plus menu/title-screen direction discussed but not yet written up). This is correct for internal design use — each document is detailed and cross-referenced for good reason — but is not the right format to hand an artist unfamiliar with the project.

**When onboarding actual art collaborators**, a single, consolidated visual brief should be built first — shorter, front-loaded, absorbable in ~10 minutes, pointing to the fuller docs for anyone who wants the complete reasoning behind a given rule. This is not a replacement for the detailed doctrine, just a front door into it.

**Not urgent — flagged for whenever actual artist conversations become imminent, not before.**

---

## Known Defects — Found in Engine

New section as of July 15. These are distinct from the 🔴/🟡 rows above: those are *things not built yet*, these are *things built that don't work*. The distinction matters because implementation debt is predictable and defects are not — everything below was found by actually running the game, and none of it was visible from the code alone.

Six were filed the morning of July 15 from the first PIE pass that reached the Bestial Lucid. By that evening four were closed, one was made moot by a design change, and one remains. One new defect was found in the process. Resolutions are kept rather than deleted — the causes were non-obvious enough to be worth not rediscovering.

The night of July 15 added six more closures and three new open items. **Every single one was a silent failure** — no compile error, no warning, and in four cases a function that looked implemented and had zero callers or an empty Blueprint body. None were the hard systems. That is now the third session in a row where the difficult architecture was correct and the bugs were a capsule, an array, an unassigned field, and a notification nobody was listening to.

### Open

| Defect | Evidence | Status |
|---|---|---|
| **Perception can clear an encounter-driven combat target** | `CombatState: BP_Enemy_BestialLucid_C_1 left combat` mid-fight, no `Leash broken` line above it. Boss then stops, so she never turns, so she never re-acquires — a deadlock. | Root-caused, unfixed. `PeripheralVisionAngleDegrees = 90.f` (a *half*-angle → 180° cone) plus `MaxAge = 5.f`: slip behind her, the stimulus expires, perception drops the target. Perception is both the input and the thing being starved. **Ignition was fixed via encounter volume aggro; disengagement was not** — perception is still allowed to end a fight it no longer owns. Two fixes wanted: a guard so perception cannot clear a target the encounter volume set, and `Orient Rotation to Movement` on her so facing tracks pursuit. The second papers over the symptom; the first is correct and is what survives an 8-Kindle where someone is always outside her cone. |
| **`MeleeAttackRange` may not account for capsule radii** | `IsTargetInAttackRange()` is `FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation()) <= MeleeAttackRange` — centre-to-centre, default 200cm. | Still untested. Her capsule was corrected upward July 14, so her centre cannot approach the player's closer than the sum of both radii. She appears to be landing melee, which makes this moot in practice for now — but the check is wrong in principle and will bite the next enemy whose capsule changes. Surface-to-surface (`<= MeleeAttackRange + CombinedRadius`) is the fix; `MeleeAttackRange` would then mean "reach past her own body," which is the thing anyone would actually tune. |
| **Navmesh serialized size mismatch** | `Recreating dtNavMesh instance … maxTiles (serialized: 75, 7 bits) vs calculated required (29403, 15 bits)` + `Unable to find RecastNavMesh instance while trying to create UCrowdManager instance`, every load. | Coverage of the boss den verified manually; regenerates at load, so non-blocking. But the nav data on disk was built for a level a fraction of the current size. Rebuild and re-save. Was briefly suspected as the cause of the boss rapid-cycle and exonerated — `Path following: Idle` in the Gameplay Debugger proved no move was ever requested. |
| **Skipping an encounter permanently orphans its Selah** | `AGothicGameState::ActivePromptCorpse` is a single global slot. Clear encounter 1, walk away without collecting, clear encounter 2 → `SetActivePromptCorpse(B)` overwrites A, and `CompleteCollection` bails on `GetActivePromptCorpse() != LastEnemyToDie`. | Design gap, not a bug — found by tracing the BP, never observed. Walk back to corpse A, press E, silent no-op; the reward is gone for the session. Also leaks a prompt widget, since no `Collected` event ever fires for A (a defensive teardown at the top of the Activated path fixes the widget half, not the Selah half). Eagle's Landing is linear enough that you'd have to deliberately skip a reward. Real fix is a volume→corpse map or per-encounter prompts. Not slice scope. |
| **`HitDetectionRadius` is 30cm and untuned** | `Dist=20.9 Radius=30.0 Hit=1` on a deliberate head shot. | New as of July 15 night, and only a defect *now* — 30cm was silently compensating for the capsule-trace measurement error. With an honest impact point it is most of a torso. The property's own comment says "the shimmer visual should match this radius so the feedback is honest"; that was impossible until tonight. This is gate item 10's tuning work. |
| **Reserve ammo contradicts the HUD Doctrine** | Built into Zone 2 next to the magazine count. HUD Doctrine names ammo reserve *by name* as the Zone 3 example: contextual flash on reload, nothing persistent in horizontal peripheral space. | Decision-by-implementation, not an oversight — the argument for Zone 2 is that Steadfast makes reserve a *managed* resource, and you decide whether to burn 50 Steadfast for 12 rounds *before* reloading, which is exactly when a reload-triggered flash hasn't fired. The doctrine predates Steadfast's tier logic. Either amend the doctrine or move the element; do not leave them disagreeing. |

### Closed

| Defect | Cause | Resolution |
|---|---|---|
| **Zero vital points on every Draugr** | Component present, `VitalPointLocations` array empty in Blueprint. | **Fixed July 15.** The property is `EditDefaultsOnly` and there is one trash-enemy blueprint — 18 warnings were 18 *instances* of one asset, so one edit closed all of them. Filed as the highest-priority item on the board and took five minutes. |
| **Boss aggro depends on her facing** | 180° cone + a den boss correctly having no patrol branch = perception was the only ignition, so half the den was a dead zone. | **Fixed July 15** via `bAggroEnemiesOnOverlap` on `AGothicEncounterVolume`. Log confirms `aggroed 1/1 enemies` on den entry. Note this fixed ignition only — see the open perception defect above. |
| **No destructible zones exist in the level** | C++ wired and correct; no actor carried the `BestialLucidZone` actor tag. `GetAllActorsWithTag` found nothing. | **Fixed July 15.** Three debris actors tagged. Then immediately exposed the next layer: `TriggerZoneCollapse` was an empty `BlueprintImplementableEvent`, so the log read healthy while nothing happened on screen. `BP_DestructibleZone` + `GE_ZoneCollapseDamage` built to give it a body. |
| **Phase 2 has never fired** | Blocked behind boss aggro; trigger mechanism also wrong (see design change below). | **Fixed July 15.** Fires reliably on the health threshold. |
| **Weapon traces hit the capsule, never the mesh** | `OnFire` traced `ECC_Pawn`. The capsule blocks it; the `CharacterMesh` profile ignores it. `Hit.ImpactPoint` had therefore never touched the body in the life of this project. `Bone=None` on every hit — capsules have no bones. | **Fixed July 15 night.** New `Weapon` trace channel (`ECC_GameTraceChannel1` = 14, Default Response **Ignore**, `ECC_Weapon` defined in `GothicMMO.h`); enemy mesh blocks it, capsules ignore it by default. **This is also the true cause of the July 14 "boss took no hitscan damage" defect**, which was closed by resizing her capsule to approximate her mesh — a fix to the symptom. Capsule size no longer matters for shooting, so that class of bug cannot recur on the next enemy. Melee (`OnMelee`, `GA_HuntersStrike`) deliberately stays on `ECC_Pawn` — a generous capsule hitbox is correct for melee. |
| **Blueprint collision override silently beat the C++ constructor** | `GetMesh()->SetCollisionResponseToChannel(ECC_Weapon, ECR_Block)` was correct in `AGothicEnemyBase`'s constructor and the trace still hit nothing. | **Fixed July 15 night.** `BP_Enemy_Draugr` had a saved override on the Mesh component's collision with `Weapon = Ignore`. Blueprints serialize component properties, so the BP's saved data overwrites the constructor at spawn. Silent. **Cost ~40 minutes of reading source that was correct the whole time.** Lesson: for collision, check the live component's response grid on a PIE instance, not the constructor — source cannot tell you what a Blueprint override did. |
| **`GrantedEffects` re-applied on every respawn** | `GiveToAbilitySystem`'s abilities loop had an idempotency guard; the `GrantedEffects` loop did not. Player ASC lives on the PlayerState and survives death; the pawn does not. Every respawn re-ran the grant against an ASC already carrying everything. | **Fixed July 15 night.** Guard added via `FGameplayEffectQuery::EffectDefinition` against `GetActiveEffects`. Verified — `showdebug abilitysystem` identical after respawn, skip lines logged. `bAbilitiesGranted` on the pawn retired as genuinely redundant: it never protected this path anyway, because the pawn is the thing being replaced. **Only works for Duration/Infinite effects** — Instant effects never enter `ActiveGameplayEffects` and cannot be guarded post-hoc, so the loop now warns loudly if it finds one (an Instant GE in a "granted" list is a category error). |
| **`AGothicHUD::UpdateSelah` had zero callers** | Selah was awarding correctly and the HUD never heard about it. `UpdateSelah` was an orphan function. | **Fixed July 15 night.** The Selah attribute-change delegate existed in June and was lost in a rewrite of `InitGASFromPlayerState` (the SuperMeter-restore or ability-set work). Restored. **This also exonerates the open `GetMagnitude called for Data None` suspicion on the GE_SelahGain path** — the attribute moves correctly; that error is stale or belongs to a different call site. |
| **Attribute lambdas held pointers to destroyed pawns** | `AddLambda` captured the *pawn's* `this` and registered on the *PlayerState's* ASC. Pawn dies, ASC survives by design, lambda stays bound holding garbage. | **Fixed July 15 night.** All four (Health, SuperMeter, Selah, Ammo) moved to `AddWeakLambda(this, ...)`, which unbinds on the bound UObject's destruction. Same boundary as the `StaleComponentRef` crosshair crash — see standing gotcha #4. Per-lambda `IsLocallyControlled()` early-returns collapsed into one `bAttributeDelegatesBound` guard, closing a latent double-bind on the two-pass init. |
| **The Collection Rite was unreachable dead code in both directions** | `[BP_Enemy_Draugr_C_4] Collection Rite Started` followed by `SetTimer passed a bad function (OnCollectionComplete) or object (None)`. | **Deleted July 15 night.** The BP called `ServerCollectEncounterSelah` *first*, which ran `CompleteCollection` synchronously — awarding, then destroying every encounter corpse **including the actor whose graph was mid-execution**. The VM kept going on a pending-kill actor and handed `SetTimer` a delegate bound to `self`, now None. Worse, the rite's payload was `CollectAllNearbyCorpses`, which skips any enemy with an `OwningEncounter` by design — so it could never do work even on a surviving actor; and the non-encounter branch it was written for had an unwired `else` pin. Removed with it: `AwardSelahToNearbyEmbers` (~80 lines, zero callers), `CollectAllNearbyCorpses`, `bCollected`, `bCollecting`, `OnCollectionInterrupted`, `CancelCollectionRite` (a stub that only logged). **Consequence to know:** an enemy with no `OwningEncounter` now has no Selah path at all. Already effectively true; there is now no code hinting the question exists. |

**Also fixed July 14, recorded so the cause isn't rediscovered:** the boss took no hitscan damage while melee and Slicer worked normally. Cause was her capsule not scaling with her mesh — `OnFire`'s `LineTraceSingleByChannel(..., ECC_Pawn, ...)` was passing through empty space where the visible body was, while melee and Slicer don't use that trace and were unaffected. Capsule corrected; vital hits now land.

**Worth noting about all of the above:** none of it was the Behavior Tree, which the gate list called the hard part. It was a capsule that didn't match its mesh, a vision cone with no patrol fallback, an `EditInstanceOnly` property that couldn't be found on Class Defaults, an actor origin, and a GameplayEffect that didn't exist. The BT was correct the entire time.

---

## Design Change — Phase 2 Trigger: Vital Index → Health Threshold

Decided and implemented July 15. Recorded because the reasoning matters more than the change.

**Was:** `Phase2TriggerVitalIndex = 2` — Phase 2 fired when the vital point shifted to a designated index.

**The problem:** vital shifts are driven by *two* things — an accumulated-damage threshold (`ShiftThreshold = 100.f`) and an independent looping timer (`bShiftOnTimer`, `ShiftTimerInterval = 5.f`) — and the index wraps modulo the array length. With the timer on and three vitals, index 2 was reached roughly 10 seconds after possess **whether or not the player had touched her.** It was a stopwatch. With the timer off it degraded to a flat 200 cumulative damage: better, but absolute, so it silently drifted every time her health was retuned, and it required the array to have at least three entries or Phase 2 could never fire at all.

**Now:** `Phase2HealthThreshold = 0.5f`, bound to the ASC's Health attribute change delegate. A *fraction* rather than a flat value, so it survives health retuning and scales when she's balanced for a full Kindle.

**What was lost:** nothing. The old trigger appeared to couple the phase to the vital point mechanic, but the coupling was invisible — no player counts vital shifts. The mechanic and the phase were never actually connected in a way anyone could perceive.

**What Phase 2 keeps:** the vital *freeze*, which is the real inversion. Phase 1 asks the player to track a moving target. Phase 2 stops hiding, resolves the vital to the heart, and makes the room the problem instead — the mechanic gets *easier* exactly when the fight gets harder. `HandleVitalPointShifted` remains bound but no longer owns the phase; a Phase 1 reaction to her own vital shifting is a plausible beat to want later.

---

## Housekeeping — Small, Known, Not Worth a Defect Row

Collected July 15 night so they stop being rediscovered. None block anything.

- **4× `GetWorld()->GetFirstPlayerController()` in `GothicPlayerCharacter.cpp`** (lines ~90, 199, 222, 319 — BeginPlay timer, Health/SuperMeter lambdas, Tick cooldown poll). `GetLocalGothicHUD()` exists and is used in exactly one of five places. On a listen server this returns the *host's* controller regardless of who is asking, so a second player's character would push its values to the host's HUD. Invisible in standalone; guaranteed weirdness the first time two clients run.
- **Patch-instruction comments pasted into `GothicEncounterVolume.cpp`** at lines ~145, 158, 160, 186 (`// replace CompleteCollection entirely`, `// ...existing reward/checkpoint/corpse-cleanup code continues unchanged below...`). Harmless; they read as instructions to a future editor.
- **Stale comment** at `GothicEnemyBase.h:75` referencing the now-deleted `CollectAllNearbyCorpses`.
- **ObjectRedirector** at `Content/Blueprints/Game/BP_GothicGameMode.uasset` (2,420 bytes — the real asset is the 22KB one in `Game/GameModes/`). Left behind by an editor move; hidden in the Content Browser by default. Right-click the folder → Fix Up Redirectors.
- **Debug logs to strip**, now a real pile: `>>> AbilityInputTagPressed`, `GA_Fire Activate`, `GA_Fire: PerformFireTrace entered`, `GA_Fire trace:`, `VitalCheck:`, `UpdateAmmo:`, `Selah changed:`, plus the `bDebugDrawVital` sphere and the `DrawDebugLine` in `PerformFireTrace`. The draw-debug ones are worth keeping behind their flags; the raw `UE_LOG`s are not.
- **`Tick`'s cooldown totals are still literals** (`4.0f`, `8.0f`, `3.5f`). Correct against the current GEs — that's the July 10 fix — but they will drift again on the next tuning pass. See standing gotcha #6 in the general conventions.

---

## Vertical Slice Gate Checklist — What Actually Must Be True to Call This Done

Re-scoped after a full-project inventory surfaced significant confusion between "gating requirement" and "valuable future work." This section is the single, unambiguous answer to "what's left." Everything else in this document is real and correctly documented, but does **not** block the slice.

**Standing gotcha #2 — FName lookups fail silently, everywhere.** Added July 15 after it cost most of a session. `Blackboard->SetValueAsInt(FName, ...)` and its siblings return void and no-op silently when the key doesn't exist on the asset, or exists under a different type. No compile error, no log warning, and the symptom is indistinguishable from a logic bug — a boss stuck in Phase 0 forever. This bit `CurrentPhase` on BB_BestialLucid and cost a full debugging pass. The same exposure exists for every name in the `GothicBBKeys` namespace (`TargetActor`, `TargetLocation`, `bCanSeeTarget`, `bIsInCombat`, `PatrolOrigin`, `AttackRange`), which are C++-side promises that no blackboard asset is obligated to keep — currently honoured by BB_Enemy and BB_BestialLucid, but nothing enforces it for the next blackboard. **The real fix, not yet built:** an `ensure` or `checkf` on the key IDs at `OnPossess`, so a missing key fails loudly at possess time on every enemy rather than silently at runtime on one. Until then, log `GetKeyID` (255 = key does not exist) whenever a blackboard write appears not to land.

**Standing gotcha #3 — OnRep never fires on the authority.** Added July 15 night after it silently disabled the Selah prompt for the only player who exists. `ReplicatedUsing = OnRep_X` fires **only on clients receiving replicated data** — never on the machine that authored the change, and never at all in standalone PIE, where no replication occurs. So a server-side `GS->ActivePromptCorpse = X; GS->ForceNetUpdate();` fired `OnEncounterPromptActivated` on remote clients only, and on nobody in the NetMode this project is actually tested in. **The fix pattern, now used by `SetActivePromptCorpse`:** a server-side setter that assigns, `ForceNetUpdate()`s, and then calls `OnRep_X()` by hand. The property was moved to `protected` with a `BlueprintPure` getter so direct assignment no longer compiles — the difference between fixing it and preventing it. **The same exposure exists on `UGothicVitalPointComponent::ActiveVitalIndex`**, which is why `ShiftVitalPoint` and `FreezeVitalPoint` both broadcast manually after assigning; any future replicated property with an OnRep needs the same treatment.

**Standing gotcha #4 — state that outlives the pawn cannot hold pawn pointers.** Added July 15 night, having now bitten twice. The player's ASC and AttributeSet live on the **PlayerState** by design, so they survive death, respawn, and possession changes. The pawn does not. Anything registered on that persistent state while capturing pawn-scoped context is holding a pointer to a corpse the moment the player dies. Two instances: the `StaleComponentRef` crosshair crash (`SteadfastComponentRef` going invalid after pawn destruction — fixed with `OnOwningPawnChanged`), and four attribute-change lambdas registered with `AddLambda` capturing the pawn's `this` (fixed with `AddWeakLambda`). The same logic is why `bAbilitiesGranted` never guarded the respawn grant path, and why `GA_Fire` reads `State.Reckoning` off the ASC rather than through the character. **Rule: if it lives on the PlayerState, ask the PlayerState — don't route through the pawn, and don't let a pawn-scoped bool answer a question about ASC state.**

**Standing gotcha, worth checking on every ability going forward:** discovered during tonight's cooldown debugging — the ability set was granting the raw C++ class (`GA_Read`) instead of the configured Blueprint child (`BP_GA_Read`), meaning every Blueprint-side value (cooldown effect, damage numbers, everything) was silently never reaching the running instance despite looking correctly configured in the editor. Worth explicitly re-confirming every ability in the ability set is granting its `BP_GA_X` variant, not the bare C++ class, since this exact mistake could easily have been made more than once and would produce no compile error and no obvious symptom beyond "my Blueprint changes don't seem to do anything."

### Required — the slice is not done until every item below is true

1. ~~**Eagle's Landing map rework**~~ — **DONE.** All five spaces blocked out to the locked encounter design: Intersection, two-floor Collapsed Building (split into two encounter volumes), Plaza with multi-wave interrupted Selah, Approach, Boss Den. The space the encounters were designed against now exists.
2. ~~**Bestial Lucid two-phase Behavior Tree**~~ — **DONE, and now proven.** BT_BestialLucid with phase-gated Claw/Charge/Roar priority selection, three GAS abilities, and tagged destructible zones wired to Phase 2 zone collapse. **Phase 2 fired in engine July 15** — health crosses 50%, vital freezes to the heart, ceiling collapse cycles on 17.5s and damages. The July 14 caveat on this item ("do not read this strike as 'the two-phase fight works'") is retired; it works. Trigger mechanism was changed from vital index to health threshold in the process — see the Design Change section above.
3. ~~**Guaranteed-vital-hit wiring for The Reckoning**~~ — **DONE.** `IsReckoningActive()` added to `GothicPlayerCharacter`, wired into `OnFire`'s vital hit check via `IsReckoningActive() || VitalPoint->IsVitalPointHit(...)`. Confirmed during design review that Slicer and Hunter's Strike do not check vital points at all, by design — Slicer is a stagger tool, not a precision tool; Hunter's Strike is a deliberately simple, always-available light attack with no systemic integration (minimal damage, no vital/stagger/Resonance interaction, consistent with Draw's role as "the deliberately simple tool" in the Warden kit) — so `OnFire` was the only place this wiring was actually needed. Scope was correctly narrowed rather than force-fit onto abilities that don't use vital detection.
4. **Minimum weapon/ability visual and audio feedback** — confirmed currently missing or unconfirmed: muzzle flash, projectile/tracer visibility, hit impact feedback. **July 15 night: the hook now exists.** `GA_Fire::OnFireCosmetic` is a `BlueprintImplementableEvent` firing on the owning client the instant the ability activates, before any server confirmation — muzzle flash, camera kick, and gunshot go there. `AGothicEnemyBase::MulticastOnHit(ImpactLocation, bWasVital)` already exists and is now actually reached on a remote client's shot, which it never was from `OnFire`. **Sharpened by tonight:** the `VITAL HIT` on-screen debug lived in `OnFire` and was lost with it, and ~10 minutes were spent believing the vital system was broken when only its tell was missing. If the developer cannot tell whether the core mechanic fired, a player has no chance — this item is the mechanic being observable, not a polish pass. A shooter with no visible feedback on firing is not a playable slice regardless of how correct the underlying damage math is. Placeholder-quality VFX/SFX is acceptable; total absence is not.
5. **Enemy death feedback beyond ragdoll** — confirmed currently ragdoll-only. At minimum needs some visual marker (blood, a death VFX beat) before the Selah prompt appears, so the kill itself registers before the reward does.
6. ~~**Selah collection UI**~~ — **DONE.** The prompt renders. `BP_GothicGameState` implements `OnEncounterPromptActivated`/`OnEncounterPromptCollected`, creating and removing `WBP_SelahPrompt`. Required a C++ fix first that the "purely a widget task" framing had missed entirely — the events fired only from `OnRep_ActivePromptCorpse`, which never fires on the authority, so in standalone they fired for nobody (standing gotcha #3). The loop finally has a visible collection moment. Remaining and non-gating: the defensive teardown at the top of the Activated path, the single-slot orphan problem (see Open Defects), and world-space placement on the corpse, which is the design and is deferred.
7. **Death/failure state implementation (Contract-scale)** — fully designed, zero implementation.
8. ~~**Steadfast tier logic (even single-weapon)**~~ — **DONE.** `HoldReload()` rewritten to select the highest tier the player currently has enough Steadfast for (10/30/60 placeholder thresholds, tune to feel), replacing the old hardcoded flat 20/6 values. Matches the "chunked segments" reticle design — whatever tier is lit is what conversion yields right now.
9. ~~**HUD elements actually rendering per the UI/UX Doctrine**~~ — **Largely done, one item remains.** Direct in-engine check performed. Findings and fixes:
   - Health number display was bound to the normalized 0-1 fill value instead of raw Health/MaxHealth (showing values like ".48" instead of "48/100") — **fixed**, text now has its own binding to raw attribute values, bar fill percentage binding left untouched.
   - Steadfast reticle display did not exist — **built.** Consolidated multiple stray/duplicate reticle widgets down to one (`WBP_Crosshair_Pistol`, embedded as a child inside the main persistent HUD widget rather than as a separate top-level widget), added 3 chunked segment images bound to `SteadfastComponent::GetCurrentSteadfast()`/`GetMaxSteadfast()`, thresholds matching the 10/30/60 tier breakpoints above.
   - Ability cooldown bars were not correctly mapped to ability slots — root cause was a chain of two bugs: (1) the `OnAbilityCooldownChanged` delegate and `UpdateAbilityCooldown` were passing a raw `int32` slot index rather than the `EGothicAbilitySlot` enum, causing an off-by-two-plus misalignment once Passive1/Passive2 were added to the enum ahead of the numbered ability slots; (2) separately, the ability set was granting the raw C++ `GA_Read` class rather than the configured `BP_GA_Read` Blueprint, meaning Blueprint-side cooldown configuration was silently never reaching the running ability instance. Both fixed — cooldown delegate chain now passes the enum end-to-end, `Switch on EGothicAbilitySlot` used in place of `Switch on Int`, ability set corrected to grant Blueprint variants.
   - **July 15 night — ammo added.** Magazine and reserve now render, via a notification boundary rather than a property binding (see Ammo & Reload above). Selah counter chain restored and confirmed firing (`GothicHUD: UpdateSelah called`); the counter's own TextBlock belongs in **Zone 4**, per the doctrine's rule that corners carry information supporting slower-paced decisions — Selah is spent at the Binder, never mid-fight, and the doctrine notes Zone 4 is currently underused because nothing generated corner-tier information. Selah is its first real resident.
   - **Remaining:** scale/opacity pass (HUD reads too small/transparent even at full screen) — explicitly left as a solo visual-tuning task, not requiring further design input. **Now covers three more elements than it did this morning**; doing it once, with ammo in place, is cheaper than doing it twice.
   - **Still missing entirely, all non-gating:** enemy health bar (the `HealthBarWidget` component exists on `AGothicEnemyBase` with no Widget Class assigned — an empty component; also `EWidgetSpace::World`, so it won't face the camera, and its visibility is driven by `OnPerceptionUpdated`, meaning it appears when the *enemy* notices *you*). Passive state display for The Loved and The Lost's ramp and Not At All — these are the Zone 2 buff/debuff cluster's first real residents, contradicting the doctrine's assumption that Warden/Penitent would supply them; `CurrentRampTime` is protected and unexposed, and the GAS-native channel is a state tag the HUD reads. Zone 1 reticle hit confirmation.
10. **Mini-boss and boss end-to-end playtest** — **half done.** The **boss** is fightable end-to-end as of July 15: reachable, aggros on den entry, takes hitscan and vital damage, transitions at 50%, freezes the vital to the heart, collapses the room, and the collapse damages. The **mini-boss** (`BT_FeralRetained`) has still never been playtested. It inherits the aggro fix and the capsule lesson but nothing else automatically, and the July 25 milestone names both.
11. **Playtest with a person who isn't you** — the actual finish line.

### Explicitly Not Gating — valuable, correctly deferred, does not block the slice

- **Encounter reset (distinct from the spatial leash)** — identified July 15, correctly deferred. The existing `LeashRange` mechanic answers "the enemy wandered too far from home, reset it," which protects the open world from wandering trash and is *wrong for a den boss* — she can't leave the rotunda, so it guards against a case that can't happen. The real MMO problem is different: eight Antecedents pull her, seven die, one runs — she's still standing in her den at 30% health, so distance-based leashing never fires and the next Kindle inherits a half-won fight. A real encounter reset triggers on "no living Antecedent inside the volume for N seconds" (which `AGothicEncounterVolume` is now positioned to answer, having a TriggerBox as of July 15) and *restores* rather than clears: health to max, phase to 1, vital unfrozen, collapse timer killed, zones restored, walk back to origin. Note two things built during the slice are one-way and will need undoing: `FreezeVitalPoint` is explicitly documented as non-reversible, and `bAggroTriggered` latches permanently. Both are correct for a slice; both are what encounter reset has to reverse. Not slice scope.
- **Weapon slot expansion (Primary/Secondary/Heavy as three distinct weapons)** — Steadfast's design doesn't require three weapons to exist to be internally coherent, only for the tier *contrast* to be demonstrable. One weapon with correct tier logic (item 8 above) satisfies the slice; a second and third weapon are a stretch goal, not a requirement, unless the explicit goal of the demo becomes "show weapon variety" specifically.
- Warden and Penitent implementation
- Full sound design doctrine (beyond the minimum feedback in item 4)
- Menu doctrine beyond the title screen
- Solo activity, Holds, economy, progression implementation
- Everything else in this document not listed under Required above

---

## Prior Priority Notes (Superseded by Checklist Above, Retained for Context)

All three launch classes (Hunter, Warden, Penitent) are fully designed at the kit level. The Hunter kit is now essentially complete in engine (6 of 6 abilities built, 1 partially blocked on The Loved and The Lost's RampEffect, itself non-gating since it's a passive refinement rather than a core-loop requirement). Warden and Penitent implementation should follow the same proven pattern — C++ ability classes staged ahead of engine sessions, Blueprint wiring done in-session — once the slice gate above is cleared. Economy implementation, progression systems, and raid design remain **correctly deferred** — not neglected, just genuinely lower priority than closing the loop between "fully designed" and "fully playable" for what already exists.

---

## How to Use This Document

Update the status table whenever something moves categories — that's the entire job of this document. It is not meant to be exhaustively rewritten each session; it's meant to be a quick, honest snapshot you or anyone else can glance at and immediately understand: what's real right now, versus what's well-designed but still theoretical.

If a feeling of "are we missing something foundational" comes up again, check here first before opening a new design conversation. If everything relevant to the question already has a row in this table with a status, the feeling is very likely about the 🔴/🟡 gap (implementation debt) rather than a genuine ⚪ gap (missing design). Only ⚪ rows represent real unaddressed design gaps.

---

*Document created: July 2026*
*Session: Vigil Design — Production Status & Foundations*
