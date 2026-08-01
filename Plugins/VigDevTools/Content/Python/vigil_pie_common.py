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
    # Duck-type route. NOT get_editor_property('blocking_hit') -- FHitResult
    # exports no fields, so that test answered False for a genuine FHitResult.
    # to_dict() sees the C++ layout regardless, so its keys are the test.
    try:
        row = value.to_dict()
    except Exception:
        return False
    return isinstance(row, dict) and all(k in row for k in _REQUIRED_HIT_KEYS)


# --------------------------------------------------------------------------
# Reading an FHitResult
#
# THE FIELD-ACCESS TRAP
# ---------------------
# FHitResult's members are NOT BlueprintReadWrite, so PyGenUtil exports ZERO
# fields for the struct. That is engine behaviour, not a binding bug:
#     <Struct 'HitResult' (0x...) {}>
#     dir(hit) -> ['assign','cast','copy','export_text','get_editor_property',
#                  'import_text','set_editor_properties','set_editor_property',
#                  'static_struct','to_dict','to_tuple']
# get_editor_property('blocking_hit') therefore raises "Failed to find property
# 'blocking_hit'" and ALWAYS WILL. Attribute access likewise. Three successive
# fixes to aim_at_vital tried those two routes and all three failed at runtime.
#
# The obvious next guess is the Break node, UGameplayStatics::BreakHitResult.
# IT DOES NOT EXIST IN THIS BINDING. Measured 2026-08-01 in PIE:
#     unreal.GameplayStatics.break_hit_result(hit)
#     -> AttributeError: type object 'GameplayStatics' has no attribute
#        'break_hit_result'
# So the "canonical Blueprint route" is not available either, and any code
# written against it (vigil_combat_driver._break_hit was) fails on first call.
#
# WHAT ACTUALLY WORKS: the struct's own to_dict() / to_tuple(). Those two are
# generated from the struct's C++ layout rather than from its exported
# properties, so they see all 18 members even though the property table is
# empty and dir() lists nothing. Measured return of hit.to_dict() on a real
# floor hit:
#     {'blocking_hit': True, 'initial_overlap': False, 'time': 0.2112,
#      'distance': 2112.15, 'location': Vector, 'impact_point': Vector,
#      'normal': Vector, 'impact_normal': Vector, 'phys_mat': PhysicalMaterial,
#      'hit_actor': StaticMeshActor, 'hit_component': StaticMeshComponent,
#      'hit_bone_name': Name("None"), 'bone_name': Name("None"),
#      'hit_item': -1, 'element_index': 0, 'face_index': -1,
#      'trace_start': Vector, 'trace_end': Vector}
# to_tuple() returns the same 18 values in that order.
#
# to_dict() is preferred because it is self-describing: it cannot silently
# shift an index. to_tuple() is the fallback and its order is NOT taken on
# faith -- the layout is selected by length and then ANCHOR-CHECKED against the
# types it predicts, so a shifted layout raises with the observed per-index
# type names instead of reading the physical material as the hit actor.
# probe_trace_return_shape() reports which route won, so this never has to be
# rediscovered from a failed run.
# --------------------------------------------------------------------------

# FHitResult members in to_tuple() order, confirmed against to_dict()'s keys on
# this build. HitBoneName was added alongside BoneName in UE5; the 17-entry
# layout is the pre-UE5 shape and is kept so a rollback is not silent.
_BREAK_HIT_LAYOUTS = {
    18: ("blocking_hit", "initial_overlap", "time", "distance", "location",
         "impact_point", "normal", "impact_normal", "phys_mat", "hit_actor",
         "hit_component", "hit_bone_name", "bone_name", "hit_item",
         "element_index", "face_index", "trace_start", "trace_end"),
    17: ("blocking_hit", "initial_overlap", "time", "distance", "location",
         "impact_point", "normal", "impact_normal", "phys_mat", "hit_actor",
         "hit_component", "bone_name", "hit_item", "element_index",
         "face_index", "trace_start", "trace_end"),
}

# (index, accepted python types, what the layout says lives there). Vectors at
# 4 and 5 are the discriminating anchors -- a shifted layout cannot pass them.
# The numeric anchors accept int as well as bool/float because the binding is
# free to widen a bool to int without the layout being wrong.
_BREAK_HIT_ANCHORS = (
    (0, (bool, int), "bBlockingHit"),
    (1, (bool, int), "bInitialOverlap"),
    (2, (float, int), "Time"),
    (3, (float, int), "Distance"),
    (4, (unreal.Vector,), "Location"),
    (5, (unreal.Vector,), "ImpactPoint"),
)


def _field_type_names(fields):
    return [type(f).__name__ for f in fields]


_REQUIRED_HIT_KEYS = ("blocking_hit", "distance", "impact_point", "hit_actor",
                      "hit_component", "bone_name")


def _hit_row_from_dict(hit):
    """to_dict() route. Self-describing, so no index arithmetic can go wrong."""
    row = hit.to_dict()
    if not isinstance(row, dict):
        raise TraceReturnShapeError(
            "FHitResult.to_dict() returned %s, not a dict." % describe_value(row))
    missing = [k for k in _REQUIRED_HIT_KEYS if k not in row]
    if missing:
        raise TraceReturnShapeError(
            "FHitResult.to_dict() is missing %s. Keys present: %s."
            % (missing, sorted(row)))
    return row, "to_dict"


def _hit_row_from_sequence(fields, source):
    """Positional route, shared by to_tuple() and the Break node. The layout is
    selected by length and then anchor-checked, so a shifted layout raises with
    the observed types rather than reading phys_mat as the hit actor."""
    if not isinstance(fields, (tuple, list)):
        raise TraceReturnShapeError(
            "%s returned %s, not a tuple." % (source, describe_value(fields)))

    layout = _BREAK_HIT_LAYOUTS.get(len(fields))
    if layout is None:
        raise TraceReturnShapeError(
            "%s returned %d values; this harness knows the %s-value layouts. "
            "Observed types by index: %s. Add the layout to _BREAK_HIT_LAYOUTS "
            "in vigil_pie_common rather than indexing at the call site."
            % (source, len(fields), sorted(_BREAK_HIT_LAYOUTS),
               _field_type_names(fields)))

    for index, accepted, declared in _BREAK_HIT_ANCHORS:
        if not isinstance(fields[index], accepted):
            raise TraceReturnShapeError(
                "%s's %d-value layout does not match this build: index %d "
                "should be %s (%s) and is %s. Observed types by index: %s. "
                "Refusing to read fields off a layout that failed its anchor "
                "check."
                % (source, len(fields), index, declared,
                   "/".join(t.__name__ for t in accepted),
                   type(fields[index]).__name__, _field_type_names(fields)))
    return dict(zip(layout, fields)), "%s/%d values" % (source, len(fields))


def _hit_row_from_tuple(hit):
    """FHitResult.to_tuple() route."""
    return _hit_row_from_sequence(hit.to_tuple(), "to_tuple")


def _hit_row_from_break_node(hit):
    """UGameplayStatics::BreakHitResult. ABSENT on this build; kept for the
    future one where PyGenUtil starts exporting it."""
    break_node = getattr(unreal.GameplayStatics, "break_hit_result", None)
    if break_node is None:
        raise TraceReturnShapeError(
            "GameplayStatics.break_hit_result does not exist in this binding "
            "(measured 2026-08-01). Do not write new code against it.")
    return _hit_row_from_sequence(break_node(hit), "break_hit_result")


def break_hit(hit):
    """FHitResult -> a plain dict, through whichever route this build has.

    Raises TraceReturnShapeError naming every route it tried and why each one
    failed, rather than guessing. `hit` of None is a caller error: None means
    "no blocking hit" (see unpack_trace_result), so there is nothing to break.
    """
    if hit is None:
        raise TraceReturnShapeError(
            "break_hit(None): line_trace_single returns None for a clean miss "
            "on this build, so the caller must check did_hit before breaking.")

    row = None
    route = None
    attempts = []
    for label, reader in (("to_dict", _hit_row_from_dict),
                          ("to_tuple", _hit_row_from_tuple),
                          ("GameplayStatics.break_hit_result",
                           _hit_row_from_break_node)):
        try:
            row, route = reader(hit)
            break
        except Exception as exc:
            attempts.append("%s (%s: %s)" % (label, type(exc).__name__, exc))

    if row is None:
        raise TraceReturnShapeError(
            "could not read %s by any known route. Tried: %s. The struct "
            "exports no properties at all (its members are not "
            "BlueprintReadWrite), so get_editor_property and attribute access "
            "are not options either; dir() shows: %s."
            % (describe_value(hit), attempts,
               sorted(n for n in dir(hit) if not n.startswith("_"))))

    return {
        "blocking_hit": bool(row["blocking_hit"]),
        "initial_overlap": bool(row.get("initial_overlap", False)),
        "time": float(row.get("time", 0.0)),
        "distance": float(row["distance"]),
        "location": row.get("location"),
        "impact_point": row["impact_point"],
        "normal": row.get("normal"),
        "impact_normal": row.get("impact_normal"),
        "actor": row["hit_actor"],
        "component": row["hit_component"],
        "bone": str(row["bone_name"]),
        "trace_start": row.get("trace_start"),
        "trace_end": row.get("trace_end"),
        "route": route,
    }


def hit_blocking(hit):
    """FHitResult -> bBlockingHit, through break_hit's route ladder.

    Never bool(hit): a UE struct has no __bool__, so bool() on it is always
    True. That exact mistake is what made the AI LOS readout report a blocked
    line of sight on every call. And never get_editor_property: FHitResult
    exports no fields at all -- see THE FIELD-ACCESS TRAP above.
    """
    return break_hit(hit)["blocking_hit"]


def unpack_trace_result(outcome):
    """(did_hit, hit, shape) from whatever line_trace_single handed back.

    Shape-agnostic by design -- see THE RETURN-SHAPE TRAP above. `shape` is a
    human-readable label for which form was observed, so a caller can surface
    it and the next person does not have to rediscover this.
    """
    if outcome is None:
        # MEASURED, and the second branch of the same defect. Back-to-back
        # aim_at_vital calls 0.1s apart in one scenario returned a bare
        # FHitResult and then None, same trace, same target. None is what the
        # binding produces when nothing blocked: there is no hit to hand back.
        # Treating it as a shape error made the tool fail intermittently even
        # once the FHitResult branch was readable, so it maps to a clean miss.
        # `hit` is None here, which is why line_trace's contract is "hit may be
        # None; check did_hit first".
        return False, None, "None -- no blocking hit"

    if _looks_like_hit_result(outcome):
        # This build's actual behaviour when something IS hit: the out param is
        # the return value.
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
        "SystemLibrary.line_trace_single returned %s. This harness accepts "
        "None (a clean miss -- the shape this build produces when nothing "
        "blocks), a bare FHitResult (the shape it produces on a hit), a "
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

    The single trace entry point for every Vigil toolset.

    CONTRACT: `hit` MAY BE None when did_hit is False. On this build a clean
    miss comes back as None -- there is no non-blocking FHitResult to carry a
    TraceEnd. Check did_hit before touching `hit`; break it with break_hit().
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

    def _probe(start, end):
        """(shape label or UNHANDLED text, raw description)."""
        outcome = unreal.SystemLibrary.line_trace_single(
            world, start, end, trace_type, False, [],
            unreal.DrawDebugTrace.NONE, True)
        try:
            _, hit, shape = unpack_trace_result(outcome)
        except TraceReturnShapeError as exc:
            return ("UNHANDLED SHAPE -- raw return was %s. %s"
                    % (describe_value(outcome), exc)), None
        return shape, hit

    # Two probes on purpose: the miss branch and the hit branch return
    # DIFFERENT types on this build (None vs a bare FHitResult), and only
    # exercising one of them is how the second branch shipped broken.
    miss_shape, _ = _probe(unreal.Vector(0.0, 0.0, 0.0),
                           unreal.Vector(0.0, 0.0, 1.0))

    # A long trace through a pawn's feet to find real geometry, so the hit
    # branch and the Break-node layout are both exercised for real.
    origin = unreal.Vector(0.0, 0.0, 5000.0)
    for pawn in all_pawns(world):
        loc = try_read(pawn.get_actor_location)
        if loc is not None:
            origin = unreal.Vector(loc.x, loc.y, loc.z + 2000.0)
            break
    hit_shape, hit = _probe(origin,
                            unreal.Vector(origin.x, origin.y, origin.z - 10000.0))

    if hit is None:
        break_row = ("not exercised -- the downward probe from %s found no "
                     "geometry, so no FHitResult was broken." % vec(origin))
    else:
        try:
            broken = break_hit(hit)
            break_row = ("route=%s; sample read: blocking_hit=%s actor=%s "
                         "bone=%r distance=%.1f impact=%s"
                         % (broken["route"], broken["blocking_hit"],
                            broken["actor"].get_name() if broken["actor"] else None,
                            broken["bone"], broken["distance"],
                            vec(broken["impact_point"])))
        except Exception as exc:
            break_row = ("FAILED on a real hit (%s: %s)"
                         % (type(exc).__name__, exc))

    return ("miss branch: %s | hit branch: %s | FHitResult read route: %s. "
            "FHitResult exports ZERO properties (its members are not "
            "BlueprintReadWrite), so get_editor_property and attribute access "
            "can never read it -- and GameplayStatics.break_hit_result, the "
            "Blueprint Break node, DOES NOT EXIST in this binding. to_dict() / "
            "to_tuple() are the working routes. KismetSystemLibrary.h:1270's "
            "`bool LineTraceSingle(..., FHitResult& OutHit, ...)` implies a "
            "(bool, FHitResult) tuple and this build does not produce one; a "
            "clean miss comes back as None. Trust this row, not the header."
            % (miss_shape, hit_shape, break_row))


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
# Inventory and equipment
#
# THE INVENTORY IS NOT ON THE PAWN. UGothicInventoryComponent is created on
# AGothicPlayerState so it survives respawns alongside the ASC and the
# AttributeSet (GothicInventoryComponent.h:2-3, GothicPlayerState.h:70-72).
# Looking for it on the character is the first thing everyone tries and it
# comes back None every time.
#
# What is and is not reachable from Python here, all verified against the
# header rather than assumed:
#
#   EquipItem / UnequipSlot     BlueprintCallable  -> exported. The real API.
#   GetAllItems / IsSlotEquipped BlueprintPure     -> exported.
#   AGothicPlayerState::GetInventory()  plain inline getter, NOT a UFUNCTION
#                               (GothicPlayerState.h:39) -> NOT exported. The
#                               UPROPERTY behind it is, so we read that.
#   GetEquippedItem             no UFUNCTION macro (GothicInventoryComponent.h:131)
#                               -> NOT exported. Read the EquippedItems
#                               UPROPERTY instead (h:302-303). C++ `protected`
#                               does not hide a UPROPERTY from Python.
#   UGothicItemDefinition::RollInstance   no UFUNCTION (GothicItemDefinition.h:138)
#   AGothicWorldPickup::InitializePickup  no UFUNCTION (GothicWorldPickup.h:27)
#                               -> NEITHER is exported, and every field of
#                               FGothicItemInstance is BlueprintReadOnly
#                               (GothicItemTypes.h:139-180) so the struct cannot
#                               be filled in from Python either. There is
#                               therefore NO way to mint an item from an
#                               arbitrary definition asset at runtime. Equip
#                               actions can only address what the inventory
#                               already holds -- the StartingItemDefs kit or
#                               DebugSpawnTestItems. Adding a definition to
#                               StartingItemDefs on the PlayerState BP is an
#                               editor-side change, not a harness one.
# --------------------------------------------------------------------------

class InventoryUnavailable(LookupError):
    """No UGothicInventoryComponent reachable from this pawn.

    Named, and raised rather than returning an empty item list, because "the
    player is carrying nothing" and "the harness never found the inventory"
    are indistinguishable once either is allowed to come back as [].
    """


class EquipSlotUnknown(ValueError):
    """A slot name or value that EGothicEquipSlot does not define."""


class ItemNotFound(LookupError):
    """Nothing in the inventory matched the requested item address."""


def inventory_component(pawn):
    """The live UGothicInventoryComponent for `pawn`. Raises if unreachable."""
    getter = getattr(pawn, "get_player_state", None)
    if getter is None:
        raise InventoryUnavailable(
            "%s has no get_player_state, so it is not a player pawn. The "
            "inventory lives on AGothicPlayerState (GothicPlayerState.h:70-72), "
            "never on the character." % pawn.get_name())

    state = getter()
    if state is None:
        raise InventoryUnavailable(
            "%s has no PlayerState yet. In PIE the PlayerState arrives a frame "
            "or two after the pawn; schedule the step slightly later."
            % pawn.get_name())

    inv = try_read(lambda: state.get_editor_property("inventory_component"))
    if isinstance(inv, dict):       # try_read error object, not a component
        inv = None
    if inv is None:
        inv = component(state, "GothicInventoryComponent")
    if inv is None:
        raise InventoryUnavailable(
            "No UGothicInventoryComponent on PlayerState %s (reached from pawn "
            "%s). Tried the 'inventory_component' UPROPERTY "
            "(GothicPlayerState.h:71-72) and get_component_by_class. Components "
            "present: %s"
            % (state.get_name(), pawn.get_name(), component_names(state)))
    return inv


_EQUIP_SLOT_TABLE = None


def equip_slot_table():
    """{PYTHON_NAME: enum entry} for every EGothicEquipSlot value.

    Built by reflection, deliberately. Python enum entry names are GENERATED
    from the C++ names rather than declared, so hardcoding them is a guess:
    EGothicEquipSlot::LeftArm (GothicItemTypes.h:41) arrives here as LEFT_ARM.
    Reflecting means a rename surfaces as a missing key WITH the real list
    attached, instead of an AttributeError inside an action.

    The values are also non-contiguous on purpose -- weapons are 0-2, armor is
    10-19 (GothicItemTypes.h:29-47) -- so an index is not a slot.
    """
    global _EQUIP_SLOT_TABLE
    if _EQUIP_SLOT_TABLE is not None:
        return _EQUIP_SLOT_TABLE

    enum_cls = getattr(unreal, "GothicEquipSlot", None)
    if enum_cls is None:
        raise EquipSlotUnknown(
            "unreal.GothicEquipSlot does not exist on this build. Suspect a "
            "meta=(ScriptName=...) rename on EGothicEquipSlot, or a stale "
            "binding -- restart the editor after a C++ rebuild.")

    table = {}
    for name in dir(enum_cls):
        if name.startswith("_"):
            continue
        entry = getattr(enum_cls, name, None)
        if isinstance(entry, enum_cls):
            table[name] = entry

    if not table:
        raise EquipSlotUnknown(
            "unreal.GothicEquipSlot exposed no enum entries. dir() gave: %s"
            % sorted(n for n in dir(enum_cls) if not n.startswith("_")))

    _EQUIP_SLOT_TABLE = table
    return table


def _slot_key(text):
    """Fold a slot name to its comparable form: LEFT_ARM == 'left arm'."""
    return "".join(ch for ch in str(text).lower() if ch.isalnum())


def equip_slot(spec):
    """Resolve a slot name ("Sidearm", "left_arm") or raw value (0, 14).

    Raises EquipSlotUnknown listing every real slot. Never falls back to
    Sidearm: silently equipping the wrong slot would produce a scenario result
    that looks like data.
    """
    table = equip_slot_table()

    if isinstance(spec, bool):
        raise EquipSlotUnknown("slot must be a name or a value, got %r" % spec)

    if isinstance(spec, int):
        for name, entry in table.items():
            if int(entry.value) == spec:
                return entry
        raise EquipSlotUnknown(
            "No EGothicEquipSlot has value %d. Values are non-contiguous "
            "(weapons 0-2, armor 10-19). Known: %s"
            % (spec, {n: int(e.value) for n, e in sorted(table.items())}))

    needle = _slot_key(spec)
    if not needle:
        raise EquipSlotUnknown("empty slot specifier")
    for name, entry in table.items():
        if _slot_key(name) == needle:
            return entry
    raise EquipSlotUnknown(
        "'%s' is not an EGothicEquipSlot. Known: %s"
        % (spec, {n: int(e.value) for n, e in sorted(table.items())}))


def equip_slot_name(entry):
    """The Python name for a slot entry, for readable results."""
    for name, known in equip_slot_table().items():
        if int(known.value) == int(entry.value):
            return name
    return "UNKNOWN(%s)" % int(entry.value)


def guid_string(guid):
    """FGuid as 32 hex digits -- FGuid::ToString(EGuidFormats::Digits).

    Formatted from the A/B/C/D UPROPERTYs (Misc/Guid.h) rather than through
    UKismetGuidLibrary, so this depends on nothing but the struct's own exported
    fields. The int32s are signed in Python and masked back to unsigned here.
    """
    parts = []
    for field in ("a", "b", "c", "d"):
        value = getattr(guid, field, None)
        if value is None:
            raise ItemNotFound(
                "FGuid did not expose '%s' on this build; cannot address items "
                "by instance ID. Fields seen: %s"
                % (field, sorted(n for n in dir(guid) if not n.startswith("_"))))
        parts.append("%08X" % (int(value) & 0xFFFFFFFF))
    return "".join(parts)


def _guid_key(text):
    """Fold a GUID to bare lowercase hex, so braces and dashes do not matter."""
    return "".join(ch for ch in str(text).lower() if ch in "0123456789abcdef")


def definition_path(item):
    """Asset path of an item's definition, or None when the instance is empty.

    An empty FGothicItemInstance with a null Definition is a real, meaningful
    value here -- UnequipSlot_Authority broadcasts exactly that as the "slot is
    now bare" signal (GothicInventoryComponent.cpp) -- so None is a legitimate
    answer from this function and not a failed read.
    """
    definition = getattr(item, "definition", None)
    if definition is None:
        return None
    return definition.get_path_name()


def item_summary(item):
    """A JSON-safe view of one FGothicItemInstance, addressable by instance_id."""
    instance_id = getattr(item, "instance_id", None)
    path = definition_path(item)
    return {
        "instance_id": guid_string(instance_id) if instance_id is not None else None,
        "definition": path,
        "definition_name": path.rsplit(".", 1)[-1] if path else None,
        "gear_power": try_read(lambda: int(item.gear_power)),
        "strain_cost": try_read(lambda: round(float(item.strain_cost), 3)),
        "is_empty": path is None,
    }


def inventory_items(inv):
    """Unequipped items. GetAllItems is BlueprintPure (GothicInventoryComponent.h:109-110)."""
    getter = getattr(inv, "get_all_items", None)
    if getter is None:
        raise InventoryUnavailable(
            "get_all_items missing on %s. It is BlueprintPure at "
            "GothicInventoryComponent.h:109-110, so its absence means stale "
            "Python bindings -- restart the editor after a C++ rebuild."
            % inv.get_name())
    return list(getter())


def equipped_slots(inv):
    """[(slot entry, FGothicItemInstance), ...] for every occupied slot.

    Reads the EquippedItems UPROPERTY (GothicInventoryComponent.h:302-303).
    It is a TArray<FGothicEquippedSlot> and NOT the TMap it used to be, because
    UE cannot replicate a TMap (h:18-20) -- code written against the old shape
    will not work here.
    """
    entries = try_read(lambda: inv.get_editor_property("equipped_items"))
    if isinstance(entries, dict):
        raise InventoryUnavailable(
            "Could not read EquippedItems on %s: %s. It is a BlueprintReadOnly "
            "UPROPERTY at GothicInventoryComponent.h:302-303; a failure here "
            "means the binding shape changed, not that nothing is equipped."
            % (inv.get_name(), entries.get("error")))
    if entries is None:
        return []
    return [(entry.get_editor_property("slot"), entry.get_editor_property("item"))
            for entry in entries]


def equipped_in_slot(inv, slot_entry):
    """The item in a slot, or None when the slot is bare."""
    target = int(slot_entry.value)
    for slot, item in equipped_slots(inv):
        if int(slot.value) == target:
            return item
    return None


def find_inventory_item(inv, instance_id=None, definition=None):
    """Locate an UNEQUIPPED item by instance GUID or by definition asset.

    Mirrors how the UI addresses items: GothicInventoryWidget::MakeUIData
    (GothicInventoryWidget.cpp:258-260) carries InstanceID as the handle, and
    every mutation on the component takes that FGuid.

    Searches the unequipped list only, because that is what EquipItem accepts --
    it uses FindInventoryItem, not FindItem (GothicInventoryComponent.h:103-104).

    The FGuid is returned by handing back the live struct, never by
    reconstructing one: pass `item.instance_id` straight through to EquipItem.
    """
    items = inventory_items(inv)

    if instance_id:
        needle = _guid_key(instance_id)
        if not needle:
            raise ItemNotFound("'%s' contains no hex digits, so it is not a "
                               "GUID." % instance_id)
        for item in items:
            if _guid_key(guid_string(item.instance_id)) == needle:
                return item
        raise ItemNotFound(
            "No unequipped item with instance ID '%s'. Carrying %d item(s): %s. "
            "(Equipped items are deliberately not searched -- EquipItem only "
            "accepts unequipped ones, GothicInventoryComponent.h:103-104.)"
            % (instance_id, len(items), [item_summary(i) for i in items]))

    if definition:
        needle = str(definition).lower()
        matches = [i for i in items
                   if (definition_path(i) or "").lower() == needle
                   or (definition_path(i) or "").rsplit(".", 1)[-1].lower() == needle]
        if len(matches) == 1:
            return matches[0]
        if not matches:
            raise ItemNotFound(
                "No unequipped item from definition '%s'. Carrying %d item(s): "
                "%s. NOTE: the harness cannot mint items -- RollInstance is not "
                "a UFUNCTION and FGothicItemInstance is fully BlueprintReadOnly "
                "-- so if the definition was never granted, add it to "
                "StartingItemDefs on the PlayerState BP (editor-side change)."
                % (definition, len(items), [item_summary(i) for i in items]))
        raise ItemNotFound(
            "'%s' matches %d unequipped items; address one by instance_id "
            "instead: %s" % (definition, len(matches),
                             [item_summary(m) for m in matches]))

    raise ItemNotFound("supply either 'instance_id' or 'definition' to "
                       "identify the item")


def inventory_authority(inv):
    """True when this component's owner may mutate inventory state directly.

    Read off the OWNING ACTOR, not the component:
    UGothicInventoryComponent::HasInventoryAuthority is a plain C++ method with
    no UFUNCTION macro (GothicInventoryComponent.h:76-77), so it is absent from
    the Python bindings. AActor::HasAuthority is BlueprintCallable, and the
    PlayerState's role is exactly what the component's own check resolves to.

    This is THE value that decides whether an equip/unequip return code is a
    verdict or just "request sent" (GothicInventoryComponent.h:28-34).
    """
    owner = inv.get_owner()
    if owner is None:
        raise InventoryUnavailable(
            "%s has no owning actor, so its authority cannot be determined. "
            "An ownerless component counts as authority in C++ "
            "(GothicInventoryComponent.h:76), but that case is a CDO, not "
            "anything a PIE scenario should be driving." % inv.get_name())
    return bool(owner.has_authority())


def inventory_snapshot(inv):
    """Everything a verifier needs to address and assert on the inventory."""
    items = inventory_items(inv)
    return {
        "component": inv.get_name(),
        "owner": try_read(lambda: inv.get_owner().get_name()),
        "has_authority": inventory_authority(inv),
        "item_count": len(items),
        "max_inventory_size": try_read(
            lambda: int(inv.get_editor_property("max_inventory_size"))),
        "items": [item_summary(i) for i in items],
        "equipped": [{"slot": equip_slot_name(slot), "slot_value": int(slot.value),
                      "item": item_summary(item)}
                     for slot, item in equipped_slots(inv)],
        "current_strain": try_read(lambda: round(float(inv.get_current_strain()), 3)),
        "resonance_cap": try_read(
            lambda: round(float(inv.get_editor_property("resonance_cap")), 3)),
    }


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
    # Project cvars/commands registered under the Vigil namespace. Same trust
    # class as "gothic." above: our own code, no engine state at risk.
    #   vigil.Deterministic / vigil.DeterministicSeed  Game/GothicDeterminism.cpp
    #   Vigil.OpenBleedGates                           AI/GothicBleedGate.cpp
    # (matching is case-insensitive, so the capitalised registration is covered)
    "vigil.",
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
