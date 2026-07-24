# Vigil MCP Dev Tools

Custom MCP toolsets for Vigil (GothicMMO) development. These give Claude Code
(or any MCP client) direct query access to enemy AI state, GAS diagnostics,
and encounter-specific data inside the running editor.

## Installation

Copy the `VigDevTools` folder into your project's `Plugins/` directory:

```
GothicMMO/
  Plugins/
    VigDevTools/
      VigDevTools.uplugin
      Content/
        Python/
          vig_blackboard_tools.py
          vig_gas_tools.py
          vig_encounter_tools.py
```

Then in the editor:
1. Edit → Plugins → search "Vigil Dev Tools" → confirm enabled
2. Restart editor
3. Run `ModelContextProtocol.RefreshTools` in the console

## Toolsets

### VigBlackboardTools
Replaces the crashed BT visual debugger. Query blackboard key-value pairs,
list all enemies and their combat state, read AI perception state.

- `dump_enemy_blackboard` — Full blackboard dump for a specific enemy
- `list_enemies_in_level` — All AI-controlled characters with basic state
- `dump_enemy_perception` — What an enemy can currently see/hear

### VigGASTools
Query ability system state without recompiling with new UE_LOG statements.

- `dump_attributes` — All GothicAttributeSet values on an actor
- `dump_active_effects` — Currently running gameplay effects
- `dump_granted_abilities` — Granted ability classes (flags the BP_ vs bare C++ gotcha)
- `dump_gameplay_tags` — Active gameplay tags (State.Dead, State.Attacking, etc.)

### VigEncounterTools
Encounter tuning for Eagle's Landing vertical slice.

- `dump_boss_state` — Bestial Lucid health, phase, blackboard, nearby pillars
- `dump_selah_state` — Selah balance on every player in the level
- `dump_encounter_volumes` — Trigger volumes and encounter zones
- `check_pillar_destruction` — BestialLucidZone tagged actors and their state

## Adding new tools

Add a new `@toolset_registry.tool_call` function to an existing toolset class,
or create a new .py file following the same pattern. Then run
`ModelContextProtocol.RefreshTools` — no recompile needed.

## Caveat

These tools query editor/PIE state. Some blackboard and ASC reads require
PIE to be running with enemies spawned. In editor-only mode, they'll report
what's placed in the level but won't have runtime GAS state.
