"""
vigil_combat_toolset.py

MCP toolset that DRIVES a live PIE session: fires weapons, activates abilities,
spawns enemies, and runs timed combat scenarios.

WHY THIS IS A SEPARATE TOOLSET
------------------------------
Unreal MCP runs in tool-search mode by default, which means an agent calls
list_toolsets and reads the groupings before calling anything. Splitting the
mutating tools into their own toolset with a docstring that says so makes the
read/write boundary legible at discovery time. VigilPIETools observes;
VigilCombatDrive changes things. That distinction costs nothing to maintain and
is the difference between an agent that knows it is about to alter a session
and one that does not.

RAILS
-----
PIE-only, hard guard. One scenario at a time. A duration cap that auto-aborts.
Every spawned actor tracked and destroyable in one call. Every mutation
appended to Saved/VigilMCP/mutation_log.txt. No arbitrary property setting --
if a new mutation is needed, it gets a named tool that says what it does.
"""

import json

import unreal
import toolset_registry

import vigil_pie_common as common
import vigil_pie_probe as probe
import vigil_combat_driver as driver


def _parse_steps(steps_json):
    try:
        steps = json.loads(steps_json)
    except Exception as exc:
        raise ValueError("steps_json is not valid JSON: %s" % exc)
    if not isinstance(steps, list):
        raise ValueError("steps_json must be a JSON array of step objects")
    return steps


def _split(csv):
    return [part.strip() for part in csv.split(",") if part.strip()]


@unreal.uclass()
class VigilCombatDrive(unreal.ToolsetDefinition):
    """Drive combat in a live PIE session of Vigil (GothicMMO). MUTATES STATE.

    Runs timed combat scenarios: firing, ability activation, aiming, enemy
    targeting, vital point control, and enemy spawning. Actions call gameplay
    functions directly and bypass Enhanced Input, which makes them reliable for
    testing combat logic but useless for testing input configuration.

    Scenarios are submitted whole and executed against game time by an in-engine
    tick. Nothing here blocks. Pair with VigilPIETools for observation.
    """

    # ----------------------------------------------------------------------
    # Discovery
    # ----------------------------------------------------------------------

    @toolset_registry.tool_call
    @staticmethod
    def scenario_actions() -> str:
        """List every action a scenario step can use, with its fields.

        Call this before writing a scenario. Steps are objects with an "at"
        (game-time offset in seconds), a "do" (action name), and whatever
        fields that action requires.

        Returns:
            JSON listing action names, required fields, and a worked example.
        """
        return common.as_json({
            "actions": driver.available_actions(),
            "fields": {
                "fire": {"actor": "str"},
                "melee": {"actor": "str"},
                "reload": {"actor": "str"},
                "convert_steadfast": {"actor": "str"},
                "swap_weapon": {"actor": "str", "index": "int"},
                "activate_slot": {"actor": "str",
                                  "slot": "LIGHT_ATTACK|HEAVY_ATTACK|ABILITY1|"
                                          "ABILITY2|ABILITY3|SUPER_ABILITY|PRIMARY_FIRE"},
                "trigger_selah": {"actor": "str"},
                "aim_at": {"actor": "str", "target": "str"},
                "aim_at_vital": {"actor": "str", "target": "str"},
                "set_combat_target": {"actor": "str (enemy)", "target": "str"},
                "freeze_vital": {"actor": "str", "index": "int, -1 = freeze in place"},
                "damage_vital": {"actor": "str", "amount": "float"},
                "console": {"command": "str", "force": "bool (optional)"},
                "mark": {"label": "str"},
            },
            "example": [
                {"at": 0.0, "do": "freeze_vital", "actor": "Feral", "index": 2},
                {"at": 0.0, "do": "aim_at_vital", "actor": "Player", "target": "Feral"},
                {"at": 0.5, "do": "fire", "actor": "Player"},
                {"at": 0.9, "do": "fire", "actor": "Player"},
                {"at": 1.3, "do": "mark", "label": "burst_complete"},
                {"at": 2.0, "do": "convert_steadfast", "actor": "Player"},
            ],
            "note": "Actor labels are pawn names from VigilPIETools.list_combatants. "
                    "A unique substring works.",
        })

    @toolset_registry.tool_call
    @staticmethod
    def scenario_validate(steps_json: str) -> str:
        """Check a scenario script without running it.

        Cheap dry run: verifies JSON shape, action names, and timing fields.
        It does NOT resolve actor labels, because actors may not exist until
        the scenario spawns them.

        Args:
            steps_json: JSON array of step objects.

        Returns:
            JSON with valid flag and any problems found.
        """
        try:
            steps = _parse_steps(steps_json)
        except ValueError as exc:
            return common.as_json({"valid": False, "problems": [str(exc)]})
        problems = driver.validate(steps)
        return common.as_json({
            "valid": not problems,
            "problems": problems,
            "step_count": len(steps),
            "duration": max((float(s.get("at", 0)) for s in steps), default=0.0),
        })

    # ----------------------------------------------------------------------
    # Running scenarios
    # ----------------------------------------------------------------------

    @toolset_registry.tool_call
    @staticmethod
    def scenario_run(steps_json: str,
                     max_duration: float = 60.0,
                     probe_actors: str = "",
                     probe_metrics: str = "",
                     probe_interval: float = 0.05) -> str:
        """Start a timed combat scenario, optionally recording state alongside it.

        Returns immediately -- it does not wait for the scenario to finish.
        Poll scenario_status, or just call scenario_stop_and_dump once enough
        game time has passed.

        When probe_actors and probe_metrics are supplied, a probe starts on the
        same world at the same moment, so scenario steps and sampled state share
        a game-time origin and can be correlated directly.

        Args:
            steps_json: JSON array of step objects. See scenario_actions.
            max_duration: Game seconds before the scenario auto-aborts.
            probe_actors: Comma-separated pawn labels to record. Empty for none.
            probe_metrics: Comma-separated metric keys. Empty for none.
            probe_interval: Probe sample period. 0.05 suits combat.

        Returns:
            JSON confirming what started.
        """
        world = common.require_world()

        if driver.is_running():
            return common.as_json({
                "started": False,
                "reason": "A scenario is already running.",
                "status": driver.status(),
            })

        steps = _parse_steps(steps_json)
        problems = driver.validate(steps)
        if problems:
            return common.as_json({"started": False, "problems": problems})

        probe_started = False
        actors = _split(probe_actors)
        metrics = _split(probe_metrics)
        if actors and metrics:
            if probe.is_running():
                probe.stop("restarted by scenario_run")
            targets = [(label, common.resolve_actor(world, label)) for label in actors]
            probe.start(targets, metrics, probe_interval, world=world)
            probe_started = True

        driver.start(world, steps, max_duration)

        return common.as_json({
            "started": True,
            "steps": len(steps),
            "scenario_duration": max((float(s.get("at", 0)) for s in steps), default=0.0),
            "probe_started": probe_started,
            "scenario": driver.status(),
            "probe": probe.status() if probe_started else None,
            "next": "Wait out the scenario, then call scenario_stop_and_dump.",
        })

    @toolset_registry.tool_call
    @staticmethod
    def scenario_status() -> str:
        """Check a running scenario without stopping or flushing it.

        Returns:
            JSON with steps fired, elapsed game time, and stop reason if ended.
        """
        return common.as_json({
            "scenario": driver.status(),
            "probe": probe.status(),
        })

    @toolset_registry.tool_call
    @staticmethod
    def scenario_stop_and_dump() -> str:
        """Stop the scenario and any paired probe, writing both to JSON.

        Returns file paths, not data. Read them with your own filesystem tools
        and correlate on the gt_rel field, which is game seconds from start in
        both files.

        Returns:
            JSON with the scenario and probe output paths.
        """
        was_running = driver.is_running()
        driver.stop("scenario_stop_and_dump called")
        scenario_out = driver.dump()

        probe_out = None
        if probe.is_running() or probe.status().get("samples"):
            probe.stop("scenario_stop_and_dump called")
            probe_out = probe.dump()

        failures = [r for r in driver.results() if not r.get("ok")]
        return common.as_json({
            "was_running": was_running,
            "scenario_file": scenario_out,
            "probe_file": probe_out,
            "failed_steps": failures,
            "correlate_on": "gt_rel",
        })

    # ----------------------------------------------------------------------
    # One-shot actions (no scenario needed)
    # ----------------------------------------------------------------------

    @toolset_registry.tool_call
    @staticmethod
    def force_ability(actor_label: str, slot: str) -> str:
        """Activate one ability immediately by slot, bypassing input.

        Convenience for quick checks. For anything where timing matters
        relative to other actions, use a scenario instead.

        Args:
            actor_label: Pawn name. A unique substring works.
            slot: LIGHT_ATTACK, HEAVY_ATTACK, ABILITY1, ABILITY2, ABILITY3,
                SUPER_ABILITY, or PRIMARY_FIRE.

        Returns:
            JSON with whether activation succeeded and the resulting cooldown.
        """
        world = common.require_world()
        pawn = common.resolve_actor(world, actor_label)
        asc = pawn.get_gothic_asc()
        if asc is None:
            return common.as_json({"activated": False, "reason": "no ASC on %s"
                                   % pawn.get_name()})
        slot_enum = common.ability_slot(slot)
        common.audit("force_ability\t%s\t%s" % (pawn.get_name(), slot))
        activated = bool(asc.try_activate_ability_by_slot(slot_enum))
        return common.as_json({
            "actor": pawn.get_name(),
            "slot": slot.upper(),
            "activated": activated,
            "cooldown_remaining": common.try_read(
                lambda: round(float(asc.get_cooldown_remaining_for_slot(slot_enum)), 2)),
            "cooldown_total": common.try_read(
                lambda: round(float(asc.get_cooldown_total_for_slot(slot_enum)), 2)),
            "note": "activated=False usually means cooldown, cost, or a blocking tag. "
                    "Check ActivationBlockedTags before assuming the grant failed.",
        })

    @toolset_registry.tool_call
    @staticmethod
    def set_time_dilation(scale: float) -> str:
        """Set global time dilation, e.g. 0.25 to watch a hit window resolve.

        Probe and scenario timing both key off game time, so they stay correct
        under dilation. Wall-clock expectations will not.

        Args:
            scale: 1.0 is normal speed. 0.1 to 0.5 is useful for combat frames.

        Returns:
            JSON confirming the command run.
        """
        world = common.require_world()
        common.run_console(world, "slomo %f" % float(scale))
        return common.as_json({"time_dilation": float(scale)})

    # ----------------------------------------------------------------------
    # Spawning
    # ----------------------------------------------------------------------

    @toolset_registry.tool_call
    @staticmethod
    def spawn_enemy(class_path: str,
                    x: float, y: float, z: float,
                    yaw: float = 0.0) -> str:
        """Spawn an actor into the running PIE world at a fixed transform.

        Fixed transforms are the cheapest determinism you get -- an encounter
        that starts from the same geometry every run is comparable between runs
        even when AI behaviour is not.

        Blueprint class paths need the _C suffix, e.g.
        "/Game/Blueprints/Enemies/BP_Enemy_FeralRetained.BP_Enemy_FeralRetained_C"

        Everything spawned is tracked; scenario_cleanup destroys it all.

        Args:
            class_path: Full object path to the class, with _C for Blueprints.
            x: World X.
            y: World Y.
            z: World Z. Spawn above the floor; capsules resolve downward.
            yaw: Facing in degrees.

        Returns:
            JSON with the spawned actor's name, to use as an actor label.
        """
        world = common.require_world()
        actor = driver.spawn(world, class_path, (x, y, z), (0.0, yaw, 0.0))
        return common.as_json({
            "spawned": actor.get_name(),
            "class": common.try_read(lambda: actor.get_class().get_name()),
            "location": [x, y, z],
            "note": "Use the spawned name as an actor label in scenarios.",
        })

    @toolset_registry.tool_call
    @staticmethod
    def scenario_cleanup() -> str:
        """Destroy every actor this harness spawned. Safe to call repeatedly.

        Call this before ending PIE, and before any run that should start from
        a clean encounter. Leftover spawns are the most common way a scenario
        result becomes uninterpretable.

        Returns:
            JSON listing what was destroyed.
        """
        common.require_world()
        destroyed = driver.cleanup()
        return common.as_json({"destroyed": destroyed, "count": len(destroyed)})
