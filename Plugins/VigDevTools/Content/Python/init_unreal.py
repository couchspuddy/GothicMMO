"""
init_unreal.py

Unreal executes any init_unreal.py found on a Content/Python path at editor
startup.

Importing a toolset module is NOT enough to expose it. @unreal.uclass() creates
the class, but the Toolset Registry only publishes toolsets handed to it
explicitly — the engine's own toolsets do this via
toolset_registry.registration.Registration (see
EditorToolset/Content/Python/init_unreal.py, which calls
`toolsets._registration.register()`).

Without the register() call below, a toolset loads cleanly, logs nothing wrong,
and never appears in the agent's tool list. ModelContextProtocol.RefreshTools
does not help either: it re-broadcasts the existing registry rather than
re-scanning Python for new ToolsetDefinition subclasses.

Each toolset is imported separately and on purpose. A syntax error in the combat
toolset should not silently take the read-only toolset down with it -- losing
observation while keeping mutation would be exactly the wrong failure. The same
applies to registration: each class is registered independently.

_REGISTRATION is kept at module scope deliberately — it owns the registered
classes, and letting it fall out of scope risks them being collected.

If a toolset does not appear in the agent's tool list:
  1. Check the Output Log for the lines this file prints.
  2. Run ModelContextProtocol.RefreshTools in the editor console.
  3. Reconnect the agent -- a connected client caches the tool schema.
"""

import importlib

import unreal

# (module name, class name) — imported and registered independently.
#
# Anything added to Content/Python is imported by Unreal but NOT published to
# the agent unless it is listed here: the Toolset Registry only exposes classes
# handed to Registration(). vig_blackboard_tools, vig_encounter_tools and
# vig_gas_tools sat on disk unregistered through a whole debugging session,
# which meant the boss's Phase 2 stall had to be inferred from position traces
# instead of just reading bTransitionPending out of the blackboard. If you drop
# a new toolset module in, add it here in the same change.
_TOOLSETS = (
    ("vigil_pie_toolset", "VigilPIETools"),
    ("vigil_combat_toolset", "VigilCombatDrive"),
    ("vig_blackboard_tools", "VigBlackboardTools"),
    ("vig_encounter_tools", "VigEncounterTools"),
)
# vig_gas_tools is deliberately NOT registered: attributes, active effects,
# granted abilities and owned tags are already covered by the engine's own
# GASToolsets.AbilitySystemInspectorToolset, which is maintained upstream and
# takes actor pointers directly. A second, hand-rolled copy of the same four
# reads is a liability, not a convenience.

_REGISTRATION = None
_registered_classes = []

try:
    from toolset_registry.registration import Registration
except Exception as exc:  # registry plugin missing/disabled
    Registration = None
    unreal.log_error("[VigilTools] toolset_registry unavailable, no toolsets "
                     "will be registered: %s" % exc)

for _module_name, _class_name in _TOOLSETS:
    try:
        _module = importlib.import_module(_module_name)
        _cls = getattr(_module, _class_name)
        _registered_classes.append(_cls)
        unreal.log("[VigilTools] %s loaded." % _module_name)
    except Exception as exc:
        # Never let one toolset's import failure block the others or startup.
        unreal.log_error("[VigilTools] Failed to load %s: %s" % (_module_name, exc))

if Registration is not None and _registered_classes:
    _REGISTRATION = Registration(_registered_classes)
    if _REGISTRATION.register():
        unreal.log("[VigilTools] Registered %d toolset(s) with the Toolset Registry: %s"
                   % (len(_registered_classes),
                      ", ".join(c.__name__ for c in _registered_classes)))
    else:
        # is_available() returned False — the registry had not initialized yet.
        unreal.log_warning(
            "[VigilTools] Toolset Registry unavailable at startup; no toolsets were "
            "registered. Check that the ToolsetRegistry plugin is enabled.")

unreal.log("[VigilTools] Startup complete. Run ModelContextProtocol.RefreshTools "
           "if tools are not visible, then reconnect the client.")
