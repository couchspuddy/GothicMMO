"""
vig_asset_common.py

Shared helpers for the ASSET-editing Vigil toolsets (vig_bt_edit_tools,
vig_anim_notify_tools). Deliberately separate from vigil_pie_common, which is
about a running PIE world -- nothing in here needs or wants a game world, and
mixing the two would mean an asset tool failing because PIE is not running.

Design rule inherited from the rest of this directory: a read that cannot
resolve raises a NAMED error. It never returns an empty list that the caller
mistakes for "the asset has nothing". Four broken diagnostic tools in this
project all failed the same way -- silently, plausibly, and expensively.
"""

import json

import unreal


# -----------------------------------------------------------------------------
# Errors. Named so an agent reading the transcript can tell "I asked for the
# wrong thing" apart from "this engine build does not expose that".
# -----------------------------------------------------------------------------

class VigAssetNotFound(LookupError):
    """The asset path / name did not resolve to a loaded asset."""


class VigNodeNotFound(LookupError):
    """The requested node identifier is not in this asset."""


class VigUnreflected(RuntimeError):
    """An API this tool depends on is not exposed to Python on this build."""


class VigSaveFailed(RuntimeError):
    """The asset was modified in memory but could not be written to disk."""


# -----------------------------------------------------------------------------
# Asset resolution
# -----------------------------------------------------------------------------

def load_asset(path_or_name, expected_class=None, class_path=None):
    """Load an asset by full object path, package path, or bare asset name.

    Bare names are resolved through the AssetRegistry rather than by guessing
    a folder, because these assets move (BT_EnemyCombat lives under
    /Game/Blueprints/AI, Melee_A_Montage under /Game/ParagonKhaimera/...).

    Args:
        path_or_name: '/Game/Path/Asset.Asset', '/Game/Path/Asset', or 'Asset'.
        expected_class: Optional unreal class; the result is type-checked.
        class_path: Optional (module, class) tuple used to narrow the registry
            search for bare names, e.g. ('/Script/AIModule', 'BehaviorTree').

    Raises:
        VigAssetNotFound: nothing resolved, or several assets share the name.
    """
    asset = None
    if path_or_name.startswith("/"):
        try:
            asset = unreal.EditorAssetLibrary.load_asset(path_or_name)
        except Exception as exc:
            raise VigAssetNotFound(
                "load_asset('%s') raised %s: %s"
                % (path_or_name, type(exc).__name__, exc))
    else:
        matches = _registry_search(path_or_name, class_path)
        if not matches:
            raise VigAssetNotFound(
                "No asset named '%s' in the AssetRegistry%s. Pass a full path "
                "like '/Game/Blueprints/AI/BT_EnemyCombat' if the name is "
                "unusual."
                % (path_or_name,
                   "" if class_path is None else " of class %s" % class_path[1]))
        if len(matches) > 1:
            raise VigAssetNotFound(
                "'%s' is ambiguous, matches: %s" % (path_or_name, matches))
        asset = unreal.EditorAssetLibrary.load_asset(matches[0])

    if asset is None:
        raise VigAssetNotFound("'%s' did not resolve to a loaded asset."
                               % path_or_name)
    if expected_class is not None and not isinstance(asset, expected_class):
        raise VigAssetNotFound(
            "'%s' is a %s, expected a %s."
            % (path_or_name, asset.get_class().get_name(),
               expected_class.__name__))
    return asset


def _registry_search(asset_name, class_path):
    """Package paths under /Game of every asset whose name matches exactly.

    Uses IAssetRegistry::GetAssetsByClass, NOT GetAssets with an FARFilter:
    FARFilter::ClassPaths is not an editable UPROPERTY, so building a filter
    from Python fails with "Property 'ClassPaths' ... cannot be edited on
    instances" -- both as a constructor kwarg and via set_editor_property.
    GetAssetsByClass is a plain UFUNCTION taking an FTopLevelAssetPath and has
    no such restriction. (Learned the hard way; do not "simplify" this back to
    an ARFilter.)

    EditorAssetLibrary.list_assets is also avoided: it walks and resolves far
    more than needed.
    """
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    if class_path is not None:
        found = registry.get_assets_by_class(
            unreal.TopLevelAssetPath(class_path[0], class_path[1]), True)
    else:
        found = registry.get_assets_by_path("/Game", recursive=True)

    out = []
    for data in found:
        package = str(data.package_name)
        if str(data.asset_name) == asset_name and package.startswith("/Game"):
            out.append(package)
    return sorted(set(out))


# -----------------------------------------------------------------------------
# Saving
# -----------------------------------------------------------------------------

def save(asset, context):
    """Mark dirty and write the asset to disk. Raises rather than reporting ok.

    UObject::Modify (reflected as Object.modify) is what flags the package
    dirty; EditorAssetLibrary.save_loaded_asset does the write. Calling save
    without modify can no-op on an asset the editor does not believe changed --
    the same class of silent no-op that level saves in this project exhibit.
    """
    try:
        asset.modify(True)
    except Exception as exc:
        raise VigUnreflected(
            "Object.modify is not callable on %s (%s: %s). Without it the "
            "package may not be flagged dirty and the save silently no-ops."
            % (context, type(exc).__name__, exc))

    try:
        ok = unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    except Exception as exc:
        raise VigSaveFailed("save_loaded_asset raised on %s: %s: %s"
                            % (context, type(exc).__name__, exc))
    if not ok:
        raise VigSaveFailed(
            "save_loaded_asset returned False for %s. The asset is modified in "
            "memory but NOT on disk -- check source control / read-only flags."
            % context)
    return True


# -----------------------------------------------------------------------------
# Result plumbing. JSON-in-a-string, for the reason spelled out in
# vigil_pie_common.as_json: this plugin's schema generator rejects
# unparameterised containers at TOOL DISCOVERY time, so a tool annotated
# `-> dict` takes the whole module out of the tool list with no call-site error.
# -----------------------------------------------------------------------------

def as_json(payload):
    return json.dumps(payload, indent=1, default=str)


def describe(value):
    """Render a property value as something JSON can carry.

    Structs (FAIDataProviderBoolValue, FBlackboardKeySelector) are the common
    case on BT nodes and are the whole reason a raw str() is not enough: an
    agent needs to see that bTrackMovingGoal has a DefaultValue and a Key, not
    a bare 'True'.
    """
    if value is None:
        return None
    if isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, unreal.Name):
        return str(value)
    if isinstance(value, (unreal.Array, list, tuple)):
        return [describe(v) for v in value]
    if isinstance(value, unreal.StructBase):
        out = {}
        for field in _struct_fields(value):
            try:
                out[field] = describe(value.get_editor_property(field))
            except Exception as exc:
                out[field] = "<unreadable: %s>" % type(exc).__name__
        return out or str(value)
    if isinstance(value, unreal.Object):
        return {"object": value.get_name(),
                "class": value.get_class().get_name(),
                "path": value.get_path_name()}
    return str(value)


def try_name(obj, property_name):
    """Read a FName-ish property as a string, or an error object.

    Used only for labels (NotifyName, NodeName) where a missing value is
    cosmetic. Never for a value a decision is made on.
    """
    try:
        return str(obj.get_editor_property(property_name))
    except Exception as exc:
        return "<unreadable: %s>" % type(exc).__name__


def try_float(obj, property_name):
    """Read a float property, or an error object. Same caveat as try_name."""
    try:
        return round(float(obj.get_editor_property(property_name)), 4)
    except Exception as exc:
        return "<unreadable: %s>" % type(exc).__name__


# Structs the engine did not generate a Python type for expose NO properties on
# their class, so dir() finds nothing and they read as opaque. FValueOrBBKey_*
# (UE 5.6+, used for BT node parameters like AcceptableRadius) is exactly this
# case. These are the field names worth probing by hand when that happens.
_PROBE_FIELDS = ("default_value", "key")


def _struct_fields(struct_value):
    """Field names of a reflected struct instance.

    unreal.StructBase does not expose a field list, so this reads the generated
    Python properties off the type. When the type has none -- an opaque struct
    like FValueOrBBKey_Float -- it falls back to probing _PROBE_FIELDS, which is
    how the DefaultValue/Key pair on BT node parameters is reached.
    """
    names = []
    for name in dir(type(struct_value)):
        if name.startswith("_"):
            continue
        if isinstance(getattr(type(struct_value), name, None), property):
            names.append(name)
    if names:
        return names

    for name in _PROBE_FIELDS:
        try:
            struct_value.get_editor_property(name)
        except Exception:
            continue
        names.append(name)
    return names
