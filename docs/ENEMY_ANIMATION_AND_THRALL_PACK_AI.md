# Enemy Animation Integration & Thrall Pack AI
**Vigil — Combat Systems Design Document**
*Status: Design locked on core behavior; several tuning values open. Zero engine implementation as of July 16.*

---

## Overview

This document covers two related pieces of work scoped in a single design session: (1) how enemy skeletal meshes and animation sets (starting with the Paragon Khaimera placeholder) tie into gameplay-driven behavior, and (2) a pack-hunting behavior model for ordinary Thralls, built specifically to solve "swarm bot" feel in solo Contract play. The two are covered together because the pack AI's legibility depends directly on the animation work — an attack with no telegraph and a pack that circles without visible intent both read as noise, not threat, for the same underlying reason.

**Explicitly out of scope for this document:** `BT_FeralRetained` (the Eagle's Landing mini-boss). Its behavior is a separate, already-configured system and any pack/regroup mechanics for it are a future, distinct conversation — not an extension of what's specified here.

---

## Why This Work Exists — The Solo Playtest Problem

The July 25 milestone playtest is solo. Eagle's Landing's encounter composition is not: Encounter 1 is 5 Thralls, Encounter 2's ground floor alone adds 3 more. A solo player facing that many enemies with no attack coordination and no telegraphs experiences simultaneous, unreadable damage from multiple sources — which reads as the game removing agency, not as a dangerous city. This document's priority ordering follows directly from that problem, not from general AI-polish instinct.

---

## Part 1 — Animation Integration Architecture

### The three layers (why a T-posing mesh isn't a mesh problem)

A skeletal mesh, its animation assets, and the logic that selects between them are three separate things. Khaimera ships with a skeleton and an animation set; the missing piece is always the **Animation Blueprint (ABP)** — the logic layer — which is authored per-project, not shipped with the mesh. A T-posing enemy almost always means either no Anim Class is assigned to the mesh component, or an assigned ABP is reading a skeleton it isn't getting.

### Locomotion

Standard pattern: the ABP's `EventGraph` reads `GetCharacterMovement()->Velocity.Size()` every tick and feeds a `Speed` float into a **Blend Space** (idle/walk/run), rather than a single looping cycle. Khaimera's animation set is expected to include an idle/walk/run trio suitable for this, since Paragon characters were built for it.

### Actions — Montages, notify-gated damage

One-off actions (attacks, hit reactions, death) are **Montages**, not blend space states. For enemy attacks specifically: **damage application must be gated on an `AnimNotify` at the attack's actual impact frame**, mirroring the existing pattern in `GA_HuntersStrike` (`Event.Montage.HitWindow.Open`). Applying damage the instant a Behavior Tree task fires, with no windup, removes the player's ability to read and react to an attack — this is a primary driver of "feels random" independent of any AI logic changes. Enemy attacks should be a BT `Play Montage and Wait` task, not an instant-resolve task.

### Hit reaction hook (already exists, currently unused)

`AGothicEnemyBase::BeginPlay` already subscribes a lambda to the Health attribute change delegate:

```cpp
AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
    UGothicAttributeSet::GetHealthAttribute()).AddLambda(
    [this](const FOnAttributeChangeData& Data)
    {
        // Health changed — Blueprint widget can poll this
    });
```

This is the wiring point for a `BlueprintImplementableEvent` (e.g. `OnHitReaction`) driving an additive flinch. Low cost, high value relative to other items here.

**Recommendation, not yet locked:** scope hit reactions per enemy tier rather than universally. A visible flinch suits Thrall (per the established hierarchy — "overwhelmed, fights like forgetting"). A minimal-or-absent reaction likely suits Retained/Lucid tier, where a threat that doesn't visibly register damage reads as *more* dangerous, not less. Needs a decision, not a default.

### Death

`AGothicEnemyBase::OnDeath_Implementation` already has ragdoll code commented out. Per the Tone & Sensory Bible's existing drift signal — *"Accursed movement, vocalization, and death animation should carry specificity and a residue of dignity — never generic monster-whimper"* — a pure physics-driven ragdoll is anatomically random and can't carry authored intent. Recommendation: a short authored death montage blending into ragdoll at its end, rather than ragdoll triggered directly on death.

### Multi-skeleton future-proofing (not urgent, name it now)

Khaimera is a placeholder for one archetype (Feral). An ABP built directly against his skeleton won't transfer to a second enemy type on a different rig. When that becomes real: a parent ABP that only reads gameplay state (Speed, GAS tags like `State.Attacking`/`State.Downed`) with per-skeleton child implementations or an Animation Blueprint Interface. Not needed with one enemy type in engine; worth not building anything today that would block it.

---

## Part 2 — Thrall Pack AI

### Design intent

Ordinary Encounter 1/2 Thralls get a hit-and-retreat pack behavior loop. This is explicitly a **mechanical expression of the existing "pre-echo" tell** already locked in `CHARACTER_ART_DIRECTION.md` — *"Thrall-tier tells are subtle pre-echoes of a latent Shaped direction, never full transformation."* The behavior should read as "something wolf-shaped is trying to happen here," not as the Thrall becoming Feral. (If this is ever extended to `BT_FeralRetained` specifically, tune independently — full manifestation is locked as Retained/Lucid-only, and that's a separate design pass regardless.)

This behavior **replaces** the previously-considered shared attack-slot counter rather than supplementing it — individual desynchronized retreat achieves the same "not everyone attacks at once" outcome as an emergent property of individual behavior, which is more in-character for something instinctive rather than tactically coordinated.

### Core loop

```
Seek → Approach → Attack (montage + notify-gated damage)
  → Withdraw → Reposition → Approach (repeat)
```

**Withdraw triggers on every attack resolution — landed or missed.** Tied to the montage finishing, not a damage-confirmation delegate, so there's one fewer thing that can silently fail to fire.

**Withdraw destination is not straight backward.** The enemy picks a point on an arc around the player, roughly at attack range, offset from its current angle — this doubles retreat as repositioning to a new approach vector, which is the mechanism that keeps vital-point tracking meaningful (an enemy that circles to a flank is a different read than one squared up).

**Retreat duration is randomized per-instance**, not a fixed constant — otherwise a pack resyncs and re-engages together regardless of individual retreat logic, reproducing the swarm problem one beat later.

**Approach points need minimum separation from each other**, checked and reserved against a shared registry, or a desynchronized pack can still cluster onto the same flank. Natural owner: `AGothicEncounterVolume`, since it already coordinates the group for aggro and Selah tracking.

**Open — none of the following are locked, all need a playtesting pass:**
- Exact arc offset range for Withdraw destinations (discussed informally in the 60–150° range, not confirmed)
- Exact retreat duration range (discussed informally as ~1.5–3.5s, not confirmed)
- Minimum separation distance between claimed approach points

### Pack-wide regroup ("Pack.Reevaluating")

Distinct from individual Withdraw — a rare, **synchronized** pause across the whole pack, deliberately the inverse of the desynchronized Withdraw logic. Rarity is what sells it as a decision rather than noise.

- **Trigger: reactive, on losing a pack member (a kill).** Confirmed. Gives the player agency over when the window opens — kill fast, force a regroup, get a breather — rather than pure RNG.
- **Open, not yet locked:** whether a timer backstop is needed so an encounter can't stall indefinitely if a player can't force a kill. Proposed, not confirmed.
- **During the pause:**
  - Guard pose (defensive stance, not idle) — must visibly read as "recalculating," not "AI turned off."
  - Each Thrall computes its next approach vector during the pause, using the same arc/separation logic as Withdraw, and holds it through re-engage.
  - Facing tracks the player via `AAIController::SetFocus`, not idle — enemies turn to follow the player if they reposition. This is the same fix already owed to the open perception defect on the boss (`Orient Rotation to Movement`); worth wiring generally rather than as a one-off.
- **Dependency, must not be skipped:** the existing open defect where perception can silently clear an encounter-owned combat target (90° half-angle cone, 5s stimulus memory) must not be allowed to interpret the pack pause as "lost the player." `Pack.Reevaluating` needs to be an explicit state that holds `bIsInCombat = true`, distinct in cause from a perception-driven disengagement even though the visible result (enemy not attacking) can look similar for a moment.

### Vital guarding via pose occlusion

**No new detection code required.** The guard pose physically occludes the currently-active vital using the mesh's existing `ECC_Weapon` collision — the same mechanism the July 15 trace-channel fix already relies on. A shot that hits the guarding limb resolves as a body hit (the trace stops on the arm, same as any other collision). A shot from an angle the guard doesn't cover — flanking, jumping over, catching the pose mid-recovery — reaches the vital and resolves normally through the existing `IsVitalPointHit` check, completely unmodified.

This means the "rewards the player who thinks to flank or jump" behavior is not scripted — it falls out of correct geometry, the same way the July 15 fix made honest hit detection fall out of correct geometry rather than a tuned radius.

**Scope note:** for the guard to read as intentional, the pose needs to plausibly cover whichever vital bone/socket is *currently* active — a generic "raise both arms" pose that happens to miss the active vital does nothing. Two tiers of cost here:
- **Cheap first pass:** a single generic guard pose. Will misalign with the vital some fraction of the time.
- **Full version:** vital-aware guarding, where the pose is authored or blended to track the active vital's rough body region. Real animation-authoring scope, not a quick pass. Recommended as a stretch goal once the base pack behavior is in engine and felt, not a day-one requirement.

---

## Dependencies on Existing Work

- **Perception disengagement defect** (tracker, Open Defects) — must be resolved or at minimum explicitly excluded from interfering with `Pack.Reevaluating`, per above.
- **`ActivationBlockedTags` pattern** — currently only checked per-ability (e.g. `State.Dead` in `GA_HuntersStrike`). If enemy attacks gain their own ability-driven structure, blocked-tag checks should live on the shared `GothicGameplayAbility` base rather than be repeated per ability, consistent with the standing lesson from the player-side kit.

---

## Explicitly Not In Scope Here

- `BT_FeralRetained` / mini-boss pack or regroup behavior — separate conversation.
- Multiplayer target-reconsideration logic (which player a pack focuses) — deferred; moot for a solo playtest, real value only once testing with a Kindle.
- Full vital-aware guard pose — stretch goal, not required for a first playable version of this behavior.

---

## Open Questions

- Exact Withdraw arc offset range and retreat duration range (both discussed informally, neither confirmed — needs playtesting)
- Minimum separation distance for claimed approach points
- Whether `Pack.Reevaluating` needs a timer backstop in addition to its kill-reactive trigger
- Hit reaction scoping per enemy tier (Thrall visible flinch vs. Retained+ minimal/none) — recommended above, not yet decided
- Whether "pre-echo" framing (Thrall behavior implying latent Feral direction) needs a documented visual tell beyond the pack behavior itself, or whether the behavior is the tell

---

*Document created: July 16, 2026*
*Session: Vigil Design — Enemy Animation Integration & Thrall Pack AI*
