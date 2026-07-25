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
    def dump_enemy_perception(actor_label: str) -> str:
        """Read what one enemy currently perceives in the running PIE session.

        Args:
            actor_label: Pawn name. A unique substring works.

        Returns:
            JSON with each perceived actor and its distance.
        """
        world = common.require_world()
        pawn = common.resolve_actor(world, actor_label)

        perception = None
        for owner in (pawn, pawn.get_controller()):
            if not owner:
                continue
            try:
                perception = owner.get_component_by_class(
                    unreal.AIPerceptionComponent)
            except Exception:
                perception = None
            if perception:
                break

        if not perception:
            return common.as_json({
                "pawn": pawn.get_name(),
                "error": "No AIPerceptionComponent on the pawn or its controller.",
            })

        perceived = common.try_read(
            lambda: list(perception.get_currently_perceived_actors()),
            default=[])
        rows = []
        for other in perceived:
            rows.append({
                "actor": common.try_read(lambda o=other: o.get_name()),
                "class": common.try_read(
                    lambda o=other: o.get_class().get_name()),
                "distance": common.try_read(
                    lambda o=other: round(pawn.get_distance_to(o), 1)),
            })
        return common.as_json({
            "pawn": pawn.get_name(),
            "perceived_count": len(rows),
            "perceived": rows,
        })
