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


# Vertical separation (uu) past which a spawn is called out as cross-floor.
#
# 180 sits above the largest same-floor origin dZ a spawn can innocently
# produce -- actor locations are capsule centres, and the biggest half-height
# gap in the project is player 88 vs Bestial Lucid 253 = 165 -- and below the
# ~200 of one Rotunda storey. It is a HARNESS heuristic on origin dZ, not the
# gameplay rule: the melee gate itself compares capsule spans
# (AGothicEnemyAIController::IsTargetInAttackRange). Deliberately noisy rather
# than silent; a false warning costs a glance, a missed one costs a diagnosis.
_CROSS_FLOOR_DZ = 180.0


def _live_player_pawns(world):
    """Every pawn in THIS world that a PlayerController currently possesses.

    Deliberately NOT GameplayStatics.get_player_pawn(world, 0), which is what
    the cross-floor check first shipped with and what made it lie: it reported
    dz=200 against z~=90 -- the untouched default-spawn region -- for a pawn
    standing on the same floor as both live players at z=290.2. Whatever that
    route handed back was not a pawn either player was standing in.

    Enumerating the world's own pawns and asking each one for its controller is
    world-scoped by construction, so it cannot reach across PIE instances, and
    it reads possession live rather than by player index -- which matters here
    because respawn re-possesses (see the pawn/PlayerState split: the ASC lives
    on the PlayerState and outlives the pawn).

    Pawns that read is_alive() == False are dropped; a corpse is not a floor
    reference. An unreadable is_alive() is NOT treated as dead -- the pawn stays
    a candidate rather than silently vanishing from the comparison.
    """
    out = []
    for pawn in common.all_pawns(world):
        controller = common.try_read(lambda p=pawn: p.get_controller())
        if not isinstance(controller, unreal.PlayerController):
            continue
        if common.try_read(lambda p=pawn: bool(p.is_alive()), default=True) is False:
            continue
        out.append(pawn)
    return out


def _nearest_live_player(world, location):
    """(pawn, pawn_location, distance, candidate_count) nearest `location`.

    Nearest by 3D distance, so a co-op pair split across two storeys is
    referenced against whichever one the spawn actually landed near. The count
    goes back in the payload so a reader can tell "one player, obviously that
    one" from "picked one of three".

    Returns (None, None, None, 0) when no live player pawn resolves in this
    world, which the caller must report as an absence -- never as a dz of zero.
    """
    best = (None, None, None)
    candidates = _live_player_pawns(world)
    for pawn in candidates:
        pawn_loc = common.try_read(lambda p=pawn: p.get_actor_location())
        if pawn_loc is None:
            continue
        dist = ((pawn_loc.x - location.x) ** 2
                + (pawn_loc.y - location.y) ** 2
                + (pawn_loc.z - location.z) ** 2) ** 0.5
        if best[2] is None or dist < best[2]:
            best = (pawn, pawn_loc, dist)
    return best + (len(candidates),)


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

    MULTIPLAYER: steps default to the server world, and any step taking an
    "actor" or "target" accepts "world": "client" to act on a second PIE
    instance instead -- for READS, aiming, and movement. ABILITY ACTIVATION ON A
    CLIENT WORLD IS REFUSED (fire, melee, activate_slot): editor Python forces a
    Local callspace on every RPC, so a non-authoritative activation re-enters
    itself through the in-process ServerTryActivateAbility and stack-overflows
    the editor. Drive every player's abilities on the SERVER world. See
    scenario_actions.
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

        Every step that takes an "actor" or a "target" also accepts an optional
        "world" (server|client|client2) -- for reads, aiming and movement. It is
        NOT an ability-activation route; see the world_field entry in the result.

        Returns:
            JSON listing action names, required fields, and a worked example.
        """
        return common.as_json({
            "actions": driver.available_actions(),
            "world_field": {
                "field": "world",
                "accepts": "server (default) | client | client2 | a PIE index",
                "applies_to": "every action taking an 'actor' or a 'target'",
                "why": "Reads, aiming and movement against a second PIE instance. "
                       "Use it to observe a client's local view of state.",
                "ABILITY_ACTIVATION_IS_REFUSED": "fire, melee and activate_slot "
                       "refuse a non-authoritative pawn and return "
                       "refused='non-authoritative-world' WITHOUT calling the ASC. "
                       "Editor Python runs under FEditorScriptExecutionGuard, which "
                       "sets GAllowActorScriptExecutionInEditor, and "
                       "AActor::GetFunctionCallspace then returns Local for every "
                       "RPC before any net-role test -- so the LocalPredicted path's "
                       "ServerTryActivateAbility executes IN PROCESS, re-enters "
                       "InternalTryActivateAbility still non-authoritative, and "
                       "recurses unboundedly in one frame: a dependent prediction "
                       "key per lap, ~200MB/min of log, then a stack overflow that "
                       "kills the editor. Real clients are unaffected (the flag is "
                       "false and the RPC goes over the wire). Activate every "
                       "player's abilities on the SERVER world -- that pawn's label "
                       "differs, so get it from list_combatants(world=\"server\").",
                "labels_are_per_world": "Actor names are assigned per PIE "
                                        "instance. '_C_0' is the HOST's pawn on "
                                        "the server world and the LOCAL PLAYER's "
                                        "pawn on a client world -- same label, "
                                        "different pawn. Never carry a name from "
                                        "one world's list_combatants into a step "
                                        "addressing another world.",
                "not_authoritative": "A client world is a replicated copy. Reads "
                                     "there may be stale and writes there are "
                                     "local predictions. Observe there; act on "
                                     "the server world.",
                "pattern": "activate on the server world; use a client world only "
                           "to see what that client believes",
            },
            "fields": {
                "fire": {"actor": "str"},
                "melee": {"actor": "str"},
                "reload": {"actor": "str"},
                "convert_steadfast": {"actor": "str"},
                "set_steadfast": {
                    "actor": "str (player)", "value": "float, 0..MaxSteadfast",
                    "_note": "DEV ONLY, authority world only. The sink the game "
                             "does not have: forces Steadfast to a chosen value "
                             "so the 0/1/2-pip bands can be driven. Drains via "
                             "TryConvertSteadfast(delta, 0.0) -- no ammo is "
                             "granted -- and raises by writing the attribute "
                             "data directly. Raises if the readback disagrees.",
                },
                "swap_weapon": {
                    "actor": "str", "index": "int",
                    "_note": "Moves ActiveWeaponIndex only. Does NOT touch the "
                             "inventory, so it exercises no equipment code -- "
                             "use equip_item/unequip_slot for that.",
                },
                "equip_item": {
                    "actor": "str (player)",
                    "instance_id": "str, 32 hex digits (from inventory_snapshot; "
                                   "null there means this build will not reflect "
                                   "the FGuid -- use 'definition')",
                    "definition": "str, asset path or bare name -- use INSTEAD of "
                                  "instance_id, and PREFER it: this path resolves "
                                  "the item without reading an instance GUID at "
                                  "all",
                    "_note": "Real UGothicInventoryComponent::EquipItem, the call "
                             "the inventory UI makes. The slot comes from the "
                             "item definition and is not a parameter. On a CLIENT "
                             "the returned bool means 'request sent', not "
                             "success -- read 'authority' and re-check with "
                             "inventory_snapshot on a later step.",
                },
                "unequip_slot": {
                    "actor": "str (player)",
                    "slot": "str name (Sidearm|Piece|Rig|Head|Neck|Chest|Back|"
                            "LeftArm|RightArm|Wrist|LeftLeg|RightLeg|Feet) or "
                            "raw value (0-2, 10-19)",
                    "_note": "The only action that reaches OnEquipmentChanged's "
                             "slot-clearing branch. Returning false is a "
                             "DOCUMENTED outcome (slot empty, or inventory at "
                             "MaxInventorySize) and arrives as a 'refusal' "
                             "field, not an error.",
                },
                "inventory_snapshot": {
                    "actor": "str (player)",
                    "_note": "Read-only. Source of instance_ids, and the way to "
                             "assert an equip actually landed. Every inventory "
                             "result also carries 'resolver_route' -- which "
                             "PlayerState route reached the inventory.",
                },
                "grant_test_items": {
                    "actor": "str (player)",
                    "_note": "DebugSpawnTestItems: ten transient ARMOR items. "
                             "Authority-only. Cannot grant a named weapon "
                             "definition -- nothing in Python can.",
                },
                "activate_slot": {"actor": "str",
                                  "slot": "LIGHT_ATTACK|HEAVY_ATTACK|ABILITY1|"
                                          "ABILITY2|ABILITY3|SUPER_ABILITY|PRIMARY_FIRE"},
                "trigger_selah": {"actor": "str"},
                "aim_at": {"actor": "str", "target": "str"},
                "aim_at_vital": {"actor": "str", "target": "str"},
                "set_combat_target": {"actor": "str (enemy)", "target": "str"},
                "freeze_vital": {"actor": "str", "index": "int, -1 = freeze in place"},
                "damage_vital": {"actor": "str", "amount": "float"},
                "apply_damage": {
                    "actor": "str (the victim)",
                    "amount": "float, RAW damage before Defense and evasion",
                    "instigator": "str, optional -- the attacker's label",
                    "effect": "str, optional GE class path",
                    "_note": "Without 'instigator' the victim damages ITSELF. "
                             "With one named, the spec is built from the "
                             "instigator's ASC instead of the target's own; "
                             "whether AttackPower/retaliation result depends "
                             "on the game-side resolution and the "
                             "instigator's stats -- verify, don't assume.",
                },
                "kill": {
                    "actor": "str", "margin": "float (optional, default 1000)",
                    "_note": "Current health plus a margin through the same "
                             "damage pipeline. Self-damage: no instigator, so "
                             "an on-kill system that reads the killer sees "
                             "none. Use apply_damage with an instigator when "
                             "the killer matters.",
                },
                "console": {"command": "str", "force": "bool (optional)"},
                "mark": {"label": "str"},
                "move_to": {
                    "actor": "str", "x": "float", "y": "float", "z": "float",
                    "accept_radius": "float (optional, default 150)",
                    "timeout": "float game seconds (optional, default 30)",
                    "_note": "Real locomotion via add_movement_input, so collision, "
                             "navmesh and Bleed gates all apply. Sustained across "
                             "ticks; the scenario stays alive until the walk ends. "
                             "Outcome lands in results as a 'move_to result' record: "
                             "arrived, blocked, or timed out.",
                },
                "stop_move": {"actor": "str"},
                "teleport": {
                    "actor": "str", "x": "float", "y": "float", "z": "float",
                    "yaw": "float (optional)",
                    "_note": "Instant, and bypasses collision. For positioning only "
                             "-- it passes straight through a Bleed gate, so it can "
                             "never tell you whether a barrier holds. Use move_to.",
                },
            },
            "example": [
                {"at": 0.0, "do": "freeze_vital", "actor": "Feral", "index": 2},
                {"at": 0.0, "do": "aim_at_vital", "actor": "Player", "target": "Feral"},
                {"at": 0.5, "do": "fire", "actor": "Player"},
                {"at": 0.9, "do": "fire", "actor": "Player"},
                {"at": 1.3, "do": "mark", "label": "burst_complete"},
                {"at": 2.0, "do": "convert_steadfast", "actor": "Player"},
            ],
            "multiplayer_example": [
                {"at": 0.0, "do": "aim_at", "actor": "BP_GothicCharacter_C_1",
                 "target": "Feral"},
                {"at": 0.4, "do": "fire", "actor": "BP_GothicCharacter_C_1"},
                {"at": 1.0, "do": "mark", "label": "p2_fired"},
            ],
            "multiplayer_example_note": "Second player driven on the SERVER "
                                        "world, where _C_1 is their pawn (_C_0 is "
                                        "the host). Never fire with world=client: "
                                        "the activation is refused, and it is "
                                        "refused because running it would recurse "
                                        "and kill the editor. Get the label from "
                                        "list_combatants(world=\"server\") -- it "
                                        "churns on respawn.",
            "note": "Actor labels are pawn names from VigilPIETools.list_combatants. "
                    "A unique substring works, and it is resolved in the step's "
                    "'world' (server unless stated).",
        })

    @toolset_registry.tool_call
    @staticmethod
    def scenario_validate(steps_json: str) -> str:
        """Check a scenario script without running it.

        Cheap dry run: verifies JSON shape, action names, timing fields, and
        the spelling of any "world" field. It does NOT resolve actor labels,
        because actors may not exist until the scenario spawns them, and it
        does not check that a requested PIE instance is actually running --
        that answer only exists at execution time.

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

        SERVER WORLD ONLY, and non-authoritative pawns are refused. Activating an
        ability on a non-authoritative ASC from editor Python recurses through the
        in-process ServerTryActivateAbility (the editor script guard forces a Local
        callspace on every RPC) and stack-overflows the editor. This tool resolves
        against the server world, so the refusal below should never fire -- it is
        there so a future world argument cannot reintroduce the crash.

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
        if not bool(pawn.has_authority()):
            return common.as_json({
                "actor": pawn.get_name(),
                "slot": slot.upper(),
                "activated": False,
                "refused": "non-authoritative-world",
                "reason": "Refused without calling the ASC: activating an ability "
                          "on a non-authoritative pawn from editor Python recurses "
                          "through the in-process ServerTryActivateAbility and "
                          "stack-overflows the editor. Use the server world's pawn "
                          "for this player (list_combatants(world=\"server\")).",
            })
        slot_enum = common.ability_slot(slot)
        common.audit("force_ability\t%s\t%s" % (pawn.get_name(), slot))
        locally_controlled = common.try_read(
            lambda: bool(pawn.is_locally_controlled()))
        activated = bool(asc.try_activate_ability_by_slot(slot_enum))

        if activated and locally_controlled is False:
            local_predicted = common.try_read(
                lambda: bool(asc.is_slot_ability_locally_predicted(slot_enum)))
            if local_predicted is True:
                # GAS answered true without activating anything -- see _activate
                # in vigil_combat_driver for the full mechanism.
                return common.as_json({
                    "actor": pawn.get_name(),
                    "slot": slot.upper(),
                    "activated": False,
                    "gas_returned": True,
                    "refused": "local-predicted-on-remote-pawn",
                    "reason": "NOTHING RAN. This pawn is authoritative but not "
                              "locally controlled, and the slot holds a "
                              "LocalPredicted ability. TryActivateAbility fires "
                              "ClientTryActivateAbility and returns true "
                              "unconditionally in that case "
                              "(AbilitySystemComponent_Abilities.cpp:1621-1627), "
                              "and under the editor script guard the RPC never "
                              "leaves the process. There is no editor-Python route "
                              "for second-player ability activation today: the "
                              "client world would recurse, the server world is "
                              "refused by GAS design.",
                })

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
                    yaw: float = 0.0,
                    no_collision_fail: bool = True) -> str:
        """Spawn a PAWN into the running PIE world at a fixed transform.

        Fixed transforms are the cheapest determinism you get -- an encounter
        that starts from the same geometry every run is comparable between runs
        even when AI behaviour is not.

        A fresh spawn is also the only clean read of a class's INTENDED config:
        placed instances freeze their overrides at placement and drift from the
        C++ constructor. Spawn one next to a placed one and diff them before
        trusting any config value read off a placed enemy.

        Routes through UAIBlueprintHelperLibrary::SpawnAIFromClass, which also
        spawns and possesses the default controller. Pawn classes only -- see
        vigil_combat_driver.spawn for why the deferred-spawn route is
        permanently unavailable from Python.

        Blueprint class paths need the _C suffix, e.g.
        "/Game/Blueprints/Enemies/BP_Enemy_FeralRetained.BP_Enemy_FeralRetained_C"

        Everything spawned is tracked; scenario_cleanup destroys it all.

        Args:
            class_path: Full object path to the class, with _C for Blueprints.
            x: World X.
            y: World Y.
            z: World Z. Spawn above the floor; capsules resolve downward.
            yaw: Facing in degrees.
            no_collision_fail: True spawns even when the capsule overlaps
                geometry. False refuses the spawn on any blocking overlap, which
                is how you find out a spawn point is inside a wall.

        Returns:
            JSON with the spawned actor's name, to use as an actor label, plus
            where the pawn ACTUALLY landed and how it sits relative to the
            player vertically.

            `location_requested` is what you asked for; `location_spawned` is
            where the pawn is once the spawn resolved, which already differs
            when depenetration pushed the capsule out of geometry. It is read
            in the same frame -- this toolset never blocks a tick -- so gravity
            has NOT settled yet and a pawn spawned in the air still reads at
            its requested Z. Re-read the location from a probe a beat later if
            the settled floor is what you need.

            `dz_to_player` is the vertical separation from the NEAREST live
            player pawn in the spawn world, measured at spawn time, and
            `dz_reference` names that player and gives both Z values so the
            number can be re-derived by hand instead of trusted. When no live
            player resolves -- before possession completes, or with everyone
            dead -- `dz_to_player` and `warning` are both absent and
            `dz_unavailable` says why; the check never falls back to a stale or
            default-spawn reference, which is exactly how it once reported a
            same-floor spawn as cross-floor.

            `warning` fires past _CROSS_FLOOR_DZ. That tripwire exists because a
            fight staged across two storeys looks completely normal in every
            other reading -- the enemy aggroes, closes horizontally, and plays
            its attack -- while landing nothing, and a full diagnosis pass has
            already been spent on exactly that setup.
        """
        world = common.require_world()
        actor = driver.spawn(world, class_path, (x, y, z), (0.0, yaw, 0.0),
                             no_collision_fail=no_collision_fail)

        spawned_loc = common.try_read(lambda: actor.get_actor_location())

        dz = None
        dz_reference = None
        dz_unavailable = None
        if spawned_loc is None:
            dz_unavailable = (
                "The spawned pawn's location could not be read, so there is "
                "nothing to difference against. No cross-floor judgement made.")
        else:
            player, player_loc, distance, considered = _nearest_live_player(
                world, spawned_loc)
            if player is None:
                dz_unavailable = (
                    "No live player pawn resolves in the spawn world (%s), so "
                    "dz_to_player and the cross-floor check are both omitted "
                    "rather than guessed. This is the expected reading before "
                    "possession completes or after every player has died."
                    % common.world_path(world))
            else:
                dz = round(spawned_loc.z - player_loc.z, 1)
                dz_reference = {
                    "player": player.get_name(),
                    "player_z": round(player_loc.z, 1),
                    "spawn_z": round(spawned_loc.z, 1),
                    "distance_3d": round(distance, 1),
                    "players_considered": considered,
                    "world": common.world_path(world),
                }

        payload = {
            "spawned": actor.get_name(),
            "class": common.try_read(lambda: actor.get_class().get_name()),
            "location_requested": [x, y, z],
            "location_spawned": common.vec(spawned_loc)
                                if spawned_loc is not None else None,
            # Retained under its original key so existing callers and scenario
            # scripts that read `location` keep working.
            "location": [x, y, z],
            "dz_to_player": dz,
            # Shows its work: which player the number was measured against and
            # both Z values, so dz can be re-derived by hand from the payload.
            "dz_reference": dz_reference,
            "controller": common.try_read(
                lambda: actor.get_controller().get_name()),
            "note": "Use the spawned name as an actor label in scenarios.",
        }

        if dz_unavailable is not None:
            payload["dz_unavailable"] = dz_unavailable

        if dz is not None and abs(dz) > _CROSS_FLOOR_DZ:
            payload["warning"] = (
                "CROSS-FLOOR SPAWN: this pawn is %.1fuu above/below %s "
                "(threshold %.0f). Melee cannot connect between storeys and the "
                "enemy will close horizontally and whiff forever. Confirm this "
                "is deliberate before reading anything into the encounter."
                % (dz, dz_reference["player"], _CROSS_FLOOR_DZ))

        return common.as_json(payload)

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
