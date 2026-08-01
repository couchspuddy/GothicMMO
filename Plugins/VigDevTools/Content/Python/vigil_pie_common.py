"""
vigil_pie_common.py

Shared helpers for the Vigil MCP toolsets.

WHY THIS MODULE EXISTS
----------------------
Every call in here is version-sensitive: the PIE world lookup, actor
resolution, component fetch, deferred spawn. Unreal MCP is Experimental and
this was authored against documentation rather than a running editor, so the
odds that at least one of these needs adjusting are high.

Putting them in one module means adjusting is a one-line edit in one file
instead of a hunt through four. That is the entire justification -- there is
no cleverness here, just a single point of failure by choice rather than by
accident.
"""

import json
import os
import time

import unreal

PROBE_DIR = "VigilMCP"


# --------------------------------------------------------------------------
# World access
# --------------------------------------------------------------------------

def pie_world():
    """The running PIE world, or None if not in PIE.

    THE most likely thing in this codebase to need fixing on a given engine
    build. If tools report "not in PIE" while PIE is visibly running, start
    here.
    """
    subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if subsystem is None:
        return None
    getter = getattr(subsystem, "get_game_world", None)
    if getter is None:
        return None
    try:
        return getter()
    except Exception:
        return None


def require_world():
    world = pie_world()
    if world is None:
        raise RuntimeError(
            "Not in PIE. Press Play in the editor, then retry. "
            "(pie_status will confirm.)"
        )
    return world


def game_time(world):
    """Seconds of gameplay time.

    Deliberately not wall clock. `slomo 0.25` is a legitimate debugging tool
    -- it is how you actually see a vital point shift or a hit window -- and
    under time dilation wall-clock stamps desync from what the game did.
    Anything correlating agent actions with gameplay state keys off this.
    """
    try:
        return round(float(unreal.GameplayStatics.get_time_seconds(world)), 4)
    except Exception:
        return None


# --------------------------------------------------------------------------
# Actor and component resolution
# --------------------------------------------------------------------------

def all_pawns(world):
    try:
        return list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn))
    except Exception:
        return []


def resolve_actor(world, label):
    """Find one pawn by exact name, else by unique case-insensitive substring.

    Raises on ambiguity rather than guessing. An agent that silently drove the
    wrong enemy would produce a plausible, wrong scenario result -- worse than
    an error, because it looks like data.
    """
    pawns = all_pawns(world)
    for pawn in pawns:
        if pawn.get_name() == label:
            return pawn

    needle = label.lower()
    matches = [p for p in pawns if needle in p.get_name().lower()]
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise LookupError(
            "No pawn matching '%s'. Present: %s"
            % (label, [p.get_name() for p in pawns])
        )
    raise LookupError(
        "'%s' is ambiguous, matches: %s" % (label, [m.get_name() for m in matches])
    )


def component(actor, class_name):
    """Fetch a component by reflected class name, or None if absent/unexposed."""
    cls = getattr(unreal, class_name, None)
    if cls is None:
        return None
    try:
        return actor.get_component_by_class(cls)
    except Exception:
        return None


class PerceptionComponentMissing(LookupError):
    """No UAIPerceptionComponent on the pawn OR its controller.

    Named on purpose. The previous perception dump swallowed this and reported
    perceived_count: 0, which reads as "the enemy sees nothing" when it actually
    means "the harness never found the component". That silent zero produced a
    false 'enemy perception is dead' finding and cost two diagnosis runs.
    """


def component_names(actor):
    """Every component name on an actor, for error messages that show the miss."""
    try:
        return sorted(c.get_name() for c in actor.get_components_by_class(
            unreal.ActorComponent))
    except Exception as exc:
        return ["<unreadable: %s: %s>" % (type(exc).__name__, exc)]


def perception_component(pawn):
    """The live UAIPerceptionComponent for `pawn`. Raises if absent.

    THE PAWN IS CHECKED FIRST, AND THAT ORDER IS THE WHOLE POINT.
    AGothicEnemyBase creates the component on the PAWN
    (Source/GothicMMO/AI/GothicEnemyBase.cpp:39, CreateDefaultSubobject
    "AIPerception"), not on the controller. UAIPerceptionComponent::OnRegister
    only back-links the controller when the component's owner is an
    AAIController -- `AIOwner = Cast<AAIController>(Owner)` then
    SetPerceptionComponent, AIPerceptionComponent.cpp:214-221 -- so on this project
    AAIController::GetAIPerceptionComponent() is null forever and every
    controller-first lookup finds nothing.

    Returns (component, owner_description).
    """
    searched = []
    controller = None
    try:
        controller = pawn.get_controller()
    except Exception:
        controller = None

    for owner, label in ((pawn, "pawn"), (controller, "controller")):
        if owner is None:
            searched.append("controller: pawn has no controller")
            continue
        try:
            found = owner.get_component_by_class(unreal.AIPerceptionComponent)
        except Exception as exc:
            searched.append("%s %s: %s: %s"
                            % (label, owner.get_name(), type(exc).__name__, exc))
            continue
        if found:
            return found, "%s (%s)" % (label, owner.get_name())
        searched.append("%s %s: no AIPerceptionComponent among %s"
                        % (label, owner.get_name(), component_names(owner)))

    raise PerceptionComponentMissing(
        "No AIPerceptionComponent found for '%s'. Looked, in order: %s. "
        "This is NOT 'perceives nothing' -- it is 'component not found'."
        % (pawn.get_name(), "; ".join(searched)))


def blackboard(actor):
    """The live UBlackboardComponent driving `actor`, or None.

    Why a fallback chain rather than one call: AAIController::GetBlackboardComponent
    is NOT a UFUNCTION (AIController.h:446-447) and therefore does not exist in
    Python at all. `controller.get_blackboard()` raises AttributeError on this
    build -- that single line is what took dump_boss_state out. The reliable
    paths are UAIBlueprintHelperLibrary::GetBlackboard (a BlueprintPure
    UFUNCTION, AIBlueprintHelperLibrary.h:52-53) and the BlueprintReadOnly
    `Blackboard` UPROPERTY (AIController.h:146-147).

    vig_blackboard_tools._blackboard learned this the hard way and keeps its own
    copy of the chain; this one exists so the probe and the encounter tools do
    not each re-derive it.
    """
    controller = None
    try:
        controller = actor.get_controller()
    except Exception:
        controller = None

    attempts = [lambda: unreal.AIBlueprintHelperLibrary.get_blackboard(actor)]
    if controller is not None:
        attempts += [
            lambda: unreal.AIBlueprintHelperLibrary.get_blackboard(controller),
            lambda: controller.get_editor_property("blackboard"),
            lambda: controller.get_component_by_class(unreal.BlackboardComponent),
        ]

    for attempt in attempts:
        try:
            bb = attempt()
            if bb:
                return bb
        except Exception:
            continue
    return None


def is_valid(obj):
    try:
        return bool(unreal.SystemLibrary.is_valid(obj))
    except Exception:
        return False


def ability_slot(slot_name):
    """Resolve an EGothicAbilitySlot member by name, e.g. 'ABILITY1'."""
    enum = getattr(unreal, "GothicAbilitySlot", None)
    if enum is None:
        raise LookupError("unreal.GothicAbilitySlot did not resolve")
    slot = getattr(enum, slot_name.upper(), None)
    if slot is None:
        raise LookupError(
            "Unknown ability slot '%s'. Valid: LIGHT_ATTACK, HEAVY_ATTACK, "
            "ABILITY1, ABILITY2, ABILITY3, SUPER_ABILITY, PRIMARY_FIRE" % slot_name
        )
    return slot


# --------------------------------------------------------------------------
# Result plumbing
# --------------------------------------------------------------------------

def try_read(fn, default=None):
    """Read something that may not resolve on this build. Never raises.

    Returns an error object in place of the value rather than a plausible
    default. A tool reporting 0.0 for a Steadfast charge it could not read
    produces confident wrong conclusions, which is strictly worse than an
    error an agent can see and report.
    """
    try:
        return fn()
    except Exception as exc:
        if default is not None:
            return default
        return {"error": "%s: %s" % (type(exc).__name__, exc)}


def as_json(payload):
    """Serialise a tool result.

    JSON-in-a-string rather than a dataclass: the schema generator in this
    plugin is Experimental, and a shape mismatch fails at tool-discovery time
    rather than call time -- a much worse failure to diagnose. Revisit once
    the dataclass path is verified on this engine build.
    """
    return json.dumps(payload, indent=1, default=str)


def vec(v, digits=1):
    return [round(v.x, digits), round(v.y, digits), round(v.z, digits)]


def output_dir():
    path = os.path.abspath(os.path.join(unreal.Paths.project_saved_dir(), PROBE_DIR))
    os.makedirs(path, exist_ok=True)
    return path


def write_json(payload, filename_stem):
    """Write a payload under Saved/VigilMCP/ and return its absolute path."""
    name = "%s_%s.json" % (filename_stem, time.strftime("%Y%m%d_%H%M%S"))
    path = os.path.join(output_dir(), name)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=1, default=str)
    return path


# --------------------------------------------------------------------------
# Console policy. One allowlist, shared by every path that can run a command,
# so there is a single place to widen or tighten it.
# --------------------------------------------------------------------------
CONSOLE_ALLOWLIST = (
    "stat ",
    "showdebug",
    "slomo",
    "pause",
    "god",
    "toggledebugcamera",
    "log ",
    "abilitysystem.",
    "gothic.",
    "displayall ",
    "dumpgameplaytags",
    "t.maxfps",
    "ke ",
)


def console_allowed(command):
    return any(command.strip().lower().startswith(p) for p in CONSOLE_ALLOWLIST)


def run_console(world, command, force=False):
    """Execute a console command, allowlisted and audited. Raises if refused."""
    if not console_allowed(command) and not force:
        raise PermissionError(
            "Command '%s' is not on the allowlist. Allowed prefixes: %s. "
            "Ask the developer to approve it, then pass force=True."
            % (command, list(CONSOLE_ALLOWLIST))
        )
    audit("console\t%s\tforce=%s" % (command, bool(force)))
    unreal.SystemLibrary.execute_console_command(world, command)
    return True


def audit(line):
    """Append to the session audit log. Every mutation goes through here."""
    with open(os.path.join(output_dir(), "mutation_log.txt"), "a", encoding="utf-8") as handle:
        handle.write("%s\t%s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), line))
