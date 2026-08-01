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
import re
import time

import unreal

PROBE_DIR = "VigilMCP"


# --------------------------------------------------------------------------
# Python-name resolution for engine classes
#
# THE SCRIPTNAME TRAP
# -------------------
# A UCLASS may carry meta=(ScriptName="..."), and PyGenUtil renames the class
# in the Python bindings to that value. The C++ name then does not exist in
# Python at all -- getattr raises AttributeError, which reads exactly like "the
# engine does not have this feature" and has now cost this harness two rounds
# of misdiagnosis.
#
# Confirmed instances in play here:
#   UAIBlueprintHelperLibrary      -> unreal.AIHelperLibrary
#                                     (AIBlueprintHelperLibrary.h:25)
#   UAbilitySystemBlueprintLibrary -> unreal.AbilitySystemLibrary
#   UBlueprintGameplayTagLibrary   -> unreal.GameplayTagLibrary
#
# Rather than hard-code one spelling and fail opaquely, resolve() takes every
# candidate spelling and, on total failure, raises an error carrying the actual
# contents of dir(unreal) that look related. The next person to hit a rename
# gets the answer in the error text instead of another investigation.
# --------------------------------------------------------------------------

_class_cache = {}


def resolve_class(candidates, purpose, hint_tokens=()):
    """The first name in `candidates` that exists on `unreal`. Never returns None.

    Args:
        candidates: Python name spellings to try, most likely first.
        purpose: What the caller wanted it for, quoted back in the error.
        hint_tokens: Substrings used to list near-misses out of dir(unreal)
            when nothing resolves, so a rename is diagnosable from the error
            alone without an editor session.

    Raises:
        LookupError: Naming every candidate tried and every near-miss found.
    """
    key = tuple(candidates)
    cached = _class_cache.get(key)
    if cached is not None:
        return cached

    for name in candidates:
        obj = getattr(unreal, name, None)
        if obj is not None:
            _class_cache[key] = obj
            return obj

    tokens = tuple(hint_tokens) or tuple(candidates[:1])
    near = sorted(
        n for n in dir(unreal)
        if not n.startswith("_") and any(t in n for t in tokens))
    raise LookupError(
        "None of %s exist on this build's `unreal` module, so %s is "
        "unavailable. This is usually a meta=(ScriptName=\"...\") rename, not a "
        "missing feature -- check the UCLASS line in the engine header. "
        "Related names that DO exist: %s"
        % (list(candidates), purpose,
           ", ".join(near) if near else "(none matching %s)" % list(tokens)))


# --------------------------------------------------------------------------
# Custom collision trace channels
#
# THE SECOND TRAP, WHICH IS NOT A RENAME
# --------------------------------------
# UCollisionProfile is NOT a ScriptName rename -- it is not exported to Python
# at all, and no spelling will ever find it. PyGenUtil::ShouldExportClass
# (PyGenUtil.cpp:1769) requires IsScriptExposedClass OR HasScriptExposedFields:
#
#   * IsScriptExposedClass wants BlueprintType on the UCLASS. UCollisionProfile
#     is declared UCLASS(config=Engine, defaultconfig, MinimalAPI,
#     meta=(DisplayName="Collision")) (CollisionProfile.h:159) -- no
#     BlueprintType. DisplayName does not affect the Python name or export.
#   * HasScriptExposedFields wants a UFUNCTION with BlueprintCallable/
#     BlueprintEvent, or a UPROPERTY with CPF_BlueprintVisible/Assignable
#     (PyGenUtil.cpp:1609-1621). UCollisionProfile declares zero UFUNCTIONs,
#     and every one of its UPROPERTYs -- DefaultChannelResponses included
#     (CollisionProfile.h:167-183) -- is a bare UPROPERTY(globalconfig), which
#     sets neither flag.
#
# FCustomChannelSetup is dead the same way: USTRUCT() with no BlueprintType
# (CollisionProfile.h:95) and plain UPROPERTY() members. So the CDO route the
# previous implementation took could never have worked on any UE build.
#
# UEngineTypes::ConvertToTraceType (EngineTypes.h:4075) is a plain static, not
# a UFUNCTION, so that route is closed too.
#
# What IS reachable is the config file the CDO would have been loaded from.
# DefaultChannelResponses is UPROPERTY(globalconfig) on a `defaultconfig`
# class, so Config/DefaultEngine.ini's [/Script/Engine.CollisionProfile]
# section IS the mapping's source of truth. Parsing it is deterministic and
# needs no reflection at all.
# --------------------------------------------------------------------------

# UCollisionProfile::LoadProfileConfig seeds TraceTypeMapping with
# ECC_Visibility then ECC_Camera (CollisionProfile.cpp:373-377) before
# appending each bTraceType custom channel in array order (:447-450). So
# ETraceTypeQuery ordinal = 2 + position among the bTraceType entries.
_BUILTIN_TRACE_TYPES = ("Visibility", "Camera")

_COLLISION_PROFILE_SECTION = "/Script/Engine.CollisionProfile"

_trace_type_cache = {}


def project_config_dir():
    """Absolute path to the project's Config/ directory.

    UPaths is exposed as unreal.Paths via meta=(ScriptName="Paths") on
    UBlueprintPathsLibrary, which is exactly the rename class of bug this
    module exists to absorb -- so it goes through resolve_class rather than a
    hard-coded spelling, and falls back to deriving Config/ from the project
    directory if the paths library moves again.
    """
    paths_failure = None
    try:
        paths = resolve_class(
            ("Paths", "BlueprintPathsLibrary"),
            "locating Config/DefaultEngine.ini",
            hint_tokens=("Path",))
        getter = getattr(paths, "project_config_dir", None)
        if getter is None:
            paths_failure = "%s has no project_config_dir" % paths.__name__
        else:
            value = str(getter())
            if value:
                return os.path.abspath(value)
            paths_failure = "project_config_dir returned an empty string"
    except LookupError as exc:
        # Not swallowed: carried into the error below if the fallback also
        # fails, so both routes are visible in one message.
        paths_failure = str(exc)

    system = resolve_class(
        ("SystemLibrary", "KismetSystemLibrary"),
        "locating Config/DefaultEngine.ini (Paths route failed: %s)"
        % paths_failure,
        hint_tokens=("System",))
    root = str(system.get_project_directory())
    if not root:
        raise LookupError(
            "SystemLibrary.get_project_directory returned an empty path and "
            "the Paths route failed (%s), so Config/DefaultEngine.ini cannot "
            "be located" % paths_failure)
    return os.path.abspath(os.path.join(root, "Config"))


def _read_ini_text(path):
    """Config ini text. Tries the encodings UE actually writes, then reports."""
    with open(path, "rb") as handle:
        raw = handle.read()
    errors = []
    for encoding in ("utf-8-sig", "utf-16", "latin-1"):
        try:
            return raw.decode(encoding)
        except (UnicodeDecodeError, UnicodeError) as exc:
            errors.append("%s: %s" % (encoding, exc))
    raise LookupError(
        "could not decode %s with any of utf-8-sig/utf-16/latin-1 (%s)"
        % (path, "; ".join(errors)))


def _parse_channel_responses(text):
    """DefaultChannelResponses entries from [/Script/Engine.CollisionProfile].

    Returns a list of dicts with at least `channel`, `name` and `trace` keys,
    in the order the engine would hold them in DefaultChannelResponses.

    Honours the ini array operators: `+` and a bare key append, `-` removes a
    previously-added entry for the same ECC_ channel. `.` is treated as an
    append, which is what UE does for a fresh array in a single file.
    """
    entries = []
    in_section = False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_section = stripped[1:-1] == _COLLISION_PROFILE_SECTION
            continue
        if not in_section or not stripped or stripped.startswith(";"):
            continue

        match = re.match(
            r'^([+\-.]?)DefaultChannelResponses\s*=\s*\((.*)\)\s*$', stripped)
        if match is None:
            continue

        operator, body = match.group(1), match.group(2)
        channel = re.search(r'\bChannel\s*=\s*(\w+)', body)
        if channel is None:
            continue
        channel = channel.group(1)

        if operator == "-":
            entries = [e for e in entries if e["channel"] != channel]
            continue

        name = re.search(r'\bName\s*=\s*"([^"]*)"', body)
        trace = re.search(r'\bbTraceType\s*=\s*(\w+)', body)
        entries = [e for e in entries if e["channel"] != channel]
        entries.append({
            "channel": channel,
            "name": name.group(1) if name else "",
            "trace": bool(trace) and trace.group(1).lower() == "true",
        })
    return entries


def collision_channel_table():
    """The project's custom collision channels, plus the trace-type ordering.

    Read straight out of Config/DefaultEngine.ini. Raises rather than returning
    an empty table if the file or the section is missing -- a silent empty
    result here would present as "the shot missed", which is the exact failure
    this whole path exists to eliminate.
    """
    path = os.path.join(project_config_dir(), "DefaultEngine.ini")
    if not os.path.isfile(path):
        raise LookupError(
            "%s does not exist, so the custom collision channels cannot be "
            "derived" % path)

    entries = _parse_channel_responses(_read_ini_text(path))
    if not entries:
        raise LookupError(
            "%s has no DefaultChannelResponses entries under [%s]; this "
            "project defines ECC_Weapon there (DefaultEngine.ini:219), so an "
            "empty parse means the file layout changed, not that the channel "
            "is gone" % (path, _COLLISION_PROFILE_SECTION))

    trace_types = list(_BUILTIN_TRACE_TYPES) + [
        e["name"] for e in entries if e["trace"]]
    return {
        "config_file": path,
        "channels": entries,
        "trace_types": trace_types,
    }


def trace_type_for_channel(channel_name):
    """(ETraceTypeQuery, provenance) for a named custom collision channel.

    `channel_name` is the Name= from DefaultEngine.ini -- "Weapon", not
    "ECC_Weapon" and not "ECC_GameTraceChannel1".

    Never guesses. If the ordinal cannot be derived from config, or the derived
    ordinal has no member on unreal.TraceTypeQuery, this raises with everything
    it tried and everything the enum actually exposes, so the answer comes out
    of the error text rather than another investigation.
    """
    cached = _trace_type_cache.get(channel_name)
    if cached is not None:
        return cached

    table = collision_channel_table()
    trace_types = table["trace_types"]
    if channel_name not in trace_types:
        raise LookupError(
            "no trace channel named '%s' in %s. Channels declared there: %s. "
            "Trace-enabled channels, in ETraceTypeQuery order: %s. A channel "
            "with bTraceType=False (ArenaBlock is one) can never be traced by "
            "ETraceTypeQuery -- it is reachable only by object type or profile."
            % (channel_name, table["config_file"],
               [(e["name"], e["channel"], e["trace"]) for e in table["channels"]],
               trace_types))

    index = trace_types.index(channel_name)
    channel = _trace_type_member(index, channel_name)
    source = next((e for e in table["channels"] if e["name"] == channel_name),
                  None)
    if source is None:
        # An engine built-in (Visibility/Camera); its ordinal is fixed by
        # CollisionProfile.cpp:373-377, not by this project's config.
        provenance = (
            "engine built-in trace type '%s' at fixed index %d "
            "(CollisionProfile.cpp:373-377)" % (channel_name, index))
    else:
        provenance = (
            "derived from %s: '%s' is %s and is trace-enabled, making it "
            "trace-type index %d (%s occupy 0 and 1 -- "
            "CollisionProfile.cpp:373-377, :447-450)"
            % (table["config_file"], channel_name, source["channel"], index,
               " and ".join(_BUILTIN_TRACE_TYPES)))

    resolved = (channel, provenance)
    _trace_type_cache[channel_name] = resolved
    return resolved


def _trace_type_member(index, channel_name):
    """unreal.TraceTypeQuery's value for a 0-based trace index.

    The Python spelling of ETraceTypeQuery::TraceTypeQuery3 is NOT assumed.
    PyGenUtil upper-snakes enum entry names through a camel-case break iterator
    (PyGenUtil.cpp:3161, 1859), and whether that iterator splits the trailing
    digit decides between TRACE_TYPE_QUERY3 and TRACE_TYPE_QUERY_3. Rather than
    reason about ICU tokenisation, try both spellings and then fall back to
    constructing the enum from its integer value -- ETraceTypeQuery is a plain
    `enum : int` starting at TraceTypeQuery1 = 0 (EngineTypes.h:1274-1276), so
    the ordinal IS the underlying value.

    Whatever route wins is cross-checked against `index`, so a member that
    exists under an unexpected spelling cannot quietly select the wrong channel.
    """
    enum_type = getattr(unreal, "TraceTypeQuery", None)
    if enum_type is None:
        raise LookupError(
            "unreal.TraceTypeQuery does not exist on this build, so no "
            "ETraceTypeQuery value can be produced for '%s'. Related names on "
            "unreal: %s"
            % (channel_name,
               sorted(n for n in dir(unreal) if "TraceType" in n) or "(none)"))

    ordinal = index + 1
    attempts = []
    for spelling in ("TRACE_TYPE_QUERY%d" % ordinal,
                     "TRACE_TYPE_QUERY_%d" % ordinal,
                     "TraceTypeQuery%d" % ordinal):
        value = getattr(enum_type, spelling, None)
        attempts.append(spelling)
        if value is not None:
            _check_trace_ordinal(value, index, spelling, channel_name)
            return value

    for label, factory in (("TraceTypeQuery.cast(%d)" % index,
                            lambda: enum_type.cast(index)),
                           ("TraceTypeQuery(%d)" % index,
                            lambda: enum_type(index))):
        attempts.append(label)
        try:
            value = factory()
        except Exception as exc:
            attempts[-1] = "%s (%s: %s)" % (label, type(exc).__name__, exc)
            continue
        if value is not None:
            _check_trace_ordinal(value, index, label, channel_name)
            return value

    raise LookupError(
        "could not obtain ETraceTypeQuery index %d (the '%s' channel) from "
        "unreal.TraceTypeQuery. Tried: %s. What the enum actually exposes: %s"
        % (index, channel_name, attempts, _trace_type_members(enum_type)))


def _trace_type_members(enum_type):
    return sorted(n for n in dir(enum_type) if not n.startswith("_"))


def _check_trace_ordinal(value, index, route, channel_name):
    """Fail loudly if the resolved enum value is not the ordinal we derived."""
    try:
        actual = int(value)
    except (TypeError, ValueError):
        return  # not convertible; the spelling is the only evidence we have
    if actual != index:
        raise LookupError(
            "%s resolved to underlying value %d but the '%s' channel derives "
            "to trace-type index %d. Refusing to trace on the wrong channel. "
            "unreal.TraceTypeQuery exposes: %s"
            % (route, actual, channel_name, index,
               _trace_type_members(type(value))))


# --------------------------------------------------------------------------
# Line tracing
#
# THE RETURN-SHAPE TRAP
# --------------------
# KismetSystemLibrary.h:1270 declares
#     static bool LineTraceSingle(..., FHitResult& OutHit, ...)
# i.e. a bool return plus one out param, which by the documented PyGenUtil rule
# should surface in Python as the tuple (bool, HitResult).
#
# ON THIS ENGINE BUILD IT DOES NOT. unreal.SystemLibrary.line_trace_single
# returns a BARE FHitResult -- no tuple, no bool. That was measured, not
# reasoned: aim_at_vital raised
#     "line_trace_single returned <Struct 'HitResult' {}>, not the (bool,
#      HitResult) pair KismetSystemLibrary.h:1270 declares"
# on all six calls of a verification run. The header is not the authority on
# the binding shape; the running build is. Read FHitResult.bBlockingHit for the
# boolean the tuple would have carried.
#
# This is the third binding-shape assumption to break in this harness (after
# the meta=(ScriptName=...) renames above and UCollisionProfile never being
# exported at all), so this helper does not pin to either shape. It accepts the
# 2-tuple, accepts the bare struct, and raises a named error enumerating what
# was actually returned if it is neither. Every trace in every toolset routes
# through here so the two call sites cannot drift apart again -- they already
# had: vig_blackboard_tools used `bool(hit)` as the truth test, and bool() on a
# UE struct is unconditionally True, so its LOS readout reported "blocked" on
# every trace it ever ran.
# --------------------------------------------------------------------------

class TraceReturnShapeError(LookupError):
    """line_trace_single returned a shape this harness cannot interpret."""


def describe_value(value):
    """A short, honest description of an arbitrary binding return value."""
    if isinstance(value, (tuple, list)):
        return "%s of %d: [%s]" % (
            type(value).__name__, len(value),
            ", ".join(type(item).__name__ for item in value))
    return "%s (%r)" % (type(value).__name__, value)


def _looks_like_hit_result(value):
    """Is this a FHitResult? Three routes, because none alone is dependable.

    isinstance against unreal.HitResult is the real test but assumes that
    spelling survives; the name check covers a ScriptName rename that keeps the
    stem; the duck-type check covers a rename that does not. A bool or an int
    passes none of them, which is what keeps the unknown-shape error reachable.
    """
    hit_result_type = getattr(unreal, "HitResult", None)
    if hit_result_type is not None and isinstance(value, hit_result_type):
        return True
    if "HitResult" in type(value).__name__:
        return True
    if isinstance(value, (bool, int, float, str, bytes, tuple, list, dict)):
        return False
    try:
        value.get_editor_property("blocking_hit")
    except Exception:
        return False
    return True


def hit_blocking(hit):
    """FHitResult -> bBlockingHit, via whichever access route this build has.

    Never bool(hit): a UE struct has no __bool__, so bool() on it is always
    True. That exact mistake is what made the AI LOS readout report a blocked
    line of sight on every call.
    """
    routes = []
    for label, fn in (
        ("get_editor_property('blocking_hit')",
         lambda: hit.get_editor_property("blocking_hit")),
        (".blocking_hit", lambda: hit.blocking_hit),
    ):
        routes.append(label)
        try:
            return bool(fn())
        except Exception as exc:
            routes[-1] = "%s (%s: %s)" % (label, type(exc).__name__, exc)
    raise TraceReturnShapeError(
        "could not read bBlockingHit from %s. Tried: %s. Fields the struct "
        "exposes: %s"
        % (describe_value(hit), routes,
           sorted(n for n in dir(hit) if not n.startswith("_"))))


def unpack_trace_result(outcome):
    """(did_hit, hit, shape) from whatever line_trace_single handed back.

    Shape-agnostic by design -- see THE RETURN-SHAPE TRAP above. `shape` is a
    human-readable label for which form was observed, so a caller can surface
    it and the next person does not have to rediscover this.
    """
    if _looks_like_hit_result(outcome):
        # This build's actual behaviour: the out param IS the return value.
        return hit_blocking(outcome), outcome, "bare FHitResult"

    if isinstance(outcome, (tuple, list)):
        # The header-derived form. Do not assume element order: find the
        # FHitResult and treat any remaining element as the bool.
        hits = [v for v in outcome if _looks_like_hit_result(v)]
        if len(hits) == 1 and len(outcome) == 2:
            hit = hits[0]
            flag = next(v for v in outcome if v is not hit)
            return bool(flag), hit, "(bool, FHitResult) tuple"
        if len(hits) == 1 and len(outcome) == 1:
            hit = hits[0]
            return hit_blocking(hit), hit, "1-tuple wrapping FHitResult"

    raise TraceReturnShapeError(
        "SystemLibrary.line_trace_single returned %s. This harness accepts a "
        "bare FHitResult (the shape this engine build actually produces), a "
        "(bool, FHitResult) pair (the shape KismetSystemLibrary.h:1270's "
        "`bool LineTraceSingle(..., FHitResult& OutHit, ...)` signature "
        "implies), or a 1-tuple wrapping a FHitResult -- and nothing else. "
        "The declared signature is NOT authoritative for the Python binding "
        "shape; extend unpack_trace_result in vigil_pie_common rather than "
        "special-casing at the call site."
        % describe_value(outcome))


def line_trace(world, start, end, trace_type, ignore_actors=(),
               trace_complex=False, ignore_self=True):
    """Run a line trace and return (did_hit, hit, shape).

    The single trace entry point for every Vigil toolset. `hit` is returned
    even when did_hit is False, because a non-blocking FHitResult still carries
    a usable TraceEnd.
    """
    outcome = unreal.SystemLibrary.line_trace_single(
        world, start, end, trace_type, trace_complex, list(ignore_actors),
        unreal.DrawDebugTrace.NONE, ignore_self)
    return unpack_trace_result(outcome)


def probe_trace_return_shape(world=None):
    """Report the OBSERVED return shape of line_trace_single on this build.

    Diagnostic only; runs a zero-length Visibility trace at the origin. Exists
    so the shape is one capabilities call away instead of one failed
    verification run away.
    """
    if world is None:
        world = pie_world()
    if world is None:
        return ("unknown -- not in PIE; line_trace_single needs a world. "
                "Re-run capabilities during a PIE session.")
    trace_type, _ = trace_type_for_channel("Visibility")
    outcome = unreal.SystemLibrary.line_trace_single(
        world, unreal.Vector(0.0, 0.0, 0.0), unreal.Vector(0.0, 0.0, 1.0),
        trace_type, False, [], unreal.DrawDebugTrace.NONE, True)
    try:
        _, _, shape = unpack_trace_result(outcome)
    except TraceReturnShapeError as exc:
        return "UNHANDLED SHAPE -- raw return was %s. %s" % (
            describe_value(outcome), exc)
    return ("%s -- raw return %s. KismetSystemLibrary.h:1270 declares "
            "`bool LineTraceSingle(..., FHitResult& OutHit, ...)`, which "
            "implies a (bool, FHitResult) tuple; the runtime binding on this "
            "build differs from the header. Trust this row, not the header."
            % (shape, describe_value(outcome)))


def ai_helper_library():
    """UAIBlueprintHelperLibrary's Python binding.

    ScriptName="AIHelperLibrary" (AIBlueprintHelperLibrary.h:25), so
    unreal.AIBlueprintHelperLibrary does NOT exist. The C++ spelling is kept as
    a fallback candidate purely so a future engine build that drops the meta
    keeps working.
    """
    return resolve_class(
        ("AIHelperLibrary", "AIBlueprintHelperLibrary"),
        "AI spawning and blackboard access",
        hint_tokens=("AIHelper", "AIBlueprint", "AIController"))


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

    NOTE: that helper is `unreal.AIHelperLibrary` in Python, not
    `unreal.AIBlueprintHelperLibrary` -- see resolve_class above. The old
    spelling raised AttributeError inside the try/except below, which silently
    dropped the two BEST paths in this chain and left only the UPROPERTY
    fallbacks doing the work.

    vig_blackboard_tools._blackboard learned this the hard way and keeps its own
    copy of the chain; this one exists so the probe and the encounter tools do
    not each re-derive it.
    """
    controller = None
    try:
        controller = actor.get_controller()
    except Exception:
        controller = None

    helper = ai_helper_library()

    attempts = [lambda: helper.get_blackboard(actor)]
    if controller is not None:
        attempts += [
            lambda: helper.get_blackboard(controller),
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
