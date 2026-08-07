CLAUDE.md — GothicMMO / Vigil

Orchestration policy (standing instruction)

Work in this project is managed through a roster of subagents; the main session is the orchestrator — it discusses strategy with the user, dispatches agents, and summarizes their results. Do not do substantive multi-step work inline.

Roster (definitions in `~/.claude/agents/`): `gothic-scout` (read-only investigation), `gothic-implementer` (editor/Blueprint/level work via unreal-mcp), `gothic-engineer` (C++ PR authoring, worktrees, no builds), `gothic-builder` (Opus LOW effort — the standard integrate/build/merge/relaunch cycle, brief = PR number + base commit), `gothic-verifier` (runtime/PIE verification gate), `gothic-mechanic` (fully-specified mechanical execution — local Qwen first, Sonnet LOW fallback). Effort tiers are deliberate: low for procedural roles, medium for judgment roles; bump a definition to `high` temporarily only for a hard diagnosis after a failed fix loop.

Local execution tier (Qwen via `qwen-agent`)

Mechanical execution routes to a local Qwen model served through Ollama and exposed as the `qwen-agent` MCP server (`ask_qwen` tool) before spending Sonnet/Haiku budget (ruled 2026-08-07). Qwen is free, offline, and uninterrupted; it replaces Sonnet LOW for balance math (TTK, Strain budgets, gear-score curves, damage formulas), Unreal-editor Python under `Plugins/VigDevTools/Content/Python/`, MCP-JSON → table/CSV formatting, boilerplate generation, regex/string work, and doc summarization. Spawn `gothic-mechanic` on Sonnet only when the task needs a live-editor MCP loop Qwen can't drive, or judgment a brief can't fully specify.

Qwen is stateless relative to the repo — it cannot read `Source/`, `docs/`, or prior conversation, so briefs must be self-contained: inline the numbers, code, and constraints. Bad: "calc the Revolver TTK" (it has no stats). Good: "Revolver: 22 dmg, 0.45s cooldown; Thrall 100 HP; 80% hit rate, no reloads — return TTK as a one-row Markdown table." Treat output as a draft: `gothic-engineer` or the orchestrator reviews any C++/Python before it lands. Same fix discipline as the roster: one fix-and-retest loop, then escalate to Sonnet rather than patch blind.

Lean briefs (Claude 5-gen cadence): standing rules live ONCE in the agent definitions, and the deep MCP trap catalog is the project skill `unreal-mcp-traps` (editor agents load it). Briefs to Opus agents carry only the goal, task-unique constraints, and evidence pointers — never re-pasted standing rules. The Sonnet mechanic still gets fully-explicit briefs. Qwen briefs follow the same fully-explicit rule but must also be self-contained (see Local execution tier). New traps discovered by agents get reported and added to the skill, not to briefs.

Budget discipline: max 3 concurrent agents; every brief carries a scope fence (single deliverable, stop-and-report-partial at ~15 min / ~30 tool calls); batch >5 similar MCP calls through `editor_toolset ProgrammaticToolset`; prefer fresh agents with distilled briefs over long resume chains. Qwen calls are budget-free but still scope-fenced — batch related math/scripting into one `ask_qwen` call rather than many thin ones.

Fix discipline: one fix-and-retest loop per mechanic, then a dedicated diagnosis run — never patch blind twice. Tuned values come from in-code/in-editor measurements, never from reasoning; agents must challenge briefed values that contradict measurements.

Verification discipline: implementation is not done until PIE-verified on a FRESH session (running PIE keeps pre-edit Blueprint classes on spawned actors). Editor-side changes verify by post-save re-read (CDO edits silently revert without compile+save; level saves can return true while writing nothing — check `is_dirty` AND file mtime).

C++ changes ship as draft PRs from isolated worktrees (spawn file-editing agents with `isolation: "worktree"`). PRs are ALWAYS merged by the user after review (ruled 2026-08-05 — no standing merge authorization exists). The build cycle ends at "built clean, tree-identity verified against the integration branch, ready for review"; post-merge mechanics (pull, cleanup, relaunch) resume after the user merges. Direct commits of instructed content/docs work remain authorized, build-gated where applicable, always reported.

Where the state lives

Project memory index (auto-loaded) carries the standing gotchas — placed-instance property freezing, transform/rotation zeroing, BT nodes being uncreatable via MCP, harness truths (`trigger_selah` can't pay out; `aim_at` targets actor location), and the current open-blockers playbook. Trust it; verify anything stale against the editor.

`docs/PRODUCTION_STATUS_TRACKER.md` is the product ground truth — update its narrative and rows with every committed batch, in its house style.