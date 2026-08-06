# CLAUDE.md — GothicMMO / Vigil

**Vigil** is a co-op, Gothic-horror action game built in **Unreal Engine 5.8** on a
dedicated-server + Gameplay Ability System (GAS) architecture. The C++ game module is
`GothicMMO`; the shipping vertical slice is the Eagle's Landing encounter (title →
Hearth hub → Palewood tutorial → boss arena). This file orients an AI assistant to the
repo; the two standing-instruction sections (**Orchestration policy**, **Where the
state lives**) are load-bearing and override default behavior.

---

## Orientation — read this first

Three files govern how work is done here, by lane:

- **`CLAUDE.md`** (this file) — orchestrator policy + repo map. The main session reads it.
- **`AGENTS.md`** — the **source-lane** contract for C++/Python-authoring agents (Codex
  and other external pipelines). It holds the engine/UHT traps and load-bearing gameplay
  invariants. Read it before touching `Source/` or `Plugins/VigDevTools/Content/Python/`.
- **`docs/`** — the design spec. `DESIGN_TENETS.md` governs; when code and docs conflict,
  flag it, don't silently resolve. `PRODUCTION_STATUS_TRACKER.md` is the product ground truth.

**Two-pipeline model.** This project is worked by two non-overlapping lanes:
1. **Editor / content lane** (this repo's subagent roster, driven via the `unreal-mcp` MCP
   server) owns `Content/` assets, Blueprints, levels, builds, and PIE runtime verification.
2. **Source lane** (C++ under `Source/GothicMMO/`, Python under
   `Plugins/VigDevTools/Content/Python/`) ships as draft PRs and never touches `Content/`.

The hard boundary between them exists because assets are edited live in an open editor;
crossing it corrupts uncommitted work. See AGENTS.md rule 1.

---

## Repository layout

```
GothicMMO.uproject          UE5 project descriptor (Git-LFS pointer)
GothicMMO.slnx / *.slnx     Solution files (generated tooling)
CLAUDE.md / AGENTS.md       Agent contracts (this file + source-lane rules)

Source/
  GothicMMO.Target.cs           Game target (TargetType.Game, BuildSettings V7)
  GothicMMOEditor.Target.cs     Editor target
  GothicMMO/
    GothicMMO.Build.cs          Module deps: GameplayAbilities, EnhancedInput,
                                ReplicationGraph, AIModule, Niagara, UMG, OnlineSubsystem…
    AbilitySystem/    GAS core — GothicAttributeSet (the damage choke point),
                      GothicAbilitySystemComponent, GothicAbilitySet, GothicGameplayTags,
                      and the GA_* abilities (Fire, Lunge, Slicer, Read, Reckoning,
                      HuntersStrike, BestialLucid boss abilities…)
    AI/               Enemy + boss brains — AIControllers, BTTask_*/BTService_* nodes,
                      GothicEnemyBase, pack AI (GothicPackSubsystem), melee hitboxes,
                      steadfast/poise, arena manager, encounter volumes
    Character/        GothicCharacterBase, GothicPlayerCharacter, input handler, anim instances
    Game/             GameMode/GameState/PlayerState/GameInstance, GothicDeterminism (seeded RNG),
                      GothicHintTrigger (tutorial gates)
    Items/            Inventory, item definitions/types, loot tables, world pickups
    UI/               GothicHUD + UMG widgets (health bars, crosshair, inventory, Selah/revive prompts)
    Weapons/          GothicWeaponData, GothicWeaponPerkCatalog

Content/              *** DO NOT EDIT FROM THE SOURCE LANE *** — Blueprints, levels
                      (L_TitleScreen, L_Hearth, L_Palewood, L_EaglesLanding), assets, Paragon
                      character kits. Binary assets are Git-LFS (.uasset/.umap).

Config/               DefaultEngine/Editor/Game/Input/GameplayTags .ini
Plugins/
  VigDevTools/        Custom MCP toolsets (Python) for live editor GAS/AI/encounter diagnostics
  unreal-mcp-main/    The unreal-mcp server plugin (editor control)
docs/                 Design bible (29 .md specs) + PRODUCTION_STATUS_TRACKER.md
.mcp.json             Registers the unreal-mcp HTTP server at 127.0.0.1:8000
```

Scale: ~78 `.cpp` / ~80 `.h` under `Source/GothicMMO/`; 29 design docs.

---

## Build & run workflow

- **Do not build ad-hoc.** The editor is typically open on this machine, which makes local
  builds hazardous, and the PR pipeline compiles every branch. Write code that compiles by
  care and say plainly in the PR that it is unbuilt (AGENTS.md rules 2–3).
- **Editor control** is via the `unreal-mcp` MCP server (`.mcp.json` → `http://127.0.0.1:8000/mcp`).
  Editor-side changes verify by post-save re-read: CDO edits silently revert without
  compile+save, and level saves can return `true` while writing nothing — check `is_dirty`
  **and** file mtime.
- **Runtime diagnostics** without recompiling: the `VigDevTools` plugin exposes Python
  toolsets (`dump_attributes`, `dump_active_effects`, `dump_granted_abilities`,
  `dump_enemy_blackboard`, `dump_boss_state`, `dump_selah_state`, …). After adding a tool,
  run `ModelContextProtocol.RefreshTools` in the console — no recompile needed.
- **Project bring-up** (fresh checkout / Blueprint wiring / GameplayEffect asset values):
  see `docs/SETUP_GUIDE.md`.
- **Git**: binary assets (`.uasset`, `.umap`, media, audio) are **Git-LFS**. Never
  `git add -A`/`git add .` — stage named files only; `Content/Blueprints/Game/Levels/L_EaglesLanding.umap`
  is routinely dirty with the maintainer's uncommitted work. `Binaries/`, `Intermediate/`,
  `Saved/`, `DerivedDataCache/`, and `.claude/` are git-ignored.

---

## Key conventions & load-bearing invariants

Match the surrounding file's style; log through `LogVigilCombat` (never `LogTemp`) with
grep-friendly `VigilTimeline|Actor|Event|key=value` formats. The following are project
invariants — do not "fix" or drift them. Full detail lives in **AGENTS.md** and the
project skills; the essentials:

- **Damage has ONE choke point** — `GothicAttributeSet::PostGameplayEffectExecute`. Attacker
  context comes from `MakeDamageContext` (stock `MakeEffectContext` stamps the Controller,
  which has no ASC — the classic silent-zero bug).
- **The player ASC lives on the PlayerState and outlives the pawn.** Any loose gameplay tag or
  cached state added must be cleared on death/respawn or it haunts the next life
  (see `State.Sprinting`/`State.Dead` in `GothicPlayerCharacter.cpp`). Components must
  **lazy-resolve** the ASC (`ResolveASC()` in `GothicSteadfastComponent.cpp`) because BeginPlay
  precedes possession on respawned pawns.
- **Melee vertical gating is capsule-span overlap** (`IsTargetInMeleeRange`), never an
  origin-ΔZ threshold.
- **All randomness uses `FGothicDeterminism`**, never raw `FMath::Rand*`.
- **Deliberately inert (deferred, not dead)**: `ReloadSpeed`, `HealingReceived`, item Stars,
  and zeroed Strain data. The systems stay.
- **UHT/GAS traps** (const-TCHAR UFUNCTIONs, no `mutable` UPROPERTYs, unreplicated ammo synced
  via Client RPCs, Python binding gaps) are catalogued in AGENTS.md and the `unreal-mcp-traps`
  project skill.

**Project skills** (invoke by name when the trigger fits): `vigil-gas-conventions` (GAS/HUD/
ability-set gotchas — check before proposing any ability/cooldown/HUD/grant fix),
`vigil-docs-style` (any `docs/` write, especially the tracker), `vigil-session-start`
(reconcile repo state against the tracker at the start of a status conversation),
`unreal-mcp-traps` (editor-agent MCP catalog).

---

## Orchestration policy (standing instruction)

Work in this project is managed through a roster of subagents; the main session is the orchestrator — it discusses strategy with the user, dispatches agents, and summarizes their results. Do not do substantive multi-step work inline.

- **Roster** (definitions in `~/.claude/agents/`): `gothic-scout` (read-only investigation), `gothic-implementer` (editor/Blueprint/level work via unreal-mcp), `gothic-engineer` (C++ PR authoring, worktrees, no builds), `gothic-builder` (Opus LOW effort — the standard integrate/build/merge/relaunch cycle, brief = PR number + base commit), `gothic-verifier` (runtime/PIE verification gate), `gothic-mechanic` (Sonnet LOW — fully-specified mechanical execution). Effort tiers are deliberate: low for procedural roles, medium for judgment roles; bump a definition to `high` temporarily only for a hard diagnosis after a failed fix loop.
- **Lean briefs (Claude 5-gen cadence)**: standing rules live ONCE in the agent definitions, and the deep MCP trap catalog is the project skill `unreal-mcp-traps` (editor agents load it). Briefs to Opus agents carry only the goal, task-unique constraints, and evidence pointers — never re-pasted standing rules. The Sonnet mechanic still gets fully-explicit briefs. New traps discovered by agents get reported and added to the skill, not to briefs.
- **Budget discipline**: max 3 concurrent agents; every brief carries a scope fence (single deliverable, stop-and-report-partial at ~15 min / ~30 tool calls); batch >5 similar MCP calls through `editor_toolset ProgrammaticToolset`; prefer fresh agents with distilled briefs over long resume chains.
- **Fix discipline**: one fix-and-retest loop per mechanic, then a dedicated diagnosis run — never patch blind twice. Tuned values come from in-code/in-editor measurements, never from reasoning; agents must challenge briefed values that contradict measurements.
- **Verification discipline**: implementation is not done until PIE-verified on a FRESH session (running PIE keeps pre-edit Blueprint classes on spawned actors). Editor-side changes verify by post-save re-read (CDO edits silently revert without compile+save; level saves can return true while writing nothing — check `is_dirty` AND file mtime).
- **C++ changes** ship as draft PRs from isolated worktrees (spawn file-editing agents with `isolation: "worktree"`). **PRs are ALWAYS merged by the user after review (ruled 2026-08-05 — no standing merge authorization exists).** The build cycle ends at "built clean, tree-identity verified against the integration branch, ready for review"; post-merge mechanics (pull, cleanup, relaunch) resume after the user merges. Direct commits of instructed content/docs work remain authorized, build-gated where applicable, always reported.

## Where the state lives

- **Project memory index** (auto-loaded) carries the standing gotchas — placed-instance property freezing, transform/rotation zeroing, BT nodes being uncreatable via MCP, harness truths (`trigger_selah` can't pay out; `aim_at` targets actor location), and the current open-blockers playbook. Trust it; verify anything stale against the editor.
- **`docs/PRODUCTION_STATUS_TRACKER.md`** is the product ground truth — update its narrative and rows with every committed batch, in its house style.
