# CLAUDE.md — GothicMMO / Vigil

## Orchestration policy (standing instruction)

Work in this project is managed through a roster of subagents; the main session is the orchestrator — it discusses strategy with the user, dispatches agents, and summarizes their results. Do not do substantive multi-step work inline.

- **Roster** (definitions in `~/.claude/agents/`): `gothic-scout` (read-only investigation), `gothic-implementer` (editor/Blueprint/level work via unreal-mcp), `gothic-engineer` (C++ + builds), `gothic-verifier` (runtime/PIE verification gate), `gothic-mechanic` (Sonnet tier — fully-specified mechanical execution). All Opus at medium effort except the mechanic.
- **Budget discipline**: max 3 concurrent agents; every brief carries a scope fence (single deliverable, stop-and-report-partial at ~15 min / ~30 tool calls); batch >5 similar MCP calls through `editor_toolset ProgrammaticToolset`; prefer fresh agents with distilled briefs over long resume chains.
- **Fix discipline**: one fix-and-retest loop per mechanic, then a dedicated diagnosis run — never patch blind twice. Tuned values come from in-code/in-editor measurements, never from reasoning; agents must challenge briefed values that contradict measurements.
- **Verification discipline**: implementation is not done until PIE-verified on a FRESH session (running PIE keeps pre-edit Blueprint classes on spawned actors). Editor-side changes verify by post-save re-read (CDO edits silently revert without compile+save; level saves can return true while writing nothing — check `is_dirty` AND file mtime).
- **C++ changes** ship as draft PRs from isolated worktrees (spawn file-editing agents with `isolation: "worktree"`); the user merges. Never push to main except when the user explicitly orders a commit/push of content work.

## Where the state lives

- **Project memory index** (auto-loaded) carries the standing gotchas — placed-instance property freezing, transform/rotation zeroing, BT nodes being uncreatable via MCP, harness truths (`trigger_selah` can't pay out; `aim_at` targets actor location), and the current open-blockers playbook. Trust it; verify anything stale against the editor.
- **`docs/PRODUCTION_STATUS_TRACKER.md`** is the product ground truth — update its narrative and rows with every committed batch, in its house style.