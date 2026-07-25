# MCP PIE Debug Loop

How Claude Code observes and drives a live Play-In-Editor session of Vigil. Separate from `SETUP_GUIDE.md` because that guide covers building the game's content; this covers the development harness around it.

Last updated: July 25, 2026 — added `VigilCombatDrive` (scenario execution, spawning, ability forcing), refactored shared helpers into `vigil_pie_common.py`, switched probe timestamps from wall clock to game time. Nothing verified against a running editor yet.

---

## Two toolsets, one boundary

| Toolset | Role | Mutates |
| --- | --- | --- |
| `VigilPIETools` | Observation — GAS vitals, Steadfast, vital points, pack registration, cooldowns, time-series recording | Only `run_console_command` and `set_vital_debug_draw` |
| `VigilCombatDrive` | Combat driving — timed scenarios, ability forcing, enemy spawning, time dilation | Yes, and says so in its docstring |

The split is not cosmetic. Unreal MCP runs in tool-search mode by default, so an agent calls `list_toolsets` and reads groupings before calling anything. Keeping mutation in its own toolset makes the boundary legible at discovery time — the agent knows it is about to change a session before it does.

An agent that can freely mutate a running game will thrash: change a value, observe a symptom, change another, never form a hypothesis. The approach that works in this codebase is instrument-then-read, and the tool surface is shaped to enforce it.

---

## Architecture

```
Claude Code ──http──> Unreal MCP server (in-editor, 127.0.0.1:8000/mcp)
                              │  (game thread, serial, never blocks)
                              ├─ VigilPIETools ────> vigil_pie_probe ──┐
                              └─ VigilCombatDrive ─> vigil_combat_driver┤
                                                                        ▼
                                                        Saved/VigilMCP/*.json
                                                                        │
Claude Code ──its own filesystem tools─────────────────────────────────┘
```

Three constraints drive everything above.

**Tool calls run on the game thread, serially.** Epic's documentation is explicit. No tool may block, sleep, or poll-until-condition — a tool that waited three seconds would freeze PIE for three seconds and destroy the timing being measured. Both the probe and the scenario driver are therefore submit-and-return: they register a tick callback and hand control straight back.

**Scenarios are submitted whole, not issued step by step.** One tool call per action would work, but every action would land at the mercy of MCP round-trip latency — roughly 200–400 ms of jitter — which is useless for hit windows, recovery frames, or Steadfast hold durations. A scenario is a JSON array with game-time offsets, executed by the engine.

**Anything Claude Code can already do, it should do itself.** `Saved/Logs/GothicMMO.log` is a file and Claude Code has file tools; wrapping it would add a round trip and a context cost for zero capability. Same reason `probe_stop_and_dump` returns a path rather than data.

---

## Files

| Path | Purpose |
| --- | --- |
| `Plugins/VigilTools/VigilTools.uplugin` | Content-only plugin, no C++ module, cannot affect a packaged build |
| `Content/Python/vigil_pie_common.py` | Shared helpers — every version-sensitive engine call lives here, on purpose |
| `Content/Python/vigil_pie_probe.py` | Tick-driven recorder, 15 metrics |
| `Content/Python/vigil_combat_driver.py` | Scenario executor, 14 actions, spawn tracking |
| `Content/Python/vigil_pie_toolset.py` | `VigilPIETools` — the observation surface |
| `Content/Python/vigil_combat_toolset.py` | `VigilCombatDrive` — the driving surface |
| `Content/Python/init_unreal.py` | Startup hook, imports each toolset independently |

`vigil_pie_common.py` exists so that when `get_game_world()` turns out to be named something else on this build, it is one line in one file rather than a hunt through four. That is its entire justification.

---

## Setup

1. **Drop `Plugins/VigilTools/` next to `GothicMMO.uproject`.** No compile step — content-only plugins need no rebuild. `EnabledByDefault: true` means the `.uproject` is untouched.
2. **Edit > Plugins**: confirm `Unreal MCP`, `Toolset Registry`, `Python Editor Script Plugin`, `Editor Scripting Utilities`. `GASToolsets` is optional and ships disabled — it provides generic attribute-set inspection this harness does not duplicate.
3. **Editor Preferences > General > Model Context Protocol**: enable `Auto Start Server`.
4. **Restart the editor.** Two lines should appear in the Output Log: the MCP bind address, and `[VigilTools] vigil_pie_toolset loaded`. Missing second line means the Python did not import and nothing else will work.
5. **Editor console**: `ModelContextProtocol.GenerateClientConfig ClaudeCode` — writes `.mcp.json` to the project root, safe to re-run.
6. **Launch Claude Code from the project root**, ask it to run `capabilities`. Every entry under `api` should read `true`.

After editing any `.py`: `ModelContextProtocol.RefreshTools`, then reconnect the client. The client caches the tool schema, so refresh alone is often not enough — this is the step people miss before concluding the toolset is broken.

---

## Driving combat

### The input-path fork

Every action in `VigilCombatDrive` calls a `BlueprintCallable` function directly. Nothing synthesises keyboard or mouse input. That is deliberate and it cuts both ways:

- **Reliable for**: combat math, death states, vital point behaviour, AI response, GAS-driven HUD updates. No focus issues, no input plumbing, no machine-dependent behaviour.
- **Blind to**: anything wrong in an Input Action asset. The hold-to-convert Steadfast defect lives in `IA_Reload`'s trigger configuration, upstream of everything the driver touches.

Which makes the driver a **bisection tool** for exactly that class of bug. `convert_steadfast` calls `ConvertSteadfastToReserve()` directly; if it succeeds when driven and fails when the key is held, the defect is in the Input Action, not the C++ binding and not the conversion logic. Same pattern applies to any input-adjacent bug.

Do not add input synthesis here. If input itself needs testing, that is a separate and deliberately nastier tool.

### Scenario format

```json
[
  {"at": 0.0, "do": "freeze_vital",  "actor": "Feral", "index": 2},
  {"at": 0.0, "do": "aim_at_vital",  "actor": "Player", "target": "Feral"},
  {"at": 0.5, "do": "fire",          "actor": "Player"},
  {"at": 0.9, "do": "fire",          "actor": "Player"},
  {"at": 1.3, "do": "mark",          "label": "burst_complete"},
  {"at": 2.0, "do": "convert_steadfast", "actor": "Player"}
]
```

`at` is a game-time offset from scenario start. Steps record `fired_at` and `drift` — the gap between scheduled and actual — so a run degraded by frame hitches is visible in the output rather than silently wrong.

Actions: `fire`, `melee`, `reload`, `convert_steadfast`, `swap_weapon`, `activate_slot`, `trigger_selah`, `aim_at`, `aim_at_vital`, `set_combat_target`, `freeze_vital`, `damage_vital`, `console`, `mark`.

### Game time, not wall time

Probe samples and scenario steps both stamp `unreal.GameplayStatics.get_time_seconds`, and both files carry `gt_rel` — game seconds from start. Correlate on that field.

`slomo 0.25` is a legitimate debugging tool; it is how a vital point shift or a hit window becomes visible at all. Under dilation, wall-clock stamps desync from what the game actually did, and a scenario's `at: 0.5` stops meaning what the probe thinks it means.

---

## Determinism

Driving combat is the easy half. Making a combat test repeatable is the hard half, and the obstacle is specific: `GothicBTService_WeightedActionSelect` picks enemy actions randomly, so two identical scenario runs produce different fights.

Available now, no C++:

- **`freeze_vital`** — the single most valuable primitive here. With the vital locked to a known index, hit detection becomes testable instead of merely observable.
- **`damage_vital`** — drives shift thresholds synthetically without needing real combat to reach them.
- **Fixed spawn transforms** via `spawn_enemy` — an encounter starting from identical geometry is comparable between runs even when AI behaviour is not.
- **`t.MaxFPS 60`** to stabilise frame pacing.

Needs C++, deferred: a seeded `FRandomStream` in the weighted select, driven by a `Gothic.TestSeed` cvar. Perhaps forty lines.

The distinction worth holding: **without a seed you get statistical testing, with a seed you get regression testing.** For the Aug 11 vital point tuning pass, statistical is fine and arguably more honest — run a scenario twenty times across `HitDetectionRadius` values and read the hit-rate distribution. Regression testing matters later, for death-state behaviour.

---

## Failure handling conventions

House rules. New tools should follow them.

- **Never return a plausible default for a value that could not be read.** Every version-sensitive read goes through `common.try_read`, which returns `{"error": "..."}` in place of the value. A tool reporting `0.0` for a Steadfast charge it could not resolve produces confident wrong conclusions — strictly worse than an error.
- **Raise on ambiguity, never guess.** `resolve_actor` refuses a label matching two pawns. An agent silently driving the wrong enemy produces a plausible wrong result, which looks like data.
- **Assume PIE teardown will invalidate references mid-run.** The Python interpreter outlives the PIE world. Probe targets are revalidated every sample; the scenario driver checks world validity every tick. Both auto-stop with a recorded reason.
- **A raising tick callback raises every frame.** Probe and driver both auto-stop after five consecutive errors.
- **Cap everything.** 20,000 probe samples (~33 min at 10Hz), 60s default scenario duration.
- **Audit every mutation.** Console commands, spawns, forced abilities, and debug-draw toggles all append to `Saved/VigilMCP/mutation_log.txt`.

---

## Known unverified assumptions

Unreal MCP is Experimental and this was authored against documentation, not a running 5.8 editor. `capabilities` answers most of these in one call.

- `UnrealEditorSubsystem.get_game_world()` returning the PIE world rather than `None`. Most likely thing to need fixing.
- `GameplayStatics.begin_deferred_actor_spawn_from_class` into a PIE world from editor Python. Highest-risk call in the harness — verify before trusting any scenario that depends on spawning.
- `LevelEditorSubsystem.editor_request_begin_play` / `editor_request_end_play` existing on this build. If `begin_play` is absent the loop still works; PIE gets started by hand.
- Whether the registry discovers top-level modules under `Content/Python` or only modules inside a package. `init_unreal.py` covers both.
- Whether Vigil's project classes (`GothicSteadfastComponent` and siblings) are exposed in the `unreal` namespace. They should be, as reflected `UCLASS` types in a project module.

Scenario scripts are passed as JSON strings rather than typed arrays, and results come back as JSON strings rather than dataclasses. Epic recommends structured types, and that is the right end state, but a schema mismatch in an Experimental generator fails at tool-discovery time rather than call time — much worse to diagnose. Migrate once the dataclass path is confirmed on this build.

---

## Fit against current milestones

The harness is only justified if it serves work already on the board.

- **Vital point tuning pass (Aug 11)** — `freeze_vital` plus `aim_at_vital` plus a 0.016s probe on `vital_index` and `vital_location` turns radius tuning from a feel exercise into a measured one. This is the strongest case for the whole harness.
- **Steadfast bar display bug** — `steadfast` from the component and `conversion_latched` from the character, sampled together, separate "the value is wrong" from "the widget is not bound" in one run. Given `SteadfastComponentRef` is the suspected cause, this settles it.
- **PackSubsystem registration** — `pack_id` reads `NAME_None` or it does not. That is the whole diagnosis, and it has been blocking Eagle's Landing AI validation.
- **Death and failure state (Aug 11)** — useful for verifying the checkpoint ammo snapshot once that infrastructure exists, not before.

Not justified for Blueprint authoring, and should not grow that way. `WBP_Inventory` node reassignment and `WBP_Crosshair_Pistol` graph work stay manual.
