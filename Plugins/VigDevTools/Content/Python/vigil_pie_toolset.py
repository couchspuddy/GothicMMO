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

        THIS TOOL MUST NOT BE ABLE TO FAIL. It is the probe you reach for when
        something else is broken, so a missing API has to come back as a False
        row, never as an exception. The previous version wrote
        `hasattr(unreal.AIBlueprintHelperLibrary, ...)` -- evaluating the
        attribute BEFORE hasattr could catch anything -- and an engine class
        rename took the whole report down along with the tool it was meant to
        diagnose. Every row now goes through _row(), which converts any
        exception into a visible error string in that row alone.

        Returns:
            JSON with api and vigil_classes tables mapping name to availability.
        """
        api = {}

        def _row(label, fn):
            """Evaluate one capability. Records the failure instead of raising."""
            try:
                api[label] = fn()
            except Exception as exc:
                api[label] = "unavailable: %s: %s" % (type(exc).__name__, exc)

        def _has(owner_name, member):
            """hasattr against unreal.<owner_name>, tolerating a missing owner."""
            owner = getattr(unreal, owner_name, None)
            if owner is None:
                return "unavailable: unreal.%s does not exist on this build " \
                       "(suspect a meta=(ScriptName=...) rename)" % owner_name
            return hasattr(owner, member)

        _row("UnrealEditorSubsystem",
             lambda: unreal.get_editor_subsystem(
                 unreal.UnrealEditorSubsystem) is not None)
        _row("UnrealEditorSubsystem.get_game_world",
             lambda: hasattr(unreal.get_editor_subsystem(
                 unreal.UnrealEditorSubsystem), "get_game_world"))
        _row("LevelEditorSubsystem",
             lambda: unreal.get_editor_subsystem(
                 unreal.LevelEditorSubsystem) is not None)
        _row("LevelEditorSubsystem.editor_request_begin_play",
             lambda: hasattr(unreal.get_editor_subsystem(
                 unreal.LevelEditorSubsystem), "editor_request_begin_play"))
        _row("LevelEditorSubsystem.editor_request_end_play",
             lambda: hasattr(unreal.get_editor_subsystem(
                 unreal.LevelEditorSubsystem), "editor_request_end_play"))
        _row("register_slate_post_tick_callback",
             lambda: hasattr(unreal, "register_slate_post_tick_callback"))
        _row("GameplayStatics.get_time_seconds",
             lambda: _has("GameplayStatics", "get_time_seconds"))
        # Expected False, permanently: BeginDeferredActorSpawnFromClass is
        # meta=(BlueprintInternalUseOnly="true") (GameplayStatics.h:65-67)
        # and PyGenUtil.cpp:1621 excludes those from the Python bindings.
        # Kept as a capability row so nobody rediscovers it as a bug.
        _row("GameplayStatics.begin_deferred_actor_spawn_from_class (expected False)",
             lambda: _has("GameplayStatics",
                          "begin_deferred_actor_spawn_from_class"))
        # Expected False, permanently: UAIBlueprintHelperLibrary is renamed to
        # AIHelperLibrary by meta=(ScriptName=...) (AIBlueprintHelperLibrary.h:25).
        # Kept as a row because this exact name is what spawn_enemy failed on
        # twice; seeing it False next to AIHelperLibrary True is the whole
        # explanation.
        _row("unreal.AIBlueprintHelperLibrary (expected False -- ScriptName rename)",
             lambda: hasattr(unreal, "AIBlueprintHelperLibrary"))
        _row("AIHelperLibrary (resolved name for UAIBlueprintHelperLibrary)",
             lambda: common.ai_helper_library().__name__)
        # The route spawn_enemy actually uses.
        _row("AIHelperLibrary.spawn_ai_from_class",
             lambda: hasattr(common.ai_helper_library(), "spawn_ai_from_class"))
        _row("AIHelperLibrary.get_blackboard",
             lambda: hasattr(common.ai_helper_library(), "get_blackboard"))
        _row("AIPerceptionComponent.get_currently_perceived_actors",
             lambda: _has("AIPerceptionComponent",
                          "get_currently_perceived_actors"))
        _row("AISenseConfig_Hearing.hearing_range (readable)",
             lambda: _has("AISenseConfig_Hearing", "hearing_range"))
        _row("MathLibrary.find_look_at_rotation",
             lambda: _has("MathLibrary", "find_look_at_rotation"))

        # --- Weapon trace channel resolution -------------------------------
        # aim_at_vital and every ranged measurement run GA_Fire's own
        # LineTraceSingleByChannel(ECC_Weapon) (GA_Fire.cpp:257). Getting the
        # ETraceTypeQuery for that channel took two failed investigations, so
        # every link in the chain is a row here.
        #
        # Expected False, permanently: UCollisionProfile is not exported to
        # Python on ANY build. It has no BlueprintType
        # (CollisionProfile.h:159) and no script-exposed field -- every
        # UPROPERTY on it is a bare globalconfig, and it declares no
        # UFUNCTIONs -- so PyGenUtil::ShouldExportClass (PyGenUtil.cpp:1769)
        # rejects it. This is NOT a ScriptName rename; do not go looking for
        # another spelling. The mapping comes from DefaultEngine.ini instead.
        _row("unreal.CollisionProfile (expected False -- never exported, "
             "not a rename)",
             lambda: hasattr(unreal, "CollisionProfile"))
        _row("unreal.TraceTypeQuery members",
             lambda: sorted(n for n in dir(unreal.TraceTypeQuery)
                            if not n.startswith("_")))
        _row("Config/DefaultEngine.ini (the trace-type source of truth)",
             lambda: common.collision_channel_table()["config_file"])
        _row("custom collision channels",
             lambda: [(e["name"], e["channel"], e["trace"])
                      for e in common.collision_channel_table()["channels"]])
        # Expect ["Visibility", "Camera", "Weapon"] on this project: Weapon is
        # the only custom channel with bTraceType=True (DefaultEngine.ini:219);
        # ArenaBlock is bTraceType=False (:220) and so is NOT traceable by
        # ETraceTypeQuery at all.
        _row("trace types in ETraceTypeQuery order",
             lambda: common.collision_channel_table()["trace_types"])
        _row("Weapon trace type (what aim_at_vital fires on)",
             lambda: common.trace_type_for_channel("Weapon")[1])
        # The binding SHAPE, not just the channel. KismetSystemLibrary.h:1270
        # declares `bool LineTraceSingle(..., FHitResult& OutHit, ...)`, which
        # implies Python yields (bool, FHitResult) -- and on this build it does
        # not, it yields a bare FHitResult. Assuming the header cost a whole
        # verification run (six raised calls) and left the AI LOS readout
        # silently reporting blocked=True forever. Observed, not declared:
        # requires PIE, and says so when there is no world.
        _row("line_trace_single return shape (OBSERVED -- header is not "
             "authoritative; needs PIE)",
             lambda: common.probe_trace_return_shape())

        # --- Inventory equip/unequip -----------------------------------
        # equip_item and unequip_slot are the only actions that exercise the
        # equipment code (OnItemEquipped, OnEquipmentChanged's slot-clearing
        # branch, the Server RPC round trip). Every binding they lean on is a
        # row, because three of them are "expected False" for structural
        # reasons that look exactly like breakage if rediscovered cold.
        _row("GothicInventoryComponent.equip_item",
             lambda: _has("GothicInventoryComponent", "equip_item"))
        _row("GothicInventoryComponent.unequip_slot",
             lambda: _has("GothicInventoryComponent", "unequip_slot"))
        _row("GothicInventoryComponent.get_all_items",
             lambda: _has("GothicInventoryComponent", "get_all_items"))
        _row("GothicInventoryComponent.debug_spawn_test_items",
             lambda: _has("GothicInventoryComponent", "debug_spawn_test_items"))
        # Expected False, permanently: GetEquippedItem carries no UFUNCTION
        # macro (GothicInventoryComponent.h:130-131). The EquippedItems
        # UPROPERTY is what the harness reads instead -- C++ `protected` does
        # not hide a UPROPERTY from Python.
        _row("GothicInventoryComponent.get_equipped_item (expected False -- "
             "no UFUNCTION macro; read the equipped_items UPROPERTY instead)",
             lambda: _has("GothicInventoryComponent", "get_equipped_item"))
        # Expected False, permanently: also no UFUNCTION macro
        # (GothicInventoryComponent.h:76-77). Authority is read off the owning
        # actor's HasAuthority instead.
        _row("GothicInventoryComponent.has_inventory_authority (expected "
             "False -- no UFUNCTION; use owner.has_authority())",
             lambda: _has("GothicInventoryComponent", "has_inventory_authority"))
        # Expected False, permanently, and it is the reason no harness action
        # can grant an authored weapon: UGothicItemDefinition::RollInstance has
        # no UFUNCTION (GothicItemDefinition.h:132-138), AGothicWorldPickup::
        # InitializePickup has none either (GothicWorldPickup.h:26-27), and
        # every FGothicItemInstance field is BlueprintReadOnly
        # (GothicItemTypes.h:139-180) so the struct cannot be filled from
        # Python. To get DA_Weapon_HeavyMeleeRig into a test inventory it must
        # be added to StartingItemDefs on the PlayerState BP -- editor-side.
        _row("GothicItemDefinition.roll_instance (expected False -- no "
             "UFUNCTION; NOTHING in Python can mint an item instance)",
             lambda: _has("GothicItemDefinition", "roll_instance"))
        _row("EGothicEquipSlot entries (GENERATED names, not declared)",
             lambda: {n: int(e.value)
                      for n, e in sorted(common.equip_slot_table().items())})
        # FGuid exports A/B/C/D (Misc/Guid.h) and that is how items are
        # addressed; if this row breaks, instance_id addressing breaks with it.
        _row("FGuid A/B/C/D readable (how items are addressed)",
             lambda: all(hasattr(unreal.Guid(), f) for f in "abcd"))

        vigil = {}
        for name in (
            "GothicCharacterBase", "GothicPlayerCharacter", "GothicEnemyBase",
            "GothicAbilitySystemComponent", "GothicAttributeSet",
            "GothicSteadfastComponent", "GothicVitalPointComponent",
            "GothicCombatStateComponent", "GothicPackSubsystem",
            "GothicAbilitySlot", "GothicInventoryComponent",
            "GothicItemDefinition", "GothicPlayerState",
        ):
            vigil[name] = hasattr(unreal, name)

        return common.as_json({
            "api": api,
            "vigil_classes": vigil,
            "probe_metrics": common.try_read(probe.available_metrics),
            "in_pie": common.try_read(lambda: common.pie_world() is not None),
            "note": "Any False or 'unavailable: ...' under api means the tool "
                    "depending on it returns a clear error rather than wrong "
                    "data. An 'unavailable' string naming an AttributeError on "
                    "a class is almost always a meta=(ScriptName=...) rename, "
                    "not a removed feature.",
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
    def list_combatants(world: str = "server") -> str:
        """List every pawn in one PIE world with a one-line combat summary.

        Cheap orientation call. The returned names are the actor labels every
        other tool takes, here and in VigilCombatDrive -- but LABELS DIFFER
        PER WORLD: the same name picks out a different pawn on the server
        world than on a client world (see resolve_world in vigil_pie_common).
        Always resolve and act within the same world.

        On a client world, the local player's pawn is the one whose `role`
        reads "ROLE_AutonomousProxy". Every other pawn on that world is a
        replicated copy, not something local input can drive.

        Args:
            world: "server" (default, preserves prior behaviour byte-for-byte),
                "client"/"client2"/..., a bare PIE instance index, or a
                "UEDPIE_<n>_" spelling. See vigil_pie_common.resolve_world.

        Returns:
            JSON array of pawns with health, alive state, role/remote_role,
            and which Gothic components each one has.
        """
        resolved_world = common.resolve_world(world)
        rows = []
        for pawn in common.all_pawns(resolved_world):
            rows.append({
                "name": pawn.get_name(),
                "class": common.try_read(lambda p=pawn: p.get_class().get_name()),
                "health": common.try_read(lambda p=pawn: round(float(p.get_health()), 1)),
                "max_health": common.try_read(
                    lambda p=pawn: round(float(p.get_max_health()), 1)),
                "alive": common.try_read(lambda p=pawn: bool(p.is_alive())),
                # Raw, not interpreted: on a client world the local pawn is
                # AutonomousProxy, not Authority -- the caller judges which
                # role means "mine" for the world it asked about.
                "role": common.try_read(lambda p=pawn: str(p.get_local_role())),
                "remote_role": common.try_read(lambda p=pawn: str(p.get_remote_role())),
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
            metrics: Comma-separated metric keys. See capabilities for the
                full list -- it is generated from the readers, so it is always
                current. For crowd control, sample `tags`: it is the only
                column that can show a State.* tag appearing and expiring, and
                nothing else in this toolset can observe a gameplay tag at
                runtime.
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
