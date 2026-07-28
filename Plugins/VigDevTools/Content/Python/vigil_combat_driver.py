"""
vigil_combat_driver.py

Tick-driven scenario executor. This is the thing that actually drives combat.

WHY A SCRIPT AND NOT COMMANDS
-----------------------------
The obvious design is one tool call per action: fire, then fire, then convert.
It technically works, and it is wrong. Every action would land at the mercy of
MCP round-trip latency -- roughly 200-400ms of jitter between steps -- which is
useless for anything timing-sensitive like a hit window, a recovery frame, or a
Steadfast hold duration.

So a scenario is submitted whole, in one instantaneous call, and executed by a
tick callback against game time. Same architecture as the probe, for the same
reason: tool calls run on the game thread and cannot wait.

WHAT THIS DELIBERATELY DOES NOT DO
----------------------------------
It does not synthesize keyboard or mouse input. Every action here calls a
BlueprintCallable function directly, which bypasses Enhanced Input entirely.
That is a feature: it isolates combat logic from input configuration. If an
action works when driven here but fails when you press the key, the bug is in
the Input Action asset, not the gameplay code.

It is also why this harness cannot test input itself. Do not add that here.
"""

import math
import time

import unreal

import vigil_pie_common as common

# --------------------------------------------------------------------------
# Module state
# --------------------------------------------------------------------------
_tick_handle = None
_steps = []
_results = []
_world = None
_game_origin = None
_wall_origin = None
_spawned = []          # actors this harness created, for cleanup
_stop_reason = None
_max_duration = 60.0
_grace = 1.0           # keep ticking briefly after the last step
_error_streak = 0
_MAX_ERROR_STREAK = 5

_ACTIONS = {}

# Active move_to orders, keyed by pawn name. Driven every tick by _drive_movers.
# Movement has to be sustained rather than fired once: add_movement_input feeds
# CharacterMovement for a single frame, so a one-shot call moves a pawn by
# millimetres. Keeping the order here and re-applying it each tick is what makes
# a scenario able to walk somewhere -- and, because it goes through
# CharacterMovement rather than set_actor_location, what makes it collide with
# geometry and with an AGothicBleedGate exactly as a player does.
_movers = {}

_MOVE_ACCEPT_RADIUS = 150.0   # uu; capsule-ish, so "arrived" isn't pixel-exact
_MOVE_TIMEOUT = 30.0          # give up on an order after this many game seconds
_MOVE_STALL_SECONDS = 2.0     # no net progress for this long => report blocked
_MOVE_STALL_EPSILON = 25.0    # uu of closing distance that counts as progress


def _action(name):
    def deco(fn):
        _ACTIONS[name] = fn
        return fn
    return deco


def available_actions():
    return sorted(_ACTIONS.keys())


# --------------------------------------------------------------------------
# Actions. Signature: fn(world, step) -> JSON-serialisable result.
# Each one raises on failure; the executor records the exception per step.
# --------------------------------------------------------------------------

def _aim_origin(pawn):
    """World location GA_Fire actually traces from.

    GA_Fire uses FindComponentByClass<UCameraComponent>() as its trace start.
    Building a look-at rotation from pawn.get_actor_location() (the capsule
    centre) and applying it to that camera aims high -- roughly 11 degrees at
    300uu, which clears the target's head entirely. Aim from the same point the
    trace starts from.
    """
    camera = common.component(pawn, "CameraComponent")
    if camera is not None:
        return camera.get_world_location()
    return pawn.get_actor_location()


def _actor(world, step, key="actor"):
    label = step.get(key)
    if not label:
        raise ValueError("step is missing required field '%s'" % key)
    return common.resolve_actor(world, label)


def _activate(pawn, slot_name):
    """Shared slot activation. OnFire/OnMelee used to exist as direct C++ calls
    and were deleted as duplicate damage paths -- they applied damage in
    ADDITION to the ability, on an ECC_Pawn capsule trace with no authority
    check. Everything now goes through the ability, which is also what input
    drives, so driving the slot exercises the real path."""
    asc = pawn.get_gothic_asc()
    if asc is None:
        raise LookupError("no ASC on %s" % pawn.get_name())
    return bool(asc.try_activate_ability_by_slot(common.ability_slot(slot_name)))


@_action("fire")
def _a_fire(world, step):
    """Fire the equipped weapon. Runs the real trace and damage path."""
    pawn = _actor(world, step)
    return {"fired": _activate(pawn, "PRIMARY_FIRE")}


@_action("melee")
def _a_melee(world, step):
    pawn = _actor(world, step)
    return {"melee": _activate(pawn, "LIGHT_ATTACK")}


@_action("reload")
def _a_reload(world, step):
    pawn = _actor(world, step)
    return {"reloaded": bool(pawn.reload_active_weapon())}


@_action("convert_steadfast")
def _a_convert(world, step):
    """Convert Steadfast to reserve ammo WITHOUT going through input.

    This is the bisection for the hold-to-convert bug. If this succeeds while
    holding the key does not, the defect is in the IA_Reload trigger config.
    """
    pawn = _actor(world, step)
    return {"converted": bool(pawn.convert_steadfast_to_reserve())}


@_action("swap_weapon")
def _a_swap(world, step):
    pawn = _actor(world, step)
    index = int(step.get("index", 0))
    pawn.swap_weapon(index)
    return {"swapped_to": index}


@_action("activate_slot")
def _a_activate(world, step):
    """Activate an ability by slot through the ASC, bypassing input."""
    pawn = _actor(world, step)
    asc = pawn.get_gothic_asc()
    if asc is None:
        raise LookupError("no ASC on %s" % pawn.get_name())
    slot = common.ability_slot(step.get("slot", "ABILITY1"))
    return {"activated": bool(asc.try_activate_ability_by_slot(slot))}


DEFAULT_DAMAGE_EFFECT = "/Game/Data/Effects/GE_Damage.GE_Damage_C"
DAMAGE_SETBYCALLER_TAG = "Data.Damage"


def _setbycaller_tag(effect_class):
    """The SetByCaller tag this effect actually declares, read from its CDO.

    FGameplayTag cannot be built from a string in Python: the keyword ctor is
    rejected, the positional form routes to MakeLiteralGameplayTag (which wants
    a tag, not a name, and reports "Struct has 0 initialization parameters"),
    and TagName is read-only so set_editor_property fails too.

    So instead of manufacturing a tag, take the live one off the effect we are
    about to apply -- Modifiers[N].ModifierMagnitude.SetByCallerMagnitude.DataTag.
    That is strictly better than hardcoding "Data.Damage": it cannot drift from
    the asset, and it makes this work for any SetByCaller-driven effect.
    """
    cdo = unreal.get_default_object(effect_class)
    modifiers = cdo.get_editor_property("modifiers") or []

    for modifier in modifiers:
        magnitude = modifier.get_editor_property("modifier_magnitude")
        if magnitude is None:
            continue
        set_by_caller = magnitude.get_editor_property("set_by_caller_magnitude")
        if set_by_caller is None:
            continue
        tag = set_by_caller.get_editor_property("data_tag")
        if tag is not None and unreal.GameplayTagLibrary.is_gameplay_tag_valid(tag):
            return tag

    raise RuntimeError(
        "%s declares no SetByCaller tag on any modifier -- nothing to assign a "
        "magnitude to" % effect_class.get_name())


def _apply_damage(target, amount, effect_path):
    """Push damage through the REAL pipeline: GE_Damage carrying a Data.Damage
    SetByCaller, applied to the target's own ASC.

    Deliberately not a health poke. GothicAttributeSet::PostGameplayEffectExecute
    is what converts IncomingDamage into a health change, and it also rolls
    evasion and subtracts Defense on the way through. Writing Health directly
    would skip all of that and prove nothing about the systems under test.

    Because mitigation applies, `amount` is RAW damage, not the health delta:
    a Thrall with 8 Defense loses (amount - 8). Pass generously when the intent
    is simply to kill.
    """
    asc = target.get_gothic_asc()
    if asc is None:
        raise LookupError("no GothicAbilitySystemComponent on %s" % target.get_name())

    effect_class = unreal.load_class(None, effect_path)
    if effect_class is None:
        raise LookupError("could not load damage effect '%s'" % effect_path)

    # Binding names are NOT the C++ names here:
    #   UAbilitySystemBlueprintLibrary declares meta=(ScriptName="AbilitySystemLibrary"),
    #   so Python sees unreal.AbilitySystemLibrary.
    #   The spec apply lives on the ASC itself as BP_ApplyGameplayEffectSpecToTarget,
    #   exposed via ScriptName as apply_gameplay_effect_spec_to_target.
    context = asc.make_effect_context()
    spec = asc.make_outgoing_spec(effect_class, 1.0, context)
    spec = unreal.AbilitySystemLibrary.assign_tag_set_by_caller_magnitude(
        spec, _setbycaller_tag(effect_class), float(amount))
    asc.apply_gameplay_effect_spec_to_target(spec, asc)


@_action("apply_damage")
def _a_apply_damage(world, step):
    """Damage a target without needing to aim.

    Scripted marksmanship is unreliable against a moving target: control
    rotation set this tick does not reach the camera until the next frame, and
    GA_Fire traces from the camera. This drives the damage path directly so
    encounter, wave and death-chain tests do not depend on hitting anything.
    """
    target = _actor(world, step)
    amount = float(step.get("amount", 25.0))
    effect_path = step.get("effect", DEFAULT_DAMAGE_EFFECT)

    before = common.try_read(lambda: float(target.get_health()))
    _apply_damage(target, amount, effect_path)
    after = common.try_read(lambda: float(target.get_health()))

    return {
        "target": target.get_name(),
        "raw_amount": amount,
        "health_before": before,
        "health_after": after,
        "alive": common.try_read(lambda: bool(target.is_alive())),
        "note": "raw damage; Defense and evasion still apply",
    }


@_action("kill")
def _a_kill(world, step):
    """Reduce a target to zero health through the damage pipeline.

    Sends current health plus a margin so mitigation cannot leave a sliver.
    Evasion can still cause a miss, so the result reports whether it landed
    rather than assuming it did.
    """
    target = _actor(world, step)
    effect_path = step.get("effect", DEFAULT_DAMAGE_EFFECT)

    health = common.try_read(lambda: float(target.get_health()))
    margin = float(step.get("margin", 1000.0))
    amount = (health if health is not None else 0.0) + margin

    _apply_damage(target, amount, effect_path)
    after = common.try_read(lambda: float(target.get_health()))

    return {
        "target": target.get_name(),
        "sent": amount,
        "health_before": health,
        "health_after": after,
        "alive": common.try_read(lambda: bool(target.is_alive())),
    }


@_action("trigger_selah")
def _a_selah(world, step):
    pawn = _actor(world, step)
    pawn.trigger_selah_moment()
    return {"triggered": True}


@_action("aim_at")
def _a_aim_at(world, step):
    """Point the controller at another actor's origin."""
    pawn = _actor(world, step)
    target = common.resolve_actor(world, step["target"])
    controller = pawn.get_controller()
    if controller is None:
        raise LookupError("no controller on %s" % pawn.get_name())
    rot = unreal.MathLibrary.find_look_at_rotation(
        _aim_origin(pawn), target.get_actor_location())
    controller.set_control_rotation(rot)
    return {"aimed_at": target.get_name(), "rotation": [rot.pitch, rot.yaw, rot.roll]}


@_action("aim_at_vital")
def _a_aim_at_vital(world, step):
    """Point the controller at a target's CURRENT vital point world location.

    The whole reason vital point tuning is hard is that aiming at an actor
    origin tells you nothing about whether the vital sphere is reachable.
    This aims at the sphere.
    """
    pawn = _actor(world, step)
    target = common.resolve_actor(world, step["target"])
    vital = common.component(target, "GothicVitalPointComponent")
    if vital is None:
        raise LookupError("%s has no GothicVitalPointComponent" % target.get_name())
    point = vital.get_current_vital_world_location()
    controller = pawn.get_controller()
    if controller is None:
        raise LookupError("no controller on %s" % pawn.get_name())
    rot = unreal.MathLibrary.find_look_at_rotation(_aim_origin(pawn), point)
    controller.set_control_rotation(rot)
    return {
        "aimed_at_vital_of": target.get_name(),
        "vital_index": int(vital.get_active_vital_index()),
        "vital_location": common.vec(point),
    }


@_action("set_combat_target")
def _a_set_target(world, step):
    """Force an enemy onto a target, skipping perception acquisition."""
    enemy = _actor(world, step)
    target = common.resolve_actor(world, step["target"])
    enemy.set_combat_target(target)
    return {"enemy": enemy.get_name(), "target": target.get_name()}


@_action("freeze_vital")
def _a_freeze_vital(world, step):
    """Lock a vital point to a known index.

    The single most valuable determinism primitive available. With the vital
    frozen, hit detection becomes testable instead of merely observable.
    Pass index -1 to freeze wherever it currently is.
    """
    target = _actor(world, step)
    vital = common.component(target, "GothicVitalPointComponent")
    if vital is None:
        raise LookupError("%s has no GothicVitalPointComponent" % target.get_name())
    index = int(step.get("index", -1))
    vital.freeze_vital_point(index)
    return {"frozen_at": int(vital.get_active_vital_index())}


@_action("damage_vital")
def _a_damage_vital(world, step):
    """Feed synthetic damage to the vital point component.

    Drives shift thresholds without needing real combat to reach them.
    Note this only notifies the vital component; it does not apply a GE.
    """
    target = _actor(world, step)
    vital = common.component(target, "GothicVitalPointComponent")
    if vital is None:
        raise LookupError("%s has no GothicVitalPointComponent" % target.get_name())
    amount = float(step.get("amount", 10.0))
    before = int(vital.get_active_vital_index())
    vital.notify_damage_taken(amount)
    return {
        "amount": amount,
        "index_before": before,
        "index_after": int(vital.get_active_vital_index()),
    }


@_action("console")
def _a_console(world, step):
    common.run_console(world, step["command"], bool(step.get("force", False)))
    return {"command": step["command"]}


def _step_vector(step):
    try:
        return unreal.Vector(
            float(step["x"]), float(step["y"]), float(step["z"]))
    except (KeyError, TypeError, ValueError):
        raise ValueError("step needs numeric 'x', 'y' and 'z'")


@_action("move_to")
def _a_move_to(world, step):
    """Walk a pawn to a world location using real character movement.

    Registers a sustained order; _drive_movers applies it every tick until the
    pawn arrives, stalls, or times out. The outcome is appended to the scenario
    results as a synthetic 'move_to result' record, so a blocked move shows up
    in the dump rather than silently never finishing.
    """
    pawn = _actor(world, step)
    target = _step_vector(step)
    start_loc = pawn.get_actor_location()
    order = {
        "pawn": pawn,
        "name": pawn.get_name(),
        "target": target,
        "accept_radius": float(step.get("accept_radius", _MOVE_ACCEPT_RADIUS)),
        "timeout": float(step.get("timeout", _MOVE_TIMEOUT)),
        "started_at": _elapsed(),
        "start_dist": math.hypot(target.x - start_loc.x, target.y - start_loc.y),
        "best_dist": None,
        "stalled_for": 0.0,
    }
    order["best_dist"] = order["start_dist"]
    _movers[order["name"]] = order
    return {
        "moving": order["name"],
        "from": common.vec(start_loc),
        "to": common.vec(target),
        "distance": round(order["start_dist"], 1),
    }


@_action("stop_move")
def _a_stop_move(world, step):
    pawn = _actor(world, step)
    removed = _movers.pop(pawn.get_name(), None)
    return {"stopped": pawn.get_name(), "had_order": removed is not None}


@_action("teleport")
def _a_teleport(world, step):
    """Instantly reposition a pawn. Bypasses collision entirely.

    Use for setting up an encounter, never for testing a barrier: a teleport
    will pass straight through an AGothicBleedGate and tell you nothing about
    whether the gate holds. Use move_to for that.
    """
    pawn = _actor(world, step)
    target = _step_vector(step)
    before = pawn.get_actor_location()
    pawn.set_actor_location(target, False, True)
    if "yaw" in step:
        rot = pawn.get_actor_rotation()
        rot.yaw = float(step["yaw"])
        pawn.set_actor_rotation(rot, True)
    _movers.pop(pawn.get_name(), None)
    return {
        "teleported": pawn.get_name(),
        "from": common.vec(before),
        "to": common.vec(pawn.get_actor_location()),
    }


def _drive_movers(elapsed, delta_seconds):
    """Apply every active move order for one frame, and retire finished ones."""
    for name in list(_movers.keys()):
        order = _movers[name]
        pawn = order["pawn"]

        if not common.is_valid(pawn):
            _movers.pop(name, None)
            _results.append({"do": "move_to result", "actor": name,
                             "ok": False, "outcome": "pawn became invalid",
                             "fired_at": round(elapsed, 3)})
            continue

        loc = pawn.get_actor_location()
        target = order["target"]
        # Horizontal only: a target Z that differs from the pawn's would otherwise
        # tilt the input vector and slow the pawn on ramps.
        dx = target.x - loc.x
        dy = target.y - loc.y
        dist = math.hypot(dx, dy)

        if dist <= order["accept_radius"]:
            _movers.pop(name, None)
            _results.append({"do": "move_to result", "actor": name, "ok": True,
                             "outcome": "arrived", "at": common.vec(loc),
                             "took_seconds": round(elapsed - order["started_at"], 2),
                             "fired_at": round(elapsed, 3)})
            continue

        if dist < order["best_dist"] - _MOVE_STALL_EPSILON:
            order["best_dist"] = dist
            order["stalled_for"] = 0.0
        else:
            order["stalled_for"] += delta_seconds

        if order["stalled_for"] >= _MOVE_STALL_SECONDS:
            _movers.pop(name, None)
            _results.append({
                "do": "move_to result", "actor": name, "ok": False,
                "outcome": "blocked",
                "blocked_at": common.vec(loc),
                "remaining_distance": round(dist, 1),
                "note": "no progress for %.1fs -- geometry, navmesh, or a Bleed gate"
                        % _MOVE_STALL_SECONDS,
                "fired_at": round(elapsed, 3)})
            continue

        if elapsed - order["started_at"] >= order["timeout"]:
            _movers.pop(name, None)
            _results.append({
                "do": "move_to result", "actor": name, "ok": False,
                "outcome": "timed out", "at": common.vec(loc),
                "remaining_distance": round(dist, 1),
                "fired_at": round(elapsed, 3)})
            continue

        # Unit vector built by hand rather than via Vector.normalize(), whose
        # in-place-vs-copy behaviour differs across engine Python bindings.
        pawn.add_movement_input(
            unreal.Vector(dx / dist, dy / dist, 0.0), 1.0, False)


@_action("mark")
def _a_mark(world, step):
    """No-op marker. Lands a labelled timestamp in the results for correlation."""
    return {"label": step.get("label", "mark")}


# --------------------------------------------------------------------------
# Execution
# --------------------------------------------------------------------------

def is_running():
    return _tick_handle is not None


def validate(steps):
    """Check a script before running it. Returns a list of problems."""
    problems = []
    for i, step in enumerate(steps):
        if not isinstance(step, dict):
            problems.append("step %d is not an object" % i)
            continue
        do = step.get("do")
        if do not in _ACTIONS:
            problems.append(
                "step %d: unknown action '%s'. Available: %s"
                % (i, do, available_actions()))
        try:
            float(step.get("at", 0.0))
        except (TypeError, ValueError):
            problems.append("step %d: 'at' must be a number" % i)
    return problems


def start(world, steps, max_duration=60.0):
    global _tick_handle, _steps, _results, _world, _game_origin, _wall_origin
    global _stop_reason, _max_duration, _error_streak

    if is_running():
        raise RuntimeError("A scenario is already running. Call stop() first.")

    problems = validate(steps)
    if problems:
        raise ValueError("Invalid scenario: %s" % problems)

    _steps = sorted(
        [dict(s, _index=i, _fired=False) for i, s in enumerate(steps)],
        key=lambda s: float(s.get("at", 0.0)))
    _results = []
    _world = world
    _game_origin = common.game_time(world)
    _wall_origin = time.time()
    _stop_reason = None
    _max_duration = float(max_duration)
    _error_streak = 0
    _movers.clear()   # a previous run's unfinished walk must not resume into this one

    common.audit("scenario start\t%d steps\tmax_duration=%.1f" % (len(_steps), _max_duration))
    _tick_handle = unreal.register_slate_post_tick_callback(_on_tick)
    return True


def stop(reason="stopped by request"):
    global _tick_handle, _stop_reason
    if _tick_handle is not None:
        try:
            unreal.unregister_slate_post_tick_callback(_tick_handle)
        except Exception:
            pass
        _tick_handle = None
        _stop_reason = reason
        _movers.clear()   # nothing drives them once the tick callback is gone
        common.audit("scenario stop\t%s" % reason)
    return True


def _elapsed():
    """Game seconds since scenario start, falling back to wall clock."""
    now = common.game_time(_world) if _world is not None else None
    if now is not None and _game_origin is not None:
        return now - _game_origin
    return time.time() - _wall_origin


def status():
    pending = sum(1 for s in _steps if not s.get("_fired"))
    return {
        "running": is_running(),
        "steps_total": len(_steps),
        "steps_fired": len(_steps) - pending,
        "steps_pending": pending,
        "elapsed_game_seconds": round(_elapsed(), 3) if _steps else None,
        "max_duration": _max_duration,
        "spawned_actors": [a.get_name() for a in _spawned if common.is_valid(a)],
        "stop_reason": _stop_reason,
    }


def _on_tick(delta_seconds):
    global _error_streak

    if not is_running():
        return

    try:
        if _world is not None and not common.is_valid(_world):
            stop("world became invalid (PIE ended)")
            return

        elapsed = _elapsed()

        if elapsed > _max_duration:
            stop("hit max_duration cap of %.1fs" % _max_duration)
            return

        for step in _steps:
            if step.get("_fired"):
                continue
            if float(step.get("at", 0.0)) > elapsed:
                break  # sorted, so nothing later is due either

            step["_fired"] = True
            record = {
                "index": step.get("_index"),
                "do": step.get("do"),
                "at": float(step.get("at", 0.0)),
                "fired_at": round(elapsed, 3),
                "drift": round(elapsed - float(step.get("at", 0.0)), 3),
            }
            try:
                record["result"] = _ACTIONS[step["do"]](_world, step)
                record["ok"] = True
            except Exception as exc:
                record["ok"] = False
                record["error"] = "%s: %s" % (type(exc).__name__, exc)
            _results.append(record)

        # Sustained orders are driven after this frame's steps, so a move_to that
        # fired on this very tick gets its first frame of input immediately.
        _drive_movers(elapsed, delta_seconds)

        # An outstanding move keeps the scenario alive past the last step's grace
        # window; otherwise the run ends the instant the final step fires and a
        # walk that needed eight seconds is cut off at one.
        if (all(s.get("_fired") for s in _steps)
                and not _movers
                and elapsed > _last_at() + _grace):
            stop("completed")

        _error_streak = 0

    except Exception as exc:
        _error_streak += 1
        unreal.log_error("[VigilCombat] tick error: %s" % exc)
        if _error_streak >= _MAX_ERROR_STREAK:
            stop("auto-stopped after %d consecutive tick errors" % _error_streak)


def _last_at():
    return max((float(s.get("at", 0.0)) for s in _steps), default=0.0)


def results():
    return list(_results)


def dump(filename_stem="scenario"):
    payload = {
        "recorded_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "meta": status(),
        "schema": {
            "at": "scheduled game-time offset from scenario start",
            "fired_at": "actual game-time offset when the step ran",
            "drift": "fired_at minus at; large values mean frame hitches",
            "ok": "false means the action raised; see error",
        },
        "steps": _results,
    }
    return {"path": common.write_json(payload, filename_stem), "steps": len(_results)}


# --------------------------------------------------------------------------
# Spawning and cleanup
# --------------------------------------------------------------------------

def spawn(world, class_path, location, rotation=(0.0, 0.0, 0.0)):
    """Spawn an actor into the PIE world and track it for cleanup.

    class_path for a Blueprint needs the _C suffix, e.g.
    "/Game/Blueprints/Enemies/BP_Enemy_FeralRetained.BP_Enemy_FeralRetained_C"

    This is the highest-risk call in the harness -- deferred spawn into a PIE
    world from editor Python is exactly the kind of thing that behaves
    differently between engine versions. Verify it before trusting a scenario
    that depends on it.
    """
    cls = unreal.load_class(None, class_path)
    if cls is None:
        raise LookupError("Could not load class '%s' (Blueprints need _C)" % class_path)

    transform = unreal.Transform(
        unreal.Vector(*location),
        unreal.Rotator(*rotation),
        unreal.Vector(1.0, 1.0, 1.0))

    actor = unreal.GameplayStatics.begin_deferred_actor_spawn_from_class(
        world, cls, transform,
        unreal.SpawnActorCollisionHandlingMethod.ADJUST_IF_POSSIBLE_BUT_ALWAYS_SPAWN)
    if actor is None:
        raise RuntimeError("Deferred spawn returned None for '%s'" % class_path)
    unreal.GameplayStatics.finish_spawning_actor(actor, transform)

    _spawned.append(actor)
    common.audit("spawn\t%s\t%s" % (class_path, actor.get_name()))
    return actor


def cleanup():
    """Destroy everything this harness spawned. Safe to call repeatedly."""
    destroyed = []
    for actor in list(_spawned):
        if common.is_valid(actor):
            name = actor.get_name()
            try:
                actor.destroy_actor()
                destroyed.append(name)
            except Exception as exc:
                unreal.log_error("[VigilCombat] cleanup failed for %s: %s" % (name, exc))
    _spawned.clear()
    common.audit("cleanup\t%d destroyed" % len(destroyed))
    return destroyed
