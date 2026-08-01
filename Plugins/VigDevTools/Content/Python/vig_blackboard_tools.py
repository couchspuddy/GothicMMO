"""
Vigil Blackboard & Behavior Tree Inspector

Reads enemy AI state out of a LIVE PIE session: blackboard key/values, the
AI controller, and the behaviour tree asset currently running. Stands in for
the BT visual debugger, which crashes UE on open on this build.

Why this file was rewritten
---------------------------
The original version never ran. Every tool was annotated `-> dict` or
`-> list[dict]`, and this plugin's schema generator rejects unparameterised
containers at TOOL-DISCOVERY time, not call time -- so the whole module failed
to import with "Type <class 'dict'>: missing specification for contained type"
and the toolset silently never appeared. Return JSON strings via
vigil_pie_common.as_json, exactly like vigil_pie_toolset does, and see that
module's as_json docstring for the full reasoning.

It also read the EDITOR world, so it could never have observed a running
fight, and it probed a hardcoded key list (NearestPillar, bIsTransitioning)
that does not match BB_BestialLucid. Keys are now enumerated from the
blackboard asset itself, so this stays correct as the tree changes.
"""

import math

import unreal
import toolset_registry

import vigil_pie_common as common


# -----------------------------------------------------------------------------
# Blackboard access
# -----------------------------------------------------------------------------

def _blackboard(controller):
    """Fetch the BlackboardComponent, trying each path this engine build might
    expose. Returns None if every path fails.

    Deliberately not a single call: AAIController exposes the blackboard
    differently across versions, and guessing one accessor is how the previous
    version of this file ended up unverifiable.
    """
    for attempt in (
        lambda: controller.get_blackboard(),
        lambda: controller.get_editor_property("blackboard"),
        lambda: controller.get_component_by_class(unreal.BlackboardComponent),
    ):
        try:
            bb = attempt()
            if bb:
                return bb
        except Exception:
            continue
    return None


def _key_entries(bb, controller=None):
    """[(name, key_type_class_name)] enumerated from the blackboard ASSET.

    Enumerating beats probing a hardcoded list: a key that gets renamed shows
    up here under its new name instead of silently vanishing from the dump.

    The BlackboardComponent's own asset accessors both come back empty on this
    build, so the controller's BehaviorTree asset is the fallback -- a
    UBehaviorTree holds a BlackboardAsset reference, and that one does resolve.
    """
    asset = None
    attempts = [
        lambda: bb.get_blackboard_asset(),
        lambda: bb.get_editor_property("blackboard_asset"),
    ]
    if controller is not None:
        attempts += [
            lambda: controller.get_editor_property(
                "behavior_tree_asset").get_editor_property("blackboard_asset"),
            lambda: controller.get_editor_property(
                "brain_component").get_editor_property(
                    "current_tree").get_editor_property("blackboard_asset"),
        ]
    for attempt in attempts:
        try:
            asset = attempt()
            if asset:
                break
        except Exception:
            continue
    if not asset:
        return []

    out = []
    seen = set()
    # Walk the parent chain -- Keys only holds keys declared on that asset.
    while asset:
        try:
            entries = asset.get_editor_property("keys") or []
        except Exception:
            entries = []
        for entry in entries:
            try:
                name = str(entry.get_editor_property("entry_name"))
                ktype = entry.get_editor_property("key_type")
                tname = ktype.get_class().get_name() if ktype else ""
            except Exception:
                continue
            if name and name not in seen:
                seen.add(name)
                out.append((name, tname))
        try:
            asset = asset.get_editor_property("parent")
        except Exception:
            asset = None
    return out


def _read_key(bb, name, type_name):
    """Read one key using the getter matching its declared type."""
    lowered = (type_name or "").lower()
    try:
        if "bool" in lowered:
            return bool(bb.get_value_as_bool(name))
        if "float" in lowered:
            return round(float(bb.get_value_as_float(name)), 3)
        if "int" in lowered:
            return int(bb.get_value_as_int(name))
        if "vector" in lowered:
            return common.vec(bb.get_value_as_vector(name))
        if "rotator" in lowered:
            r = bb.get_value_as_rotator(name)
            return [round(r.pitch, 1), round(r.yaw, 1), round(r.roll, 1)]
        if "name" in lowered:
            return str(bb.get_value_as_name(name))
        if "string" in lowered:
            return str(bb.get_value_as_string(name))
        if "enum" in lowered:
            return int(bb.get_value_as_enum(name))
        if "class" in lowered:
            c = bb.get_value_as_class(name)
            return c.get_name() if c else "None"
        if "object" in lowered:
            obj = bb.get_value_as_object(name)
            if not obj:
                return "None"
            getter = getattr(obj, "get_actor_label", None)
            return getter() if getter else obj.get_name()
    except Exception as exc:
        return {"error": "%s: %s" % (type(exc).__name__, exc)}
    return {"error": "unhandled key type '%s'" % type_name}


def _ai_pawns(world):
    """Every pawn with a non-player controller."""
    out = []
    for pawn in common.all_pawns(world):
        try:
            controller = pawn.get_controller()
        except Exception:
            continue
        if not controller or isinstance(controller, unreal.PlayerController):
            continue
        out.append((pawn, controller))
    return out


# -----------------------------------------------------------------------------
# Perception
#
# Every read in this section either returns real engine state or raises. There
# is deliberately no `default=[]` anywhere: the previous implementation called
# GetCurrentlyPerceivedActors with no argument (it takes a REQUIRED
# TSubclassOf<UAISense>, AIPerceptionComponent.h:371-372), the TypeError was
# swallowed by try_read's default, and the tool reported perceived_count: 0 for
# an enemy that was demonstrably mid-fight. Nothing here may fail quietly.
# -----------------------------------------------------------------------------

# Sense class -> the label used in the output. UAISense subclasses are reflected
# by name; a sense the build does not expose is reported as such, not skipped.
_SENSE_CLASS_NAMES = ("AISense_Sight", "AISense_Hearing", "AISense_Damage")

# UAISenseConfig_Sight fields that drive FDigestedSightProperties
# (AISense_Sight.cpp:113-123). Read live, because placed instances freeze
# overrides at placement and drift from the C++ constructor -- live Thralls have
# been observed with DetectNeutrals true against a constructor default of false.
_SIGHT_FIELDS = (
    "sight_radius",
    "lose_sight_radius",
    "peripheral_vision_angle_degrees",
    "point_of_view_backward_offset",
    "near_clipping_radius",
    "auto_success_range_from_last_seen_location",
    "max_age",
    "starts_enabled",
)

_AFFILIATION_FIELDS = ("detect_enemies", "detect_neutrals", "detect_friendlies")


class PerceptionReadError(RuntimeError):
    """A perception read failed. Raised rather than reported as an empty list."""


def _sense_class(name):
    cls = getattr(unreal, name, None)
    if cls is None:
        raise PerceptionReadError(
            "unreal.%s did not resolve on this build; the sense cannot be "
            "queried by class." % name)
    return cls


def _sense_configs(perception):
    """The component's live SensesConfig, field by field.

    SensesConfig is EditDefaultsOnly+Instanced on UAIPerceptionComponent, so it
    reads through get_editor_property. A sense that is present but unreadable
    shows as an error object; it is never omitted.
    """
    try:
        configs = list(perception.get_editor_property("senses_config") or [])
    except Exception as exc:
        raise PerceptionReadError(
            "Could not read SensesConfig off %s: %s: %s"
            % (perception.get_name(), type(exc).__name__, exc))

    out = []
    for config in configs:
        if config is None:
            out.append({"error": "null entry in SensesConfig"})
            continue
        cls_name = config.get_class().get_name()
        row = {"class": cls_name}
        if cls_name == "AISenseConfig_Sight":
            for field in _SIGHT_FIELDS:
                row[field] = common.try_read(
                    lambda c=config, f=field: c.get_editor_property(f))
            affil = common.try_read(
                lambda c=config: c.get_editor_property("detection_by_affiliation"))
            row["detection_by_affiliation"] = {
                f: common.try_read(lambda a=affil, ff=f: a.get_editor_property(ff))
                for f in _AFFILIATION_FIELDS
            } if not isinstance(affil, dict) else affil
            row["_fov_note"] = (
                "peripheral_vision_angle_degrees is a HALF-angle: the sense "
                "digests it as cos(angle) and compares it to the dot product "
                "(AISense_Sight.cpp:119, AIHelpers.cpp:74). 90 means a 180-degree "
                "total FOV.")
        out.append(row)
    if not out:
        raise PerceptionReadError(
            "%s has an EMPTY SensesConfig -- it is registered as a listener for "
            "nothing and can never perceive anything." % perception.get_name())
    return out


def _perceived_by(perception, sense_cls, what):
    """Currently- or known-perceived actors for one sense. Raises on failure.

    sense_cls of None means "any sense": GetCurrentlyPerceivedActors branches on
    SenseToUse == nullptr and falls back to HasAnyCurrentStimulus
    (AIPerceptionComponent.cpp:812).
    """
    getter = {
        "current": "get_currently_perceived_actors",
        "known": "get_known_perceived_actors",
    }[what]
    fn = getattr(perception, getter, None)
    if fn is None:
        raise PerceptionReadError(
            "UAIPerceptionComponent.%s is not exposed on this build." % getter)
    try:
        return list(fn(sense_cls) or [])
    except Exception as exc:
        raise PerceptionReadError(
            "%s(%s) failed: %s: %s -- this is a harness/API failure, NOT an "
            "empty perception list."
            % (getter, sense_cls, type(exc).__name__, exc))


def _actor_rows(pawn, actors):
    return [{
        "actor": common.try_read(lambda a=a: a.get_name()),
        "class": common.try_read(lambda a=a: a.get_class().get_name()),
        "distance": common.try_read(lambda a=a: round(pawn.get_distance_to(a), 1)),
    } for a in actors]


def _perceived_block(pawn, perception, what):
    block = {"any_sense": _actor_rows(
        pawn, _perceived_by(perception, None, what))}
    for name in _SENSE_CLASS_NAMES:
        cls = getattr(unreal, name, None)
        if cls is None:
            block[name] = {"error": "unreal.%s did not resolve" % name}
            continue
        block[name] = _actor_rows(pawn, _perceived_by(perception, cls, what))
    return block


def _known_only_block(pawn, perception):
    """Known-but-not-currently-perceived: actors the AI remembers, stale.

    The gap between this and `perceived` is the difference between "never saw
    it" and "saw it and lost it", which are different bugs.
    """
    current = {a.get_name() for a in _perceived_by(perception, None, "current")}
    known = _perceived_by(perception, None, "known")
    return _actor_rows(pawn, [a for a in known if a.get_name() not in current])


def _player_pawns(world):
    out = []
    for pawn in common.all_pawns(world):
        try:
            controller = pawn.get_controller()
        except Exception:
            continue
        if controller and isinstance(controller, unreal.PlayerController):
            out.append(pawn)
    return out


def _stimuli(perception, target):
    """The engine's own last-stimulus record for one target.

    UAIPerceptionComponent::GetActorsPerception (AIPerceptionComponent.h:379-380)
    fills an FActorPerceptionBlueprintInfo; FAIStimulus::bSuccessfullySensed is
    BlueprintReadWrite (AIPerceptionTypes.h:162-163) and is described in the
    engine as "currently used only for marking failed sight tests" -- which is
    exactly the question this tool exists to answer.
    """
    fn = getattr(perception, "get_actors_perception", None)
    if fn is None:
        raise PerceptionReadError(
            "UAIPerceptionComponent.get_actors_perception is not exposed on this "
            "build.")
    try:
        returned = fn(target)
    except Exception as exc:
        raise PerceptionReadError(
            "get_actors_perception(%s) failed: %s: %s"
            % (target.get_name(), type(exc).__name__, exc))

    # bool return plus an out-param struct comes back as a tuple.
    if isinstance(returned, tuple):
        has_info, info = returned[0], returned[1]
    else:
        has_info, info = bool(returned), returned

    if not has_info or info is None:
        return {
            "has_record": False,
            "meaning": "The perception system holds NO record for this actor -- "
                       "it has never been evaluated as a stimulus source for "
                       "this listener (registration/affiliation), as opposed to "
                       "having been evaluated and failed.",
        }

    rows = []
    for stim in (common.try_read(
            lambda: list(info.get_editor_property("last_sensed_stimuli") or []))
            or []):
        if isinstance(stim, str):
            rows.append({"error": stim})
            continue
        rows.append({
            "successfully_sensed": common.try_read(
                lambda s=stim: bool(s.get_editor_property("successfully_sensed"))),
            "age": common.try_read(
                lambda s=stim: round(float(s.get_editor_property("age")), 2)),
            "strength": common.try_read(
                lambda s=stim: round(float(s.get_editor_property("strength")), 3)),
            "stimulus_location": common.try_read(
                lambda s=stim: common.vec(
                    s.get_editor_property("stimulus_location"))),
            "receiver_location": common.try_read(
                lambda s=stim: common.vec(
                    s.get_editor_property("receiver_location"))),
            "tag": common.try_read(
                lambda s=stim: str(s.get_editor_property("tag"))),
        })
    return {
        "has_record": True,
        "is_hostile": common.try_read(
            lambda: bool(info.get_editor_property("is_hostile"))),
        "is_friendly": common.try_read(
            lambda: bool(info.get_editor_property("is_friendly"))),
        "last_sensed_stimuli": rows,
        "_index_note": "Stimuli are indexed by FAISenseID in registration order, "
                       "which matches the senses_config order above.",
    }


def _sight_config(senses):
    for row in senses:
        if row.get("class") == "AISenseConfig_Sight":
            return row
    return None


def _num(value):
    """A config field that must be numeric, or None if it read back as an error."""
    return float(value) if isinstance(value, (int, float)) else None


def _sight_cone(pawn, target, sight, currently_seen):
    """Reproduce FAISystem::CheckIsTargetInSightCone for one target.

    This is a FAITHFUL re-implementation of AIHelpers.cpp:57-78, not an
    approximation of it -- same base offset, same near/far clipping, same dot
    product against cos(PeripheralVisionAngleDegrees). The two inputs it takes
    on trust are the ones the engine also takes:

      * listener location and direction come from the OWNER actor's
        GetActorEyesViewPoint (UAIPerceptionComponent::GetLocationAndDirection,
        AIPerceptionComponent.cpp:449-458) -- and the owner here is the pawn.
      * the target point is the target's actor location (AISense_Sight.cpp:526),
        which is what the default path uses when the target implements no
        IAISightTargetInterface.

    The engine picks LoseSightRadius over SightRadius once a target is already
    seen (AISense_Sight.cpp:527), so `currently_seen` selects the radius.
    """
    if sight is None:
        return {"error": "No AISenseConfig_Sight in SensesConfig; this enemy has "
                         "no sight sense at all."}

    eye = common.try_read(lambda: pawn.get_actor_eyes_view_point())
    if isinstance(eye, dict):
        return {"error": "GetActorEyesViewPoint failed: %s" % eye.get("error")}
    location, rotation = eye[0], eye[1]
    # UKismetMathLibrary::GetForwardVector (unreal.MathLibrary, ScriptName meta)
    # rather than a method on unreal.Rotator -- same value the engine takes as
    # ViewRotation.Vector(), and reliably reflected.
    forward = unreal.MathLibrary.get_forward_vector(rotation)

    backward = _num(sight.get("point_of_view_backward_offset")) or 0.0
    base = (location.x - forward.x * backward,
            location.y - forward.y * backward,
            location.z - forward.z * backward)

    target_loc = target.get_actor_location()
    delta = (target_loc.x - base[0], target_loc.y - base[1], target_loc.z - base[2])
    dist = math.sqrt(delta[0] ** 2 + delta[1] ** 2 + delta[2] ** 2)

    radius = _num(sight.get(
        "lose_sight_radius" if currently_seen else "sight_radius"))
    near = _num(sight.get("near_clipping_radius")) or 0.0
    half_angle = _num(sight.get("peripheral_vision_angle_degrees"))

    out = {
        "eye_location": common.vec(location),
        "eye_forward": [round(forward.x, 3), round(forward.y, 3), round(forward.z, 3)],
        "target_location": common.vec(target_loc),
        "distance": round(dist, 1),
        "radius_applied": radius,
        "radius_source": "lose_sight_radius (already seen)" if currently_seen
                         else "sight_radius (not currently seen)",
        "near_clipping_radius": near,
        "half_angle_deg": half_angle,
    }

    if radius is None or half_angle is None:
        out["verdict"] = "UNKNOWN"
        out["reason"] = ("Sight config did not read back numerically "
                         "(radius=%s, half_angle=%s); no cone verdict is possible."
                         % (radius, half_angle))
        return out

    # The engine adds PointOfViewBackwardOffset to the radius before squaring
    # (AISense_Sight.cpp:115-116), so the effective far clip is radius + offset.
    far = radius + backward
    if dist > far:
        out["verdict"] = "OUT_OF_RANGE"
        out["reason"] = ("distance %.1f exceeds effective sight radius %.1f "
                         "(config %.1f + backward offset %.1f)."
                         % (dist, far, radius, backward))
        return out
    if dist < near:
        out["verdict"] = "INSIDE_NEAR_CLIP"
        out["reason"] = ("distance %.1f is inside the near clipping radius %.1f."
                         % (dist, near))
        return out

    if dist < 1e-4:
        out["off_axis_deg"] = 0.0
        out["verdict"] = "IN_CONE"
        out["reason"] = "target is coincident with the listener."
        return out

    dot = (delta[0] * forward.x + delta[1] * forward.y + delta[2] * forward.z) / dist
    off_axis = math.degrees(math.acos(max(-1.0, min(1.0, dot))))
    out["off_axis_deg"] = round(off_axis, 1)
    out["cos_dot"] = round(dot, 4)
    out["cos_threshold"] = round(math.cos(math.radians(
        max(0.0, min(180.0, half_angle)))), 4)

    if off_axis > half_angle:
        out["verdict"] = "OUTSIDE_SIGHT_CONE"
        out["reason"] = (
            "target is %.1f degrees off-axis, outside the %.1f degree half-angle "
            "(%.0f degree total FOV). CheckIsTargetInSightCone returns false and "
            "AISense_Sight bails BEFORE any line-of-sight trace "
            "(AISense_Sight.cpp:528-531) -- so no LOS result exists for this "
            "target, and none is missing."
            % (off_axis, half_angle, half_angle * 2.0))
    else:
        out["verdict"] = "IN_CONE"
        out["reason"] = (
            "target is %.1f degrees off-axis, inside the %.1f degree half-angle, "
            "and within range. The cone test passes, so visibility is decided by "
            "the LOS trace -- see los_trace_approx."
            % (off_axis, half_angle))
    return out


def _los_trace(world, pawn, target, cone):
    """Best-effort re-run of the sight LOS trace. EXPLICITLY APPROXIMATE.

    The engine traces from the listener location to the target's actor location
    on UAISense_Sight::DefaultSightCollisionChannel (AISense_Sight.cpp:571),
    which comes from the AISystem's DefaultSightCollisionChannel setting and is
    ECC_Visibility unless DefaultGame.ini overrides it -- this project sets no
    override. ECollisionChannel is not directly traceable from Python, so this
    uses TraceTypeQuery1, the Visibility trace channel.

    Labelled approx because that mapping is a config assumption and because the
    engine's own trace ignores the listener actor via a query param this route
    reproduces with an ignore list. Treat a disagreement with the engine's
    stimulus record as the trace being wrong, not the engine.
    """
    if cone.get("verdict") != "IN_CONE":
        return {"skipped": "cone test did not pass; the engine never traces in "
                           "this case either."}
    eye = cone.get("eye_location")
    if not isinstance(eye, list):
        return {"error": "no eye location from the cone step"}
    try:
        hit = unreal.SystemLibrary.line_trace_single(
            world,
            unreal.Vector(eye[0], eye[1], eye[2]),
            target.get_actor_location(),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
            False,
            [pawn],
            unreal.DrawDebugTrace.NONE,
            True)
    except Exception as exc:
        return {"error": "line_trace_single failed: %s: %s"
                         % (type(exc).__name__, exc)}

    blocked, result = (hit[0], hit[1]) if isinstance(hit, tuple) else (bool(hit), None)
    row = {
        "channel": "TraceTypeQuery1 (Visibility) -- assumed equal to the AI "
                   "system's DefaultSightCollisionChannel (ECC_Visibility)",
        "blocked": bool(blocked),
    }
    if blocked and result is not None:
        row["blocking_actor"] = common.try_read(
            lambda: result.get_editor_property("hit_actor").get_name())
        row["impact_point"] = common.try_read(
            lambda: common.vec(result.get_editor_property("impact_point")))
        row["note"] = ("A hit that IS the target actor still counts as visible "
                       "to the engine (UE::AISense_Sight::IsTraceConsideredVisible, "
                       "AISense_Sight.cpp:575).")
    return row


def _explain_target(world, pawn, perception, senses, target):
    seen_by_sight = any(
        a.get_name() == target.get_name()
        for a in _perceived_by(perception, _sense_class("AISense_Sight"), "current"))
    sight = _sight_config(senses)
    cone = _sight_cone(pawn, target, sight, seen_by_sight)
    return {
        "target": target.get_name(),
        "target_class": target.get_class().get_name(),
        "currently_seen_by_sight": seen_by_sight,
        "engine_stimuli": _stimuli(perception, target),
        "sight_cone": cone,
        "los_trace_approx": _los_trace(world, pawn, target, cone),
    }


@unreal.uclass()
class VigBlackboardTools(unreal.ToolsetDefinition):
    """Inspect live enemy AI state: blackboard values, controller, and the
    running behaviour tree. Read-only. Requires an active PIE session."""

    @toolset_registry.tool_call
    @staticmethod
    def dump_enemy_blackboard(actor_label: str = "") -> str:
        """Dump every blackboard key/value for one enemy in the running PIE session.

        Args:
            actor_label: Pawn name (a unique substring works). If empty, uses
                the first AI-controlled pawn that has a blackboard.

        Returns:
            JSON with the pawn, its controller, the behaviour tree asset, and
            every blackboard key with its current value. Keys that could not be
            read appear as error objects rather than plausible defaults.
        """
        world = common.require_world()

        if actor_label:
            pawn = common.resolve_actor(world, actor_label)
            controller = pawn.get_controller()
        else:
            pawn = controller = None
            for candidate, ctrl in _ai_pawns(world):
                if _blackboard(ctrl):
                    pawn, controller = candidate, ctrl
                    break
            if pawn is None:
                return common.as_json(
                    {"error": "No AI pawn with a blackboard found in PIE."})

        if not controller:
            return common.as_json({
                "pawn": pawn.get_name(),
                "error": "Pawn has no controller.",
            })

        result = {
            "pawn": pawn.get_name(),
            "pawn_class": pawn.get_class().get_name(),
            "controller": controller.get_name(),
            "controller_class": controller.get_class().get_name(),
            "location": common.try_read(
                lambda: common.vec(pawn.get_actor_location())),
        }

        bb = _blackboard(controller)
        if not bb:
            result["error"] = (
                "No BlackboardComponent reachable on this controller. If the "
                "AI is visibly running, _blackboard() needs a new access path "
                "for this engine build.")
            return common.as_json(result)

        result["behavior_tree"] = common.try_read(
            lambda: controller.get_editor_property(
                "behavior_tree_asset").get_name(),
            default="<unreadable>")

        entries = _key_entries(bb, controller)
        if not entries:
            result["error"] = (
                "Blackboard asset exposed no keys -- neither the component's "
                "asset accessors nor the behaviour tree's BlackboardAsset "
                "resolved. _key_entries needs another path for this build.")
            return common.as_json(result)

        result["blackboard"] = {
            name: _read_key(bb, name, tname) for name, tname in entries
        }
        return common.as_json(result)

    @toolset_registry.tool_call
    @staticmethod
    def list_ai_pawns() -> str:
        """List every AI-controlled pawn in the running PIE session.

        Cheap orientation call before dump_enemy_blackboard. The names returned
        are the labels every other Vigil tool accepts.

        Returns:
            JSON array of pawns with class, controller, location, and whether a
            blackboard is reachable.
        """
        world = common.require_world()
        rows = []
        for pawn, controller in _ai_pawns(world):
            rows.append({
                "pawn": pawn.get_name(),
                "class": pawn.get_class().get_name(),
                "controller_class": controller.get_class().get_name(),
                "location": common.try_read(
                    lambda p=pawn: common.vec(p.get_actor_location())),
                "has_blackboard": _blackboard(controller) is not None,
            })
        return common.as_json({"count": len(rows), "pawns": rows})

    @toolset_registry.tool_call
    @staticmethod
    def dump_enemy_perception(actor_label: str, target_label: str = "") -> str:
        """Read what one enemy perceives, and why it does not perceive a target.

        Args:
            actor_label: Enemy pawn name. A unique substring works.
            target_label: Pawn to explain in detail -- typically the player. If
                empty, every player-controlled pawn is explained.

        Returns:
            JSON with the perception component's owner, the sight/hearing
            config as it reads LIVE (placed instances drift from the C++
            constructor), the currently- and previously-perceived actor lists
            per sense, the engine's own last stimulus record per target, and a
            harness-computed sight-cone verdict for each explained target.

        Raises:
            PerceptionComponentMissing if the component cannot be found. It
            never reports an empty perception list to mean "not found".
        """
        world = common.require_world()
        pawn = common.resolve_actor(world, actor_label)
        perception, owner_desc = common.perception_component(pawn)

        result = {
            "pawn": pawn.get_name(),
            "pawn_class": pawn.get_class().get_name(),
            "perception_component": perception.get_name(),
            "perception_owner": owner_desc,
            "game_time": common.game_time(world),
            "_owner_note": "The component lives on the PAWN, so "
                           "AAIController::GetAIPerceptionComponent() is null on "
                           "this project. See vigil_pie_common.perception_component.",
        }

        senses = _sense_configs(perception)
        result["senses_config"] = senses

        # Currently perceived, all senses then per sense. GetCurrentlyPerceivedActors
        # takes a REQUIRED TSubclassOf<UAISense> (AIPerceptionComponent.h:371-372);
        # calling it with no argument raises, and the previous version swallowed
        # that into an empty list. None means "any sense" -- the SenseToUse ==
        # nullptr branch at AIPerceptionComponent.cpp:812.
        result["perceived"] = _perceived_block(pawn, perception, "current")
        result["known_not_current"] = _known_only_block(pawn, perception)
        result["perceived_count"] = len(result["perceived"]["any_sense"])

        targets = []
        if target_label:
            targets = [common.resolve_actor(world, target_label)]
        else:
            targets = _player_pawns(world)
        if not targets:
            result["targets"] = {
                "error": "No target to explain: target_label was empty and no "
                         "player-controlled pawn exists in this PIE world."
            }
            return common.as_json(result)

        result["targets"] = [
            _explain_target(world, pawn, perception, senses, target)
            for target in targets
        ]
        return common.as_json(result)
