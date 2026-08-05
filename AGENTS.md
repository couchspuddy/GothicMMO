# AGENTS.md — GothicMMO / Vigil (source-lane contributor rules)

You are contributing to **Vigil**, a UE5 (5.8) action game. You work the **source lane only**: C++ under `Source/GothicMMO/` and Python under `Plugins/VigDevTools/Content/Python/`. Another agent pipeline owns the Unreal editor, all `Content/` assets, builds, and runtime verification. Stay in your lane and the partnership works.

## Hard rules (each one has burned this project)

1. **Never touch `Content/`** — assets are edited through a live-editor pipeline you don't have. Never commit, revert, or reformat anything under it. `Content/Blueprints/Game/Levels/L_EaglesLanding.umap` is often dirty in the working tree with the maintainer's uncommitted work: never stage it, never stash, **never `git add -A` or `git add .`** — stage named files only.
2. **Never push to `main`. Never merge PRs.** All work ships as a **draft PR from a feature branch**. The maintainer reviews and merges personally (standing policy, 2026-08-05). Your PR body must state the mechanism, every interpretation you made, and what you could NOT verify.
3. **Do not build.** The project's build pipeline compiles every PR (editor-open state on this machine makes ad-hoc builds hazardous). Write code that compiles by care, and say plainly in the PR that it is unbuilt.
4. **Declare untested runtime behavior.** You cannot run PIE or drive the editor. Anything whose correctness depends on runtime (replication timing, GAS activation, AI behavior) gets a "needs PIE verification: <specific check>" line in the PR body — the verification pipeline picks those up.
5. Never read, echo, or commit credentials, tokens, or secrets.

## Engine/UHT traps (verified the hard way)

- An override of a parent `UFUNCTION` must NOT re-declare `UFUNCTION()` — UHT rejects it.
- `UFUNCTION`s can't take `const TCHAR*` params — use an overload pair (C++-only overload + UFUNCTION forwarder).
- `mutable` is rejected on UPROPERTYs; const getters that lazy-cache use `const_cast` (existing precedent in `GothicSteadfastComponent`).
- The player ASC lives on the **PlayerState and outlives the pawn** — any loose gameplay tag or cached state you add must be cleared on death/respawn or it haunts the next life (see `State.Sprinting`/`State.Dead` handling in `GothicPlayerCharacter.cpp`).
- Components must **lazy-resolve** the ASC (BeginPlay precedes possession on respawned pawns) — pattern: `ResolveASC()` in `GothicSteadfastComponent.cpp`.
- `WeaponSlots`/ammo are **unreplicated pawn state**; owner-client sync is done via Client RPCs (see `Client_RestoreAmmo`).
- Non-UFUNCTION methods don't exist in Python bindings (`APawn::GetPlayerState` famously); Python resolves via UPROPERTY access (`player_state`) — see `vigil_pie_common.player_state()`.

## Load-bearing invariants (do not "fix" or drift these)

- **Damage** has ONE choke point: `GothicAttributeSet::PostGameplayEffectExecute` — `applied = max(1, raw + BaseAP − BaseDef) × (AP/BaseAP) × (BaseDef/Def)`. Attacker context comes from `MakeDamageContext` (stock `MakeEffectContext` stamps the Controller, which has no ASC — the classic silent-zero bug).
- Weapon raw damage = `WeaponData->Damage × (weapon's own GearPower/100, floor 1.0) × (1 + archetype armor rolls)`. Gear Score (sum of armor tiers, Salvage = 0) drives AttackPower/Defense.
- Melee vertical gating is **capsule span overlap** (`IsTargetInMeleeRange`), never an origin-ΔZ threshold — flat thresholds are provably impossible (the boss's same-floor origin gap exceeds a Thrall's cross-floor gap).
- `ReloadSpeed`, `HealingReceived`, and item Stars are **deliberately inert** (deferred features, not dead code). Strain data is authored to 0 pending a balance pass — the system stays.
- Rolled anything uses `FGothicDeterminism`, never raw `FMath::Rand*`.
- Design docs in `docs/` are the spec (`DESIGN_TENETS.md` governs); when code and docs conflict, flag it in the PR — don't silently resolve.

## Style

Match the surrounding file: comment density, naming, `LogVigilCombat` (never `LogTemp`) with grep-friendly `VigilTimeline|Actor|Event|key=value` formats for anything the verification pipeline should see. Tunables are `UPROPERTY(EditDefaultsOnly)` with honest defaults and a note when a value is reasoned rather than measured.

## Workflow

Branch `feat/<name>` or `fix/<name>` off current `main` → commit (named files) → push → open a **draft PR**. Keep PRs single-concern and small; a clean partial with an itemized remainder beats a sprawling complete. The pipeline will: integrate → build → PIE-verify → surface to the maintainer for review.
