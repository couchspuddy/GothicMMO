"""
vigil_pie_toolset.py

MCP toolset for OBSERVING a live PIE session in Vigil.

Read-only by design, with one exception (run_console, allowlisted and audited).
Anything that changes gameplay state lives in VigilCombatDrive so the boundary
is visible to an agent at discovery time.

DESIGN THESIS
-------------
An agent that can freely mutate a running game will thrash -- change a value,
see a symptom, change another, never form a hypothesis. The debugging approach
that works in this codebase is instrument-then-read, and this tool surface is
shaped to support exactly that.

THREAD MODEL
------------
Unreal MCP runs every tool call on the game thread, serially. Nothing here
blocks, sleeps, or loops until a condition. Duration-based observation goes
through vigil_pie_probe, which samples on tick and flushes to disk.
"""

import unreal
import toolset_registry

import vigil_pie_common as common
import vigil_pie_probe as probe


@unreal.uclass()
class VigilPIETools(unreal.ToolsetDefinition):
    """Observe a live Play-In-Editor session of Vigil (GothicMMO). Read-only.

    Reads GAS attributes, Steadfast charge, vital point state, combat state,
    pack registration, and ability cooldowns out of a running PIE session, and
    records any of them over time. Use probe_start / probe_stop_and_dump rather
    than repeated polling: tool calls execute on the game thread and cannot
    wait, so agent-side polling samples at MCP round-trip latency.

    To change gameplay state, use the VigilCombatDrive toolset instead.
    """

    # ----------------------------------------------------------------------
    # Diagnostics
    # ----------------------------------------------------------------------

    @toolset_registry.tool_call
    @staticmethod
    def capabilities() -> str:
        """Report which engine APIs and Vigil classes this harness resolved.

        Run this first, once, after any engine upgrade. Several APIs used here
        fail by returning None rather than raising, so a resolution table is
        more useful than a smoke test.

        Returns:
            JSON with api and vigil_classes tables mapping name to availability.
        """
        subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

        api = {
            "UnrealEditorSubsystem": subsystem is not None,
            "UnrealEditorSubsystem.get_game_world": hasattr(subsystem, "get_game_world"),
            "LevelEditorSubsystem": level_editor is not None,
            "LevelEditorSubsystem.editor_request_begin_play": hasattr(
                level_editor, "editor_request_begin_play"),
            "LevelEditorSubsystem.editor_request_end_play": hasattr(
                level_editor, "editor_request_end_play"),
            "register_slate_post_tick_callback": hasattr(
                unreal, "register_slate_post_tick_callback"),
            "GameplayStatics.get_time_seconds": hasattr(
                unreal.GameplayStatics, "get_time_seconds"),
            # Expected False, permanently: BeginDeferredActorSpawnFromClass is
            # meta=(BlueprintInternalUseOnly="true") (GameplayStatics.h:65-67)
            # and PyGenUtil.cpp:1621 excludes those from the Python bindings.
            # Kept as a capability row so nobody rediscovers it as a bug.
            "GameplayStatics.begin_deferred_actor_spawn_from_class (expected False)":
                hasattr(unreal.GameplayStatics,
                        "begin_deferred_actor_spawn_from_class"),
            # The route spawn_enemy actually uses.
            "AIBlueprintHelperLibrary.spawn_ai_from_class": hasattr(
                unreal.AIBlueprintHelperLibrary, "spawn_ai_from_class"),
            "AIPerceptionComponent.get_currently_perceived_actors": hasattr(
                unreal.AIPerceptionComponent, "get_currently_perceived_actors"),
            "MathLibrary.find_look_at_rotation": hasattr(
                unreal.MathLibrary, "find_look_at_rotation"),
        }

        vigil = {}
        for name in (
            "GothicCharacterBase", "GothicPlayerCharacter", "GothicEnemyBase",
            "GothicAbilitySystemComponent", "GothicAttributeSet",
            "GothicSteadfastComponent", "GothicVitalPointComponent",
            "GothicCombatStateComponent", "GothicPackSubsystem",
            "GothicAbilitySlot",
        ):
            vigil[name] = hasattr(unreal, name)

        return common.as_json({
            "api": api,
            "vigil_classes": vigil,
            "probe_metrics": probe.available_metrics(),
            "in_pie": common.pie_world() is not None,
            "note": "Any False under api means the tool depending on it returns "
                    "a clear error rather than wrong data.",
        })

    # ----------------------------------------------------------------------
    # PIE lifecycle
    # ----------------------------------------------------------------------

    @toolset_registry.tool_call
    @staticmethod
    def pie_status() -> str:
        """Check whether PIE is running and summarise the live world.

        Returns:
            JSON with in_pie, world name, game time, pawn inventory, probe state.
        """
        world = common.pie_world()
        if world is None:
            return common.as_json({"in_pie": False, "probe": probe.status()})

        pawns = common.all_pawns(world)
        return common.as_json({
            "in_pie": True,
            "world": common.try_read(lambda: world.get_name()),
            "game_time": common.game_time(world),
            "pawn_count": len(pawns),
            "pawns": [
                {"name": p.get_name(),
                 "class": common.try_read(lambda p=p: p.get_class().get_name())}
                for p in pawns
            ],
            "probe": probe.status(),
        })

    @toolset_registry.tool_call
    @staticmethod
    def pie_stop() -> str:
        """End the current PIE session.

        Stops any running probe first, so the buffer stays flushable after the
        world tears down.

        Returns:
            JSON describing what happened.
        """
        if probe.is_running():
            probe.stop("pie_stop called")

        level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        end_play = getattr(level_editor, "editor_request_end_play", None)
        if end_play is None:
            return common.as_json({
                "stopped": False,
                "reason": "LevelEditorSubsystem.editor_request_end_play not "
                          "available on this build. Stop PIE manually (Esc).",
            })
        end_play()
        return common.as_json({"stopped": True, "probe_flushable": True})

    @toolset_registry.tool_call
    @staticmethod
    def pie_start() -> str:
        """Start a PIE session at the current editor viewport location.

        Not available on every engine build. If this reports unavailable, ask
        the developer to press Play -- do not retry.

        Returns:
            JSON describing whether PIE was started.
        """
        if common.pie_world() is not None:
            return common.as_json({"started": False, "reason": "Already in PIE."})

        level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        begin_play = getattr(level_editor, "editor_request_begin_play", None)
        if begin_play is None:
            return common.as_json({
                "started": False,
                "reason": "LevelEditorSubsystem.editor_request_begin_play not "
                          "available on this build. Ask the developer to press Play.",
            })
        begin_play()
        return common.as_json({
            "started": True,
            "note": "PIE start is asynchronous. Call pie_status once to confirm "
                    "the world exists before resolving actors.",
        })

    # ----------------------------------------------------------------------
    # State inspection
    # ----------------------------------------------------------------------

    @toolset_registry.tool_call
    @staticmethod
    def list_combatants() -> str:
        """List every pawn in the PIE world with a one-line combat summary.

        Cheap orientation call. The returned names are the actor labels every
        other tool takes, here and in VigilCombatDrive.

        Returns:
            JSON array of pawns with health, alive state, and which Gothic
            components each one has.
        """
        world = common.require_world()
        rows = []
        for pawn in common.all_pawns(world):
            rows.append({
                "name": pawn.get_name(),
                "class": common.try_read(lambda p=pawn: p.get_class().get_name()),
                "health": common.try_read(lambda p=pawn: round(float(p.get_health()), 1)),
                "max_health": common.try_read(
                    lambda p=pawn: round(float(p.get_max_health()), 1)),
                "alive": common.try_read(lambda p=pawn: bool(p.is_alive())),
                "components": [
                    name for name in (
                        "GothicSteadfastComponent",
                        "GothicVitalPointComponent",
                        "GothicCombatStateComponent",
                    ) if common.component(pawn, name) is not None
                ],
            })
        return common.as_json({"count": len(rows), "pawns": rows})

    @toolset_registry.tool_call
    @staticmethod
    def describe_combatant(actor_label: str) -> str:
        """Deep state dump for one live pawn.

        Reads GAS vitals, Steadfast charge and conversion latch, vital point
        index and world location, combat state, pack registration, and per-slot
        ability cooldowns. Any field that could not be read appears as an error
        object rather than a plausible default -- treat those as findings.

        Args:
            actor_label: Pawn name from list_combatants. A unique substring works.

        Returns:
            JSON state dump for the resolved pawn.
        """
        world = common.require_world()
        actor = common.resolve_actor(world, actor_label)

        out = {
            "name": actor.get_name(),
            "class": common.try_read(lambda: actor.get_class().get_name()),
            "game_time": common.game_time(world),
            "vitals": {
                "health": common.try_read(lambda: round(float(actor.get_health()), 2)),
                "max_health": common.try_read(lambda: round(float(actor.get_max_health()), 2)),
                "stamina": common.try_read(lambda: round(float(actor.get_stamina()), 2)),
                "ether": common.try_read(lambda: round(float(actor.get_ether()), 2)),
                "alive": common.try_read(lambda: bool(actor.is_alive())),
            },
            "transform": {
                "location": common.try_read(lambda: common.vec(actor.get_actor_location())),
                "speed": common.try_read(lambda: round(actor.get_velocity().length(), 1)),
            },
        }

        steadfast = common.component(actor, "GothicSteadfastComponent")
        if steadfast is not None:
            out["steadfast"] = {
                "current": common.try_read(
                    lambda: round(float(steadfast.get_current_steadfast()), 3)),
                "max": common.try_read(
                    lambda: round(float(steadfast.get_max_steadfast()), 3)),
                "fill_rate_per_second": common.try_read(
                    lambda: steadfast.get_editor_property("fill_rate_per_second")),
                # Latch state lives on the character, not the component. If the
                # component value is healthy and this is False while holding the
                # key, the defect is upstream in the Input Action trigger.
                "conversion_latched": common.try_read(
                    lambda: bool(actor.is_converting_steadfast())),
                "refill_cost": common.try_read(
                    lambda: int(actor.get_active_steadfast_refill_cost())),
            }

        vital = common.component(actor, "GothicVitalPointComponent")
        if vital is not None:
            out["vital_point"] = {
                "active_index": common.try_read(lambda: int(vital.get_active_vital_index())),
                "current_world_location": common.try_read(
                    lambda: common.vec(vital.get_current_vital_world_location())),
                "next_world_location": common.try_read(
                    lambda: common.vec(vital.get_next_vital_world_location())),
                "hit_detection_radius": common.try_read(
                    lambda: vital.get_editor_property("hit_detection_radius")),
                "shift_threshold": common.try_read(
                    lambda: vital.get_editor_property("shift_threshold")),
                # bDebugDrawVital in C++ (GothicVitalPointComponent.h:197). The
                # Python name drops the bool "b" prefix -- see set_vital_debug_draw.
                "debug_draw": common.try_read(
                    lambda: vital.get_editor_property("debug_draw_vital")),
            }

        # In-combat has two sources and only one of them exists per pawn type:
        # the component is Blueprint-added and only BP_GothicPlayerCharacter has
        # it, while enemies carry the state on the blackboard's bIsInCombat key
        # (AGothicEnemyAIController::SetBlackboardTarget, .cpp:98). Report which
        # one answered -- an unlabelled bool here is how this rotted unnoticed.
        blackboard = common.blackboard(actor)
        combat = common.component(actor, "GothicCombatStateComponent")
        if combat is not None:
            out["combat_state"] = {
                "in_combat": common.try_read(lambda: bool(combat.is_in_combat())),
                "source": "GothicCombatStateComponent",
            }
        elif blackboard is not None:
            out["combat_state"] = {
                "in_combat": common.try_read(
                    lambda: bool(blackboard.get_value_as_bool("bIsInCombat"))),
                "source": "blackboard bIsInCombat",
            }
        else:
            out["combat_state"] = {
                "error": "no GothicCombatStateComponent and no blackboard on "
                         "this pawn -- in-combat state is unreadable"}

        # Enemy-only reads. NAME_None on pack_id is the diagnostic for the
        # PackSubsystem registration gap.
        if hasattr(actor, "get_pack_id"):
            # combat_target comes from the blackboard: the pawn's own CombatTarget
            # is never cleared (ClearCombatTarget clears only the key,
            # GothicEnemyAIController.cpp:116-124), so it is reported separately
            # and labelled as the latched value rather than mistaken for live truth.
            out["pack"] = {
                "pack_id": common.try_read(lambda: str(actor.get_pack_id())),
                "combat_target": common.try_read(
                    lambda: blackboard.get_value_as_object("TargetActor").get_name()
                    if (blackboard is not None
                        and common.is_valid(
                            blackboard.get_value_as_object("TargetActor")))
                    else None),
                "combat_target_pawn_latched": common.try_read(
                    lambda: actor.get_combat_target().get_name()
                    if common.is_valid(actor.get_combat_target()) else None),
            }

        asc = common.try_read(lambda: actor.get_gothic_asc(), default=None)
        cooldowns = {}
        for slot_name in ("LIGHT_ATTACK", "HEAVY_ATTACK", "ABILITY1", "ABILITY2",
                          "ABILITY3", "SUPER_ABILITY", "PRIMARY_FIRE"):
            try:
                slot = common.ability_slot(slot_name)
            except LookupError:
                continue
            cooldowns[slot_name.lower()] = {
                "remaining": common.try_read(
                    lambda s=slot: round(float(asc.get_cooldown_remaining_for_slot(s)), 2)),
                "total": common.try_read(
                    lambda s=slot: round(float(asc.get_cooldown_total_for_slot(s)), 2)),
            }
        out["ability_cooldowns"] = cooldowns or {
            "error": "ASC or GothicAbilitySlot did not resolve from Python"}

        return common.as_json(out)

    # ----------------------------------------------------------------------
    # Time-series observation
    # ----------------------------------------------------------------------

    @toolset_registry.tool_call
    @staticmethod
    def probe_start(actor_labels: str,
                    metrics: str,
                    interval_seconds: float = 0.1) -> str:
        """Start recording state over time without blocking the game.

        Samples on the engine tick into a memory buffer and returns immediately.
        Play the scenario, then call probe_stop_and_dump for a JSON file of the
        whole window.

        Use this instead of repeatedly calling describe_combatant: agent-side
        polling samples at MCP round-trip latency, far too coarse to see a
        Steadfast fill curve or a vital point shift.

        Args:
            actor_labels: Comma-separated pawn names from list_combatants.
            metrics: Comma-separated metric keys. See capabilities for the list.
            interval_seconds: Sample period. 0.1 is usually right; 0.016 for
                single-frame events like a hit registration window.

        Returns:
            JSON confirming what is being recorded.
        """
        world = common.require_world()
        if probe.is_running():
            return common.as_json({
                "started": False,
                "reason": "A probe is already running.",
                "status": probe.status(),
            })

        labels = [p.strip() for p in actor_labels.split(",") if p.strip()]
        keys = [p.strip() for p in metrics.split(",") if p.strip()]
        targets = [(label, common.resolve_actor(world, label)) for label in labels]
        probe.start(targets, keys, interval_seconds, world=world)
        return common.as_json({
            "started": True,
            "status": probe.status(),
            "next": "Play the scenario, then call probe_stop_and_dump.",
        })

    @toolset_registry.tool_call
    @staticmethod
    def probe_status() -> str:
        """Check a running probe without stopping or flushing it.

        Returns:
            JSON with sample count, elapsed time, and stop reason if it ended.
        """
        return common.as_json(probe.status())

    @toolset_registry.tool_call
    @staticmethod
    def probe_stop_and_dump(filename_stem: str = "probe") -> str:
        """Stop the probe and write the buffer to JSON on disk.

        Returns a file path, not the data. Read the file with your own
        filesystem tools -- a 10Hz recording of a two minute fight is over a
        thousand rows and does not belong in a tool result.

        Args:
            filename_stem: Output name prefix under Saved/VigilMCP/.

        Returns:
            JSON with the absolute path and the number of samples written.
        """
        was_running = probe.is_running()
        probe.stop("probe_stop_and_dump called")
        result = probe.dump(filename_stem or "probe")
        result["was_running"] = was_running
        return common.as_json(result)

    # ----------------------------------------------------------------------
    # Narrow mutation
    # ----------------------------------------------------------------------

    @toolset_registry.tool_call
    @staticmethod
    def set_vital_debug_draw(actor_label: str, enabled: bool) -> str:
        """Toggle the vital point debug overlay on one live pawn.

        The overlay is what makes HitDetectionRadius tunable honestly -- the
        radius cannot be judged without seeing where the sphere actually is.

        Args:
            actor_label: Pawn name from list_combatants.
            enabled: True to draw the overlay.

        Returns:
            JSON confirming the new value read back from the component.
        """
        world = common.require_world()
        actor = common.resolve_actor(world, actor_label)
        vital = common.component(actor, "GothicVitalPointComponent")
        if vital is None:
            return common.as_json({
                "set": False,
                "reason": "%s has no GothicVitalPointComponent" % actor.get_name(),
            })

        # C++ declares `bool bDebugDrawVital` (GothicVitalPointComponent.h:197).
        # Unreal's Python bindings strip the leading "b" from BOOL property names
        # before pythonizing (PyGenUtil::PythonizePropertyName), so the reflected
        # name is debug_draw_vital, NOT b_debug_draw_vital. This tool asked for
        # the latter and every call raised -- write and read the same wrong name
        # and nothing ever reported a mismatch.
        prop = "debug_draw_vital"
        try:
            vital.set_editor_property(prop, bool(enabled))
        except Exception as exc:
            return common.as_json({
                "set": False,
                "reason": "set_editor_property('%s') failed on %s: %s: %s"
                          % (prop, actor.get_name(), type(exc).__name__, exc),
                "cpp_property": "bDebugDrawVital (GothicVitalPointComponent.h:197)",
                "hint": "If the property was renamed in C++, fix it here rather "
                        "than letting this tool report a value it never wrote.",
            })

        readback = vital.get_editor_property(prop)
        common.audit("set_vital_debug_draw\t%s\t%s" % (actor.get_name(), bool(enabled)))
        if bool(readback) != bool(enabled):
            # Loud on purpose: a silent no-op here is indistinguishable from a
            # radius that is genuinely invisible, which is the exact confusion
            # this tool exists to remove.
            return common.as_json({
                "set": False,
                "actor": actor.get_name(),
                "requested": bool(enabled),
                "read_back": bool(readback),
                "reason": "Write to '%s' did not stick." % prop,
            })
        return common.as_json({
            "set": True,
            "actor": actor.get_name(),
            "debug_draw": bool(readback),
        })

    @toolset_registry.tool_call
    @staticmethod
    def run_console_command(command: str, force: bool = False) -> str:
        """Execute a console command in the PIE world.

        Prefix-allowlisted by default and every invocation appended to
        Saved/VigilMCP/mutation_log.txt. Set force only when the developer has
        explicitly approved the specific command.

        Args:
            command: The console command, e.g. "stat unit" or "showdebug abilitysystem".
            force: Bypass the allowlist. Requires explicit developer approval.

        Returns:
            JSON confirming execution or explaining the refusal.
        """
        world = common.require_world()
        try:
            common.run_console(world, command, force)
        except PermissionError as exc:
            return common.as_json({
                "executed": False,
                "reason": str(exc),
                "allowlist_prefixes": list(common.CONSOLE_ALLOWLIST),
                "hint": "Do not set force on your own initiative.",
            })
        return common.as_json({
            "executed": True,
            "command": command,
            "forced": bool(force),
            "note": "Console output goes to the editor log, not this result. "
                    "Read Saved/Logs/GothicMMO.log for output.",
        })
