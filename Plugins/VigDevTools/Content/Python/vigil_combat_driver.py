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


def _step_world(world, step):
    """The world a step addresses: its optional "world" field, else the run's.

    Every scenario runs against the SERVER world and that stays the default, so
    a step with no "world" field behaves exactly as it always has.

    "world": "client" exists for READS and for non-ability actions (aim, move,
    teleport, snapshots) against a second PIE instance. It is NOT an activation
    route: fire/melee/activate_slot refuse a non-authoritative pawn outright,
    because driving a LocalPredicted ability on one from editor Python recurses
    through the in-process Server RPC and stack-overflows the editor. See
    _activate. Ability steps address the SERVER world's pawn for that player.

    See the per-world block in vigil_pie_common for the two traps that come with
    a client world (labels are per-world; a client world is not authoritative).
    """
    spec = step.get("world")
    if spec is None:
        return world
    return common.resolve_world(spec)


def _actor(world, step, key="actor"):
    """Resolve a step's actor label, in the step's world.

    The label is resolved in whichever world the step addresses, which matters:
    the same name identifies different pawns on the server and client worlds.
    """
    label = step.get(key)
    if not label:
        raise ValueError("step is missing required field '%s'" % key)
    return common.resolve_actor(_step_world(world, step), label)


def _activate(pawn, slot_name, result_key):
    """Shared slot activation. OnFire/OnMelee used to exist as direct C++ calls
    and were deleted as duplicate damage paths -- they applied damage in
    ADDITION to the ability, on an ECC_Pawn capsule trace with no authority
    check. Everything now goes through the ability, which is also what input
    drives, so driving the slot exercises the real path.

    Returns the whole step payload rather than a bare bool. A false under
    `result_key` used to be unreadable: TryActivateAbilityBySlot answers the
    same silent false whether NO ability is bound to the slot or the bound one
    refused to activate (GothicAbilitySystemComponent.cpp, the bare
    `return false` after the map lookup misses). On a false the payload also
    carries `slot_bound` and `registered_slot_count` off the ASC, so the empty
    client-world map names itself instead of looking like a refused cast.

    Deliberately no fallback route: if the slot path is broken, the step must
    fail, not quietly succeed by some other means.

    CLIENT-WORLD ACTIVATION IS REFUSED HERE, and that refusal is the point.
    Editor Python runs under FEditorScriptExecutionGuard, which sets
    GAllowActorScriptExecutionInEditor; AActor::GetFunctionCallspace then answers
    Local for every RPC before it ever looks at the net role. Activating a
    LocalPredicted ability on a non-authoritative ASC from here therefore calls
    ServerTryActivateAbility IN PROCESS, which re-enters InternalTryActivateAbility
    still non-authoritative and recurses without bound inside a single frame --
    one dependent prediction key per lap, ~200MB/min of log, then a stack overflow
    that kills the editor. This has happened; it cost a session.

    So the authority check happens BEFORE the ASC is touched. C++ carries the same
    tripwire (GothicAbilitySystemComponent::TryActivateAbilityBySlot), but the
    harness must not rely on it: an older binary would still recurse.

    REMOTE LOCALPREDICTED -- why a `true` here is not always an activation
    ---------------------------------------------------------------------
    Driving the SERVER world's pawn for a second player passes the authority check
    and then reports a `true` that means nothing ran. GAS's TryActivateAbility
    defaults bAllowRemoteActivation to true, and for a LocalOnly/LocalPredicted
    ability whose avatar is not locally controlled it calls
    ClientTryActivateAbility and returns true without activating
    (AbilitySystemComponent_Abilities.cpp:1621-1627). A ServerInitiated ability --
    which is the GothicGameplayAbility default, so most slots -- genuinely does
    run in that same situation and also answers true, so the bool alone cannot
    tell them apart. IsSlotAbilityLocallyPredicted does, and the false reported
    below is the honest answer for the LocalPredicted case (GA_Fire, PRIMARY_FIRE).
    """
    asc = pawn.get_gothic_asc()
    if asc is None:
        raise LookupError("no ASC on %s" % pawn.get_name())

    # has_authority off the pawn, matching common.inventory_authority's route:
    # AActor::HasAuthority is BlueprintCallable, the C++ helpers are not UFUNCTIONs.
    if not bool(pawn.has_authority()):
        return {
            result_key: False,
            "slot": slot_name,
            "refused": "non-authoritative-world",
            "actor": pawn.get_name(),
            "failure_means":
                "REFUSED WITHOUT CALLING THE ASC. %s is not authoritative in its "
                "resolved world. Activating an ability on a non-authoritative pawn "
                "from editor Python recurses through the in-process "
                "ServerTryActivateAbility RPC (the editor script guard forces a Local "
                "callspace) and stack-overflows the editor. Drive THIS PLAYER'S "
                "SERVER-WORLD pawn instead -- its label differs from the client "
                "world's, so resolve it with list_combatants(world=\"server\") rather "
                "than reusing a client label." % pawn.get_name(),
        }

    slot_enum = common.ability_slot(slot_name)

    # Read local control BEFORE activating: it decides whether a `true` below is
    # a real activation or the engine's remote-activation stub. See REMOTE
    # LOCALPREDICTED in the docstring.
    locally_controlled = common.try_read(lambda: bool(pawn.is_locally_controlled()))

    activated = bool(asc.try_activate_ability_by_slot(slot_enum))

    if activated and locally_controlled is False:
        # is_slot_ability_locally_predicted separates the two meanings of true.
        # try_read, not a direct call: an older binary does not have it, and the
        # honest answer there is to leave the raw result alone.
        local_predicted = common.try_read(
            lambda: bool(asc.is_slot_ability_locally_predicted(slot_enum)))

        if local_predicted is True:
            return {
                result_key: False,
                "slot": slot_name,
                "actor": pawn.get_name(),
                "gas_returned": True,
                "refused": "local-predicted-on-remote-pawn",
                "failure_means":
                    "NOTHING RAN, despite GAS answering true. %s is authoritative "
                    "here but is NOT locally controlled, and the ability in this "
                    "slot is LocalPredicted. TryActivateAbility defaults "
                    "bAllowRemoteActivation to true, so for a Local* ability on a "
                    "remote pawn it fires ClientTryActivateAbility at the owning "
                    "client and returns true UNCONDITIONALLY, without activating "
                    "anything (AbilitySystemComponent_Abilities.cpp:1621-1627). "
                    "Under the editor script guard that RPC does not even leave "
                    "the process -- callspace is forced Local, so it re-enters "
                    "this same ASC, still not locally controlled, and fails with "
                    "\"Can't activate LocalOnly or LocalPredicted ability ... when "
                    "not local.\" in the log. THERE IS CURRENTLY NO EDITOR-PYTHON "
                    "ROUTE FOR SECOND-PLAYER ABILITY ACTIVATION: the client world "
                    "is refused above (it would recurse), and the server world is "
                    "refused by GAS design here. Do not read a P2 fire/ability "
                    "step as evidence of anything." % pawn.get_name(),
            }

    payload = {result_key: activated}

    if not activated:
        # try_read, not a direct call: GetRegisteredAbilitySlotCount is only
        # BlueprintPure as of the client-map fix, so an older binary answers
        # with an error object here rather than raising through the scenario.
        count = common.try_read(
            lambda: int(asc.get_registered_ability_slot_count()))
        payload["slot"] = slot_name
        payload["registered_slot_count"] = count
        payload["slot_bound"] = (count > 0) if isinstance(count, int) else count
        payload["failure_means"] = (
            "no ability is bound to any slot on this ASC -- on a client world "
            "that is the replicated-map defect, not a refused activation"
            if count == 0 else
            "an ability IS bound; activation itself was refused (cooldown, "
            "cost, blocking tag, or not locally controlled for a "
            "LocalPredicted ability)")

    return payload


@_action("fire")
def _a_fire(world, step):
    """Fire the equipped weapon. Runs the real trace and damage path.

    SERVER WORLD ONLY. A "world": "client" step is refused before the ASC is
    touched -- client-world activation recurses through the in-process Server RPC
    under the editor script guard and kills the editor. Fire the second player's
    SERVER-world pawn (resolve its label with list_combatants(world="server")).
    """
    pawn = _actor(world, step)
    return _activate(pawn, "PRIMARY_FIRE", "fired")


@_action("melee")
def _a_melee(world, step):
    """Melee attack. SERVER WORLD ONLY -- see _activate for why a client-world
    step is refused rather than run."""
    pawn = _actor(world, step)
    return _activate(pawn, "LIGHT_ATTACK", "melee")


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


@_action("set_steadfast")
def _a_set_steadfast(world, step):
    """Force Steadfast to `value` (0..MaxSteadfast). Dev harness only.

    WHY THIS EXISTS: nothing in the game drains Steadfast on demand. Reloads
    cost 1-3 points and convert_steadfast only pays out when the pawn can
    afford the cost, so the 0/1/2-pip band thresholds could not be driven to a
    chosen value -- they were untestable, not unverified.

    HOW IT WRITES, and why not a GameplayEffect: a GE applied with a default
    FPredictionKey silently applies nothing and returns an invalid handle, with
    no log. So this uses the two routes the C++ itself uses.

      DOWN  -- UGothicSteadfastComponent::TryConvertSteadfast(delta, 0.0)
               (BlueprintCallable, GothicSteadfastComponent.h:38-39). It
               subtracts exactly `delta` via the same AttributeSet->SetSteadfast
               the fill tick uses, and the ammo grant is the caller's job, so
               passing 0.0 makes it a pure drain with no ammo side effect.
      UP    -- write the FGameplayAttributeData UPROPERTY on the attribute set
               (BaseValue and CurrentValue together), which is the same field
               SetSteadfast lands on. This is the raw route; it is acceptable
               in a dev tool and nowhere else. It is verified by re-reading,
               and raises rather than reporting a write it did not achieve.

    AUTHORITY ONLY. The fill tick is server-authoritative
    (GothicSteadfastComponent.cpp:40-42), so a client-side poke is overwritten
    on the next replication.
    """
    pawn = _actor(world, step)

    component = common.component(pawn, "GothicSteadfastComponent")
    if component is None:
        raise LookupError(
            "%s has no UGothicSteadfastComponent; components present: %s"
            % (pawn.get_name(), common.component_names(pawn)))

    if not bool(common.try_read_or_none(lambda: pawn.has_authority())):
        raise RuntimeError(
            "set_steadfast needs authority -- Steadfast accumulation is "
            "server-side (GothicSteadfastComponent.cpp:40-42) and a client "
            "write is overwritten on the next replication. Target the "
            "server-world pawn (list_combatants(world=\"server\")).")

    if "value" not in step:
        raise ValueError("set_steadfast needs a \"value\" (0..MaxSteadfast)")

    maximum = float(component.get_max_steadfast())
    target = max(0.0, min(float(step["value"]), maximum))
    before = float(component.get_current_steadfast())

    if target < before:
        component.try_convert_steadfast(before - target, 0.0)
        route = "TryConvertSteadfast drain"
    elif target > before:
        route = _raise_steadfast(pawn, target)
    else:
        route = "no change"

    after = float(component.get_current_steadfast())
    if abs(after - target) > 0.01:
        raise RuntimeError(
            "set_steadfast asked for %.3f but the attribute reads %.3f after "
            "the write (was %.3f, route: %s). Do not treat the pip state as "
            "set." % (target, after, before, route))

    return {"requested": round(float(step["value"]), 3),
            "clamped_to": round(target, 3),
            "before": round(before, 3),
            "after": round(after, 3),
            "max": round(maximum, 3),
            "route": route}


def _raise_steadfast(pawn, target):
    """Push Steadfast UP by writing the attribute data struct. Returns the route.

    Python hands back a COPY of an FGameplayAttributeData, so the copy is
    edited and then written back to the UPROPERTY -- editing the copy in place
    changes nothing on the actor, which is the failure mode to watch for here.
    BaseValue and CurrentValue are both set: the fill tick reads CurrentValue,
    while replication and any later aggregation start from BaseValue, and
    leaving them disagreeing produces a value that snaps back under load.
    """
    attrs = common.attribute_set(pawn)
    if attrs is None:
        raise LookupError(
            "No UGothicAttributeSet on %s or its PlayerState, so Steadfast "
            "cannot be raised." % pawn.get_name())

    data = attrs.get_editor_property("steadfast")
    data.set_editor_property("base_value", target)
    data.set_editor_property("current_value", target)
    attrs.set_editor_property("steadfast", data)
    return "attribute data write (BaseValue+CurrentValue)"


@_action("swap_weapon")
def _a_swap(world, step):
    pawn = _actor(world, step)
    index = int(step.get("index", 0))
    pawn.swap_weapon(index)
    return {"swapped_to": index}


# --------------------------------------------------------------------------
# Inventory equip / unequip
#
# swap_weapon above only moves ActiveWeaponIndex between weapons the character
# already holds. It never touches the inventory, so it cannot exercise any of
# the equipment code: the OnItemEquipped broadcast, OnEquipmentChanged's
# slot-clearing branch, the auto-reload trigger, or the Server RPC round trip.
# These two actions do, by calling the same functions the inventory UI calls.
# --------------------------------------------------------------------------

def _inventory(world, step):
    return common.inventory_component(_actor(world, step))


def _equip_result(inv, slot_entry, before, called, authority):
    """Shared result shape: what was asked, what the API said, what changed.

    `changed` is the load-bearing field. On a client `called` is meaningless
    (see below), so the only honest evidence that anything happened is the
    before/after diff -- and even that lands a round trip later, not in this
    result. The replication test reads it on a LATER step, not this one.
    """
    after = common.equipped_in_slot(inv, slot_entry)
    before_summary = common.item_summary(before) if before is not None else None
    after_summary = common.item_summary(after) if after is not None else None
    return {
        "slot": common.equip_slot_name(slot_entry),
        "returned": called,
        "authority": authority,
        "return_means": (
            "verdict -- this component has authority, so the bool is the real "
            "outcome" if authority else
            "REQUEST SENT ONLY -- on a client EquipItem/UnequipSlot forward to "
            "their Server RPC and return true unconditionally "
            "(GothicInventoryComponent.h:118-124, .cpp routers). Do NOT read "
            "this true as success; re-read the slot on a later step and "
            "compare."),
        "equipped_before": before_summary,
        "equipped_after": after_summary,
        "changed": before_summary != after_summary,
    }


@_action("equip_item")
def _a_equip_item(world, step):
    """Equip an inventory item through UGothicInventoryComponent::EquipItem.

    THE REAL PATH, ON PURPOSE. This is the same BlueprintCallable the inventory
    UI drives (GothicInventoryWidget calls straight through to it), not a poke
    at EquippedItems. Poking the array would prove nothing about the code that
    ships: it would skip the authority router, the strain gate, and the
    OnItemEquipped broadcast that AGothicPlayerCharacter::OnEquipmentChanged is
    bound to -- which is where the weapon slot, mesh, crosshair, ammo and
    auto-reload behaviour all actually live.

    Address the item with EITHER:
      instance_id  32 hex digits from an inventory snapshot (braces/dashes ok)
      definition   asset path or bare asset name, e.g. DA_Weapon_HeavyMeleeRig

    The slot is NOT a parameter: the definition owns it
    (UGothicItemDefinition::EquipSlot), exactly as it does for the UI.
    """
    inv = _inventory(world, step)
    item = common.find_inventory_item(
        inv,
        instance_id=step.get("instance_id"),
        definition=step.get("definition"))

    summary = common.item_summary(item)
    definition = item.definition
    if definition is None:
        raise common.ItemNotFound(
            "Item %s has a null Definition, so it has no slot to equip into."
            % summary["instance_id"])
    slot_entry = definition.get_editor_property("equip_slot")
    before = common.equipped_in_slot(inv, slot_entry)
    authority = common.inventory_authority(inv)

    # The FGuid is handed straight back from the live struct and never rebuilt
    # from the string -- constructing an FGuid in Python is not something to
    # rely on, and there is no reason to.
    called = bool(inv.equip_item(item.instance_id))

    result = _equip_result(inv, slot_entry, before, called, authority)
    result["requested"] = summary
    if authority and not called:
        result["refusal"] = (
            "EquipItem returned false on the authority. The documented causes "
            "are: the ID is not in the unequipped inventory, or the item would "
            "exceed the Resonance Strain cap (GothicInventoryComponent.h:114-124). "
            "Note the strain gate is UNREACHABLE with current content -- max "
            "wearable strain is 50 against a cap of 100 (h:182-192) -- so "
            "suspect the ID first.")
    return result


@_action("unequip_slot")
def _a_unequip_slot(world, step):
    """Unequip a slot through UGothicInventoryComponent::UnequipSlot.

    This is the action that exercises OnEquipmentChanged's SLOT-CLEARING
    branch. UnequipSlot_Authority broadcasts OnItemEquipped with an EMPTY
    FGothicItemInstance as the "slot is now bare" signal, and OnEquipmentChanged
    reads Item.Definition being null to clear the weapon slot
    (GothicInventoryComponent.cpp, UnequipSlot_Authority). Nothing else in this
    harness reaches that branch.

    Fields: actor, slot (name like "Sidearm"/"left_arm", or raw value 0-2/10-19).

    FALSE IS A REAL, DOCUMENTED OUTCOME HERE, not an error: on the authority
    UnequipSlot refuses when the inventory is already at MaxInventorySize,
    because the item would have nowhere to go, and it leaves the item equipped
    (GothicInventoryComponent.cpp, UnequipSlot_Authority). The result explains
    which case fired rather than raising, so a scenario testing the full-
    inventory refusal can assert on it.
    """
    inv = _inventory(world, step)
    if "slot" not in step:
        raise ValueError(
            "unequip_slot needs a 'slot'. Known: %s"
            % {n: int(e.value)
               for n, e in sorted(common.equip_slot_table().items())})
    slot_entry = common.equip_slot(step["slot"])

    before = common.equipped_in_slot(inv, slot_entry)
    authority = common.inventory_authority(inv)
    called = bool(inv.unequip_slot(slot_entry))

    result = _equip_result(inv, slot_entry, before, called, authority)
    if authority and not called:
        items = len(common.inventory_items(inv))
        cap = common.try_read(
            lambda: int(inv.get_editor_property("max_inventory_size")))
        if before is None:
            result["refusal"] = (
                "the slot was already empty -- UnequipSlot returns false when "
                "IndexOfEquippedSlot finds nothing.")
        elif isinstance(cap, int) and items >= cap:
            result["refusal"] = (
                "INVENTORY FULL (%d/%d). The cap is checked BEFORE anything is "
                "torn down, so the item is still equipped and the state is "
                "intact -- this is the designed refusal, not a failure."
                % (items, cap))
        else:
            result["refusal"] = (
                "UnequipSlot returned false with the slot occupied and the "
                "inventory at %s/%s. That combination is not explained by the "
                "documented refusals; treat it as a finding, not a harness "
                "problem." % (items, cap))
    return result


@_action("inventory_snapshot")
def _a_inventory_snapshot(world, step):
    """Dump the inventory: instance IDs to address, plus equipped state.

    Read-only. Run this before an equip step to get instance_ids, and after one
    to assert on the result -- particularly on a client, where the equip call's
    own return value is not a verdict.
    """
    return common.inventory_snapshot(_inventory(world, step))


@_action("grant_test_items")
def _a_grant_test_items(world, step):
    """Fill the inventory with the C++ debug kit, so equip steps have something
    to move.

    Calls UGothicInventoryComponent::DebugSpawnTestItems (BlueprintCallable,
    GothicInventoryComponent.h:284-285), which rolls ten transient ARMOR items
    across the Salvage..Pure tiers.

    KNOW WHAT THIS CANNOT DO: the kit is armor only -- there is no weapon in it,
    and it cannot be pointed at a specific definition asset. It will not produce
    DA_Weapon_HeavyMeleeRig or any other authored weapon. Nothing in the Python
    bindings can: RollInstance and InitializePickup are not UFUNCTIONs, and
    every FGothicItemInstance field is BlueprintReadOnly, so an instance cannot
    be minted or filled in from here at all.

    Authority-only. AddItem refuses on a client with a log
    (GothicInventoryComponent.h:81-87), so on a client this silently grants
    nothing -- which is why authority is reported and asserted below.
    """
    inv = _inventory(world, step)
    if not common.inventory_authority(inv):
        raise common.InventoryUnavailable(
            "grant_test_items needs authority: DebugSpawnTestItems calls "
            "AddItem, which is authority-only and refuses on a client "
            "(GothicInventoryComponent.h:81-87). Run this on the server/host.")

    granter = getattr(inv, "debug_spawn_test_items", None)
    if granter is None:
        raise common.InventoryUnavailable(
            "debug_spawn_test_items missing on %s; it is BlueprintCallable at "
            "GothicInventoryComponent.h:284-285, so this means stale bindings."
            % inv.get_name())

    before = len(common.inventory_items(inv))
    granter()
    after = len(common.inventory_items(inv))
    if after <= before:
        raise common.InventoryUnavailable(
            "DebugSpawnTestItems added nothing (%d items before and after). "
            "The usual cause is a full inventory -- AddItem refuses at "
            "MaxInventorySize." % before)
    return {"items_before": before, "items_after": after,
            "granted": after - before,
            "note": "armor only; no weapon definitions are reachable from here"}


@_action("activate_slot")
def _a_activate(world, step):
    """Activate an ability by slot through the ASC, bypassing input.

    SERVER WORLD ONLY. A "world": "client" step is refused before the ASC is
    touched -- client-world activation recurses through the in-process Server RPC
    under the editor script guard and kills the editor. Activate the second
    player's SERVER-world pawn (list_combatants(world="server") for its label).
    """
    pawn = _actor(world, step)
    return _activate(pawn, step.get("slot", "ABILITY1"), "activated")


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


def _apply_damage(target, amount, effect_path, instigator=None):
    """Push damage through the REAL pipeline: GE_Damage carrying a Data.Damage
    SetByCaller, applied to the target's ASC.

    Deliberately not a health poke. GothicAttributeSet::PostGameplayEffectExecute
    is what converts IncomingDamage into a health change, and it also rolls
    evasion and subtracts Defense on the way through. Writing Health directly
    would skip all of that and prove nothing about the systems under test.

    Because mitigation applies, `amount` is RAW damage, not the health delta:
    a Thrall with 8 Defense loses (amount - 8). Pass generously when the intent
    is simply to kill.

    WHO DEALT IT — TWO SHAPES, TWO FORMULAS
    ---------------------------------------
    WITH an `instigator`:   health change = max(1, Raw + instigator AttackPower
                            - target Defense)
    WITHOUT an instigator:  health change = max(1, Raw - target Defense)

    The two shapes build their context differently and deliberately so.

    An instigated hit mirrors the project's blessed route,
    UGothicAbilitySystemComponent::ApplyEffectToASC: the spec is made off the
    ATTACKER's ASC and applied to the target's, and the context comes from
    UGothicAbilitySystemComponent::MakeDamageContext
    (GothicAbilitySystemComponent.cpp:255-276) — the same static every C++ damage
    site uses, now exposed to Python as a BlueprintCallable static.

    THE NOTE THAT USED TO STAND HERE WAS WRONG, and instigated harness damage
    silently carried AttackPower 0 for as long as it stood. It claimed stock
    make_effect_context() was equivalent because the ASC's owner is the
    PlayerState, which implements IAbilitySystemInterface. The owner is the
    CONTROLLER: AGothicCharacterBase passes GetOwner() to InitializeGAS
    (GothicCharacterBase.cpp:100) for players and enemies alike. A Controller has
    no ASC and implements no IAbilitySystemInterface, so
    GetOriginalInstigatorAbilitySystemComponent() came back null and the
    AttackBonus term at GothicAttributeSet.cpp:222 was 0 no matter who was named.
    MakeDamageContext re-points the instigator at the AVATAR, which does resolve
    to an ASC — that is what makes an attacker's AttackPower real here.

    Uninstigated damage KEEPS the stock make_effect_context() path, unchanged and
    on purpose. It is not an oversight to fix later:

      * It is attributionless/environmental damage, and attributionless damage
        must contribute no AttackPower. Routing it through MakeDamageContext
        would name the VICTIM as its own instigator, which resolves to a real ASC
        and would add the victim's OWN AttackPower — phantom damage from a source
        that has no attack. Same semantics as the pillar collapse: a source with
        no ASC contributes nothing.
      * Every banked harness measurement was taken under it. Changing it would
        silently move numbers nobody re-measured — a bare `apply_damage 20` on a
        Defense-3, AttackPower-10 enemy would jump from 17 to 27.
      * NotifyDamagedBy still never fires with a real attacker
        (GothicAttributeSet.cpp:246-268 skips it for self-damage), so retaliation
        is only testable with an `instigator`.
    """
    target_asc = target.get_gothic_asc()
    if target_asc is None:
        raise LookupError("no GothicAbilitySystemComponent on %s" % target.get_name())

    source_asc = target_asc
    if instigator is not None:
        source_asc = instigator.get_gothic_asc()
        if source_asc is None:
            raise LookupError(
                "no GothicAbilitySystemComponent on instigator %s, so it cannot "
                "be named as the source of this damage. Drop the 'instigator' "
                "field to fall back to self-damage, but note that AttackPower "
                "and retaliation are then not exercised."
                % instigator.get_name())

    effect_class = unreal.load_class(None, effect_path)
    if effect_class is None:
        raise LookupError("could not load damage effect '%s'" % effect_path)

    # Binding names are NOT the C++ names here:
    #   UAbilitySystemBlueprintLibrary declares meta=(ScriptName="AbilitySystemLibrary"),
    #   so Python sees unreal.AbilitySystemLibrary.
    #   The spec apply lives on the ASC itself as BP_ApplyGameplayEffectSpecToTarget,
    #   exposed via ScriptName as apply_gameplay_effect_spec_to_target.
    #
    # MakeDamageContext is a static, so it is a classmethod in Python. The avatar
    # it takes is the PAWN that dealt the blow, never the ASC's owner.
    #
    # ONLY the instigated shape goes through it. Uninstigated damage keeps stock
    # make_effect_context() so it stays attributionless and contributes no
    # AttackPower -- see WHO DEALT IT above.
    if instigator is not None:
        context = unreal.GothicAbilitySystemComponent.make_damage_context(
            source_asc, instigator)
    else:
        context = source_asc.make_effect_context()
    spec = source_asc.make_outgoing_spec(effect_class, 1.0, context)
    spec = unreal.AbilitySystemLibrary.assign_tag_set_by_caller_magnitude(
        spec, _setbycaller_tag(effect_class), float(amount))
    source_asc.apply_gameplay_effect_spec_to_target(spec, target_asc)


@_action("apply_damage")
def _a_apply_damage(world, step):
    """Damage a target without needing to aim.

    Scripted marksmanship is unreliable against a moving target: control
    rotation set this tick does not reach the camera until the next frame, and
    GA_Fire traces from the camera. This drives the damage path directly so
    encounter, wave and death-chain tests do not depend on hitting anything.

    Pass an optional "instigator" (an actor label, resolved in the same world)
    to name an attacker. With it, the damage exercises AttackPower and fires
    NotifyDamagedBy, which is what makes retaliation testable from here.
    Without it the victim damages itself, AttackPower contributes nothing, and
    retaliation never triggers -- see _apply_damage for why.
    """
    target = _actor(world, step)
    amount = float(step.get("amount", 25.0))
    effect_path = step.get("effect", DEFAULT_DAMAGE_EFFECT)
    instigator = (_actor(world, step, "instigator")
                  if step.get("instigator") else None)

    before = common.try_read(lambda: float(target.get_health()))
    _apply_damage(target, amount, effect_path, instigator)
    after = common.try_read(lambda: float(target.get_health()))

    return {
        "target": target.get_name(),
        "instigator": instigator.get_name() if instigator is not None else None,
        "raw_amount": amount,
        "health_before": before,
        "health_after": after,
        "alive": common.try_read(lambda: bool(target.is_alive())),
        "note": ("`raw_amount` is the SetByCaller magnitude, NOT the health "
                 "delta, and evasion can drop the hit entirely. "
                 + ("INSTIGATED: health change is max(1, raw + instigator "
                    "AttackPower - target Defense). The context is built through "
                    "MakeDamageContext, so the instigator resolves to the "
                    "attacker's AVATAR ASC and its AttackPower really is added -- "
                    "expect health_before - health_after to EXCEED raw_amount "
                    "whenever AttackPower beats Defense. NotifyDamagedBy fires, "
                    "so retaliation is in play."
                    if instigator is not None else
                    "ATTRIBUTIONLESS: health change is max(1, raw - target "
                    "Defense). No instigator ASC resolves, so NO AttackPower is "
                    "added from either side -- deliberately, this is "
                    "environmental damage -- and NotifyDamagedBy never fires. "
                    "Unchanged from every previously banked measurement. Add an "
                    "'instigator' field to exercise AttackPower or retaliation.")),
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
    target = _actor(world, step, "target")
    controller = pawn.get_controller()
    if controller is None:
        raise LookupError("no controller on %s" % pawn.get_name())
    rot = unreal.MathLibrary.find_look_at_rotation(
        _aim_origin(pawn), target.get_actor_location())
    controller.set_control_rotation(rot)
    return {"aimed_at": target.get_name(), "rotation": [rot.pitch, rot.yaw, rot.roll]}


# --------------------------------------------------------------------------
# Weapon trace. Everything that answers "would this shot land?" runs through
# here, because the answer is only meaningful if it is the SAME trace GA_Fire
# runs: LineTraceSingleByChannel(Start=camera location, End=Start+forward*range,
# ECC_Weapon, simple collision, shooter ignored) -- GA_Fire.cpp:250-257.
# --------------------------------------------------------------------------

WEAPON_CHANNEL_NAME = "Weapon"        # DefaultEngine.ini:219, ECC_GameTraceChannel1
_FALLBACK_TRACE_RANGE = 10000.0


def _resolve_weapon_trace_type():
    """The ETraceTypeQuery that maps to ECC_Weapon, plus how we worked it out.

    UEngineTypes::ConvertToTraceType (EngineTypes.h:4075) is the obvious call and
    it is NOT a UFUNCTION -- UEngineTypes exposes four plain statics and nothing
    else, so Python cannot reach any of them.

    The previous implementation reconstructed the mapping off the
    UCollisionProfile CDO. That route is not merely broken on this build, it is
    unreachable on every build: UCollisionProfile has no BlueprintType and no
    script-exposed field, so PyGenUtil never generates it (the full derivation
    is in vigil_pie_common, above collision_channel_table). It also carried an
    _ASSUMED_WEAPON_TRACE_INDEX fallback, which meant a config change could have
    pointed live fire at the wrong channel while the tool still answered.

    Both are gone. common.trace_type_for_channel derives the ordinal from
    Config/DefaultEngine.ini -- the actual source of the globalconfig property
    the CDO would have loaded -- and raises rather than guessing.
    """
    return common.trace_type_for_channel(WEAPON_CHANNEL_NAME)


def _break_hit(hit):
    """FHitResult -> a small dict. Delegates to common.break_hit.

    This used to hard-code fields[0]/[3]/[5]/[9]/[10]/[12] against a "19 out
    params" reading of the header. The layout selection and its anchor check
    now live in vigil_pie_common so the driver, the LOS readout and any future
    caller cannot drift apart on it -- which they already had once.
    """
    return common.break_hit(hit)


def _weapon_trace(world, start, direction, trace_range, ignore_actors):
    """Run GA_Fire's trace along `direction`. Returns the broken hit, or None."""
    channel, _ = _resolve_weapon_trace_type()
    end = unreal.Vector(start.x + direction.x * trace_range,
                        start.y + direction.y * trace_range,
                        start.z + direction.z * trace_range)
    # Shape handling lives in common.line_trace, NOT here. KismetSystemLibrary
    # .h:1270 declares `bool LineTraceSingle(..., FHitResult& OutHit, ...)`,
    # which implies a (bool, FHitResult) tuple -- but this engine build's
    # binding returns a BARE FHitResult, and pinning to the header's implied
    # shape is what made every aim_at_vital call raise. See THE RETURN-SHAPE
    # TRAP in vigil_pie_common.
    did_hit, hit, _shape = common.line_trace(
        world, start, end, channel, ignore_actors=ignore_actors)

    if not did_hit:
        return None
    return _break_hit(hit)


def _unit(v):
    length = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
    if length <= 1e-4:
        raise ValueError("cannot normalise a zero-length direction")
    return unreal.Vector(v.x / length, v.y / length, v.z / length)


def _basis(forward):
    """Two unit vectors spanning the plane perpendicular to `forward`."""
    up = unreal.Vector(0.0, 0.0, 1.0)
    if abs(forward.z) > 0.95:
        up = unreal.Vector(1.0, 0.0, 0.0)
    right = _unit(unreal.Vector(
        forward.y * up.z - forward.z * up.y,
        forward.z * up.x - forward.x * up.z,
        forward.x * up.y - forward.y * up.x))
    true_up = unreal.Vector(
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x)
    return right, _unit(true_up)


def _dist(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


# Candidate aim points are the vital, then rings around it in the plane
# perpendicular to the line of sight, at these fractions of the EFFECTIVE hit
# radius (scaled by mesh world scale, same as the runtime threshold).
# Any mesh surface within the vital radius projects to within roughly that
# radius of the vital as seen from the shooter, so this covers the reachable
# set without a solver. Using the scaled radius keeps the ring spread
# proportional to the enemy -- on the 4x Lucid the unscaled rings all landed
# inside a single hand.
_VITAL_AIM_RINGS = (0.4, 0.75, 1.0, 1.35)
_VITAL_AIM_SPOKES = 8


def _weapon_trace_range(pawn):
    """The range GA_Fire will actually trace to, and where the number came from."""
    try:
        data = pawn.get_active_weapon_data()   # BlueprintPure, GothicPlayerCharacter.h:287
        if data is not None:
            value = float(data.get_editor_property("trace_range"))
            if value > 0.0:
                return value, "UGothicWeaponData.TraceRange on the active weapon"
    except Exception as exc:
        return _FALLBACK_TRACE_RANGE, (
            "fallback %.0fuu -- GetActiveWeaponData/TraceRange unreadable (%s: %s)"
            % (_FALLBACK_TRACE_RANGE, type(exc).__name__, exc))
    return _FALLBACK_TRACE_RANGE, (
        "fallback %.0fuu -- no active weapon data on this pawn"
        % _FALLBACK_TRACE_RANGE)


def _effective_hit_radius(vital):
    """The radius the RUNTIME adjudicates against, not the authored one.

    IsVitalPointHit thresholds on GetEffectiveHitRadius() -- HitDetectionRadius
    times the largest component of the owning mesh's world scale
    (GothicVitalPointComponent.cpp:240-242, 447-463). Reading the raw
    UPROPERTY instead reports 30.0 on every enemy in the game, which on the
    4.005x Lucid understates the real threshold of 120.2 by a factor of four
    and made this action refuse aims the game would have scored VITAL.

    Ask the component. Do not re-derive the scale here -- "largest component,
    not average" is a decision that lives in C++ and must only live there.
    """
    try:
        return float(vital.get_effective_hit_radius()), None
    except Exception as exc:
        pass

    # GetEffectiveHitRadius is BlueprintPure (GothicVitalPointComponent.h:178),
    # so this branch means the plugin is running against older C++ than the
    # scaled-radius change. The authored value is the honest fallback, but say
    # loudly that it is UNSCALED so nobody reads a refusal threshold as truth.
    try:
        raw = float(vital.get_editor_property("hit_detection_radius"))
        return raw, ("UNSCALED %.1f -- GetEffectiveHitRadius unreachable "
                     "(%s: %s); this is the authored HitDetectionRadius with no "
                     "mesh scale applied and will under-report on scaled enemies"
                     % (raw, type(exc).__name__, exc))
    except Exception as raw_exc:
        return 30.0, ("assumed UNSCALED 30.0 -- neither GetEffectiveHitRadius "
                      "(%s: %s) nor HitDetectionRadius (%s: %s) readable; "
                      "GothicVitalPointComponent.h:229 declares the default"
                      % (type(exc).__name__, exc,
                         type(raw_exc).__name__, raw_exc))


def _solve_vital_aim(world, pawn, target, vital):
    """Find an aim point whose ECC_Weapon impact registers as a vital hit.

    THE BUG THIS REPLACES
    ---------------------
    The old action aimed at GetCurrentVitalWorldLocation() and called it done.
    That world position is a bone transform plus a per-Blueprint LocalOffset
    (GothicVitalPointComponent.cpp:193-212) and sits 100-245uu off the capsule
    axis on this project's rigs -- so the ray toward it frequently passed the
    mesh entirely and GA_Fire took its silent early-out at GA_Fire.cpp:259-266.
    Seven measured attempts, zero hits, and every one of them reported success.

    A vital registers on the IMPACT POINT, not the aim point:
    UGothicVitalPointComponent::IsVitalPointHit compares Dist(ImpactPoint,
    CurrentVitalLocation) against GetEffectiveHitRadius() + BonusRadius
    (GothicVitalPointComponent.cpp:447-463), and GA_Fire feeds it Hit.ImpactPoint
    (GA_Fire.cpp:309). So the aim point has to be chosen such that the surface
    the ray lands on is inside the vital sphere.

    Candidates are evaluated with BonusRadius = 0 on purpose. The shooter's
    VitalPointRadius attribute only ever widens the sphere (GA_Fire.cpp:306-309),
    so a solution that holds at zero bonus holds for every loadout.

    Raises when no candidate works. Firing into space and reporting a rotation
    was the actual defect; a loud failure with the closest miss is the fix.
    """
    start = _aim_origin(pawn)
    vital_loc = vital.get_current_vital_world_location()
    trace_range, range_note = _weapon_trace_range(pawn)
    radius, radius_note = _effective_hit_radius(vital)

    base_dir = _unit(unreal.Vector(vital_loc.x - start.x,
                                   vital_loc.y - start.y,
                                   vital_loc.z - start.z))
    right, up = _basis(base_dir)

    candidates = [("vital centre", vital_loc)]
    for ring in _VITAL_AIM_RINGS:
        offset = ring * radius
        for spoke in range(_VITAL_AIM_SPOKES):
            angle = 2.0 * math.pi * spoke / _VITAL_AIM_SPOKES
            dx = math.cos(angle) * offset
            dy = math.sin(angle) * offset
            candidates.append((
                "ring %.2fR spoke %d" % (ring, spoke),
                unreal.Vector(vital_loc.x + right.x * dx + up.x * dy,
                              vital_loc.y + right.y * dx + up.y * dy,
                              vital_loc.z + right.z * dx + up.z * dy)))

    best = None
    wrong_actor = None
    for tried, (label, point) in enumerate(candidates, start=1):
        direction = _unit(unreal.Vector(point.x - start.x,
                                        point.y - start.y,
                                        point.z - start.z))
        hit = _weapon_trace(world, start, direction, trace_range, [pawn])
        if hit is None:
            continue
        if hit["actor"] is None or hit["actor"].get_name() != target.get_name():
            if wrong_actor is None:
                wrong_actor = hit["actor"].get_name() if hit["actor"] else "<none>"
            continue

        gap = _dist(hit["impact_point"], vital_loc)
        if best is None or gap < best["gap"]:
            best = {"label": label, "point": point, "direction": direction,
                    "impact": hit["impact_point"], "gap": gap, "bone": hit["bone"],
                    "distance": hit["distance"]}

        # IsVitalPointHit is the authoritative test -- ask the component rather
        # than re-deriving its arithmetic here, so this cannot drift from C++.
        if bool(vital.is_vital_point_hit(hit["impact_point"], 0.0)):
            return {
                "solved": True,
                "aim_point": point,
                "direction": direction,
                "impact_point": hit["impact_point"],
                "impact_gap": gap,
                "impact_bone": hit["bone"],
                "candidate": label,
                "candidates_tried": tried,
                "vital_location": vital_loc,
                "effective_hit_radius": radius,
                "trace_range": trace_range,
                "range_note": range_note,
                "radius_note": radius_note,
            }

    detail = {
        "vital_location": common.vec(vital_loc),
        "vital_index": int(vital.get_active_vital_index()),
        "shooter_camera": common.vec(start),
        "distance_to_vital": round(_dist(start, vital_loc), 1),
        "effective_hit_radius": radius,
        "candidates_tried": len(candidates),
        "trace_channel": _resolve_weapon_trace_type()[1],
        "trace_range_source": range_note,
    }
    if best is not None:
        detail["closest_impact"] = common.vec(best["impact"])
        detail["closest_impact_gap"] = round(best["gap"], 1)
        detail["closest_impact_bone"] = best["bone"]
    if wrong_actor is not None:
        detail["blocked_by"] = wrong_actor
    if radius_note is not None:
        detail["radius_note"] = radius_note

    raise RuntimeError(
        "No aim point on %s puts an ECC_Weapon impact within %.1fcm of its "
        "vital (the effective hit radius the runtime adjudicates against: "
        "GetEffectiveHitRadius, authored radius x mesh world scale). %s. "
        "Refusing to aim -- the shot would miss and GA_Fire is "
        "silent on a miss (GA_Fire.cpp:259-266). Detail: %s"
        % (target.get_name(), radius,
           ("Closest reachable surface was %.1fcm away on bone '%s'"
            % (best["gap"], best["bone"])) if best is not None
           else "No candidate ray hit the target at all",
           detail))


def _camera_shot_now(world, pawn, target, vital, trace_range):
    """What firing THIS TICK would actually do, along the camera's real forward.

    Control rotation set this tick does not reach the camera until the next
    frame, and GA_Fire traces from the camera (GA_Fire.cpp:221, 250). So an
    aim step and a fire step scheduled at the same `at` fire the OLD rotation.
    Reporting the camera's current answer makes that visible instead of leaving
    it as folklore: aim, wait a tick, aim again, and this field tells you
    whether the shot is lined up before you spend it.
    """
    camera = common.component(pawn, "CameraComponent")
    if camera is None:
        return {"error": "no CameraComponent on %s -- GA_Fire would early-out "
                         "at GA_Fire.cpp:224" % pawn.get_name()}

    hit = _weapon_trace(world, camera.get_world_location(),
                        camera.get_forward_vector(), trace_range, [pawn])
    if hit is None:
        return {"would_hit": None, "would_be_vital": False,
                "note": "camera forward hits nothing -- firing now is a miss"}

    name = hit["actor"].get_name() if hit["actor"] else None
    on_target = name == target.get_name()
    return {
        "would_hit": name,
        "would_be_vital": bool(
            on_target and vital.is_vital_point_hit(hit["impact_point"], 0.0)),
        "impact_point": common.vec(hit["impact_point"]),
        "impact_gap_to_vital": round(
            _dist(hit["impact_point"], vital.get_current_vital_world_location()), 1),
    }


@_action("aim_at_vital")
def _a_aim_at_vital(world, step):
    """Aim so the SHOT lands on the vital, and refuse if it cannot.

    Aiming at the vital's world position is wrong: the vital is a point in
    space, the hit is a point on the mesh, and IsVitalPointHit only ever sees
    the second one. This solves for an aim point whose ECC_Weapon impact falls
    inside the vital sphere, verifies it against the component's own
    IsVitalPointHit, and raises when no such point exists.

    READING THE DAMAGE THIS PRODUCES
    --------------------------------
    Do NOT sanity-check VitalDamageMultiplier by dividing a vital number by a
    body number. The flat terms sit OUTSIDE the multiplier -- the execution is
    (Raw * VitalMult) + AttackPower - Defense -- so at the session's measured
    raw of 24.33 against a Thrall (Defense 3, AttackPower 15) a body shot lands
    36.34 and a geometric vital lands 72.83. That ratio is ~2.06, not the 2.5
    the multiplier is set to, and 2.06 is CORRECT. The multiplier was
    independently confirmed at 2.5 (70.84 vital vs 34.34 body on the boss,
    reconciling to the same formula).
    """
    # Rebound once, up front: the traces below have to run in the same world
    # the pawns were resolved in, or they trace an empty copy of the level.
    world = _step_world(world, step)
    pawn = _actor(world, step)
    target = _actor(world, step, "target")
    vital = common.component(target, "GothicVitalPointComponent")
    if vital is None:
        raise LookupError("%s has no GothicVitalPointComponent" % target.get_name())

    controller = pawn.get_controller()
    if controller is None:
        raise LookupError("no controller on %s" % pawn.get_name())

    solution = _solve_vital_aim(world, pawn, target, vital)

    rot = unreal.MathLibrary.find_look_at_rotation(
        _aim_origin(pawn), solution["aim_point"])
    controller.set_control_rotation(rot)

    return {
        "aimed_at_vital_of": target.get_name(),
        "vital_index": int(vital.get_active_vital_index()),
        "vital_location": common.vec(solution["vital_location"]),
        "aim_point": common.vec(solution["aim_point"]),
        "predicted_impact": common.vec(solution["impact_point"]),
        "predicted_impact_gap": round(solution["impact_gap"], 1),
        "predicted_impact_bone": solution["impact_bone"],
        "effective_hit_radius": solution["effective_hit_radius"],
        "solved_by": solution["candidate"],
        "rotation": [round(rot.pitch, 2), round(rot.yaw, 2), round(rot.roll, 2)],
        "trace_channel": _resolve_weapon_trace_type()[1],
        "trace_range": solution["trace_range"],
        "camera_shot_now": _camera_shot_now(
            world, pawn, target, vital, solution["trace_range"]),
        "note": "Control rotation reaches the camera NEXT frame; schedule the "
                "fire step at least ~0.1s after this one. camera_shot_now "
                "reports what firing on THIS tick would have done.",
        "damage_reading_note": "Do not divide vital damage by body damage to "
                              "check VitalDamageMultiplier. The flat terms are "
                              "outside it: (Raw * VitalMult) + AttackPower - "
                              "Defense. At raw 24.33 vs a Thrall (Def 3, AP 15) "
                              "body = 36.34 and vital = 72.83, a ratio of ~2.06 "
                              "even though the multiplier is 2.5.",
    }


@_action("set_combat_target")
def _a_set_target(world, step):
    """Force an enemy onto a target, skipping perception acquisition."""
    enemy = _actor(world, step)
    target = _actor(world, step, "target")
    enemy.set_combat_target(target)
    return {"enemy": enemy.get_name(), "target": target.get_name()}


@_action("freeze_vital")
def _a_freeze_vital(world, step):
    """Lock a vital point to a known INDEX. It still tracks the animating bone.

    Freezing does not pin a world position, and the harness note claiming it
    stranded the aim point in open air is wrong -- FreezeVitalPoint only sets
    bIsFrozen so ShiftVitalPoint becomes a no-op
    (GothicVitalPointComponent.cpp:229 region, .h:302-303).
    GetCurrentVitalWorldLocation still calls ComputeWorldLocation every time,
    which reads the live bone transform (GothicVitalPointComponent.cpp:193-217).
    A frozen vital on a walking pawn moves with the pawn. Verified against the
    C++, not assumed.

    What index -1 really costs is REPEATABILITY, not accuracy: it freezes
    whatever the shift timer happened to land on, so two runs of the same
    scenario freeze different limbs and the damage numbers are not comparable.
    Pass an explicit index when a measurement has to be reproducible; this
    reports which one you got either way.

    Freezing is one-way -- nothing ever clears bIsFrozen, so the pawn is
    permanently frozen for the rest of the PIE session.
    """
    target = _actor(world, step)
    vital = common.component(target, "GothicVitalPointComponent")
    if vital is None:
        raise LookupError("%s has no GothicVitalPointComponent" % target.get_name())

    requested = int(step.get("index", -1))
    try:
        defined = len(vital.get_editor_property("vital_point_locations") or [])
    except Exception:
        defined = None

    if defined is not None and requested >= 0 and requested >= defined:
        raise ValueError(
            "vital index %d is out of range on %s -- VitalPointLocations "
            "defines %d entr%s (0..%d). FreezeVitalPoint would silently keep "
            "the current index (GothicVitalPointComponent.h:136-137)."
            % (requested, target.get_name(), defined,
               "y" if defined == 1 else "ies", defined - 1))

    vital.freeze_vital_point(requested)
    resolved = int(vital.get_active_vital_index())

    result = {
        "target": target.get_name(),
        "requested_index": requested,
        "frozen_at": resolved,
        "vital_location": common.vec(vital.get_current_vital_world_location()),
        "vital_points_defined": defined,
        "note": "The index is frozen; the world location still follows the bone "
                "every frame. Freezing is irreversible for this PIE session.",
    }
    if requested < 0:
        result["repeatability_warning"] = (
            "index -1 froze whatever the shift had reached (index %d). Two runs "
            "will not freeze the same limb -- pass an explicit index to make a "
            "vital-damage measurement reproducible." % resolved)
    return result


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

        # The spec is checked without touching PIE, so a typo'd world name
        # ("clientt") is caught by scenario_validate rather than by one failed
        # step twenty seconds into a run. Whether that instance EXISTS is only
        # answerable at execution time, and resolve_world reports it there.
        if step.get("world") is not None:
            try:
                common.world_spec_index(step["world"])
            except ValueError as exc:
                problems.append("step %d: %s" % (i, exc))
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

def spawn(world, class_path, location, rotation=(0.0, 0.0, 0.0),
          behavior_tree=None, no_collision_fail=True):
    """Spawn a PAWN into the PIE world and track it for cleanup.

    class_path for a Blueprint needs the _C suffix, e.g.
    "/Game/Blueprints/Enemies/BP_Enemy_FeralRetained.BP_Enemy_FeralRetained_C"

    WHY THIS IS NOT THE DEFERRED-SPAWN ROUTE ANY MORE
    -------------------------------------------------
    The old body called GameplayStatics.begin_deferred_actor_spawn_from_class
    and failed with "type object 'GameplayStatics' has no attribute ...". That
    is not a version wobble, it is permanent: both
    UGameplayStatics::BeginDeferredActorSpawnFromClass (GameplayStatics.h:65-67)
    and UGameplayStatics::FinishSpawningActor (GameplayStatics.h:69-71) are
    tagged meta=(BlueprintInternalUseOnly="true"), and the Python bindings skip
    any function carrying that key (PyGenUtil.cpp:1621, in
    IsBlueprintExposedFunction). Those two names will never exist in Python, so
    there is nothing to retry.

    THE ROUTE ACTUALLY USED
    -----------------------
    UAIBlueprintHelperLibrary::SpawnAIFromClass
    (AIBlueprintHelperLibrary.h:44-45) -- a plain BlueprintCallable UFUNCTION
    with a WorldContext, so passing the PIE world spawns into the PIE world, not
    the editor world. Its body (AIBlueprintHelperLibrary.cpp:207-240) does
    World->SpawnActor<APawn>, then SpawnDefaultController (which possesses), and
    runs a BehaviorTree if one is supplied.

    Two behaviour differences from the old code, both deliberate and visible:
      * Pawns only. TSubclassOf<APawn> is enforced here with a named error
        rather than being discovered as a null return. There is no reflected
        route to spawn an arbitrary AActor into a PIE world from editor Python;
        EditorActorSubsystem.spawn_actor_from_class targets the EDITOR world and
        would silently put the actor in the wrong world.
      * Collision handling is AlwaysSpawn (no_collision_fail=True) or
        AdjustIfPossibleButDontSpawnIfColliding (False) -- those are the only
        two the helper exposes (AIBlueprintHelperLibrary.cpp:216). The old
        AdjustIfPossibleButAlwaysSpawn is not reachable this way; AlwaysSpawn is
        the closer match and is the default, so a spawn point inside geometry
        interpenetrates instead of failing.

    THE NAME, WHICH IS NOT THE C++ NAME
    -----------------------------------
    The first cut of this call said `unreal.AIBlueprintHelperLibrary` and died
    with "module 'unreal' has no attribute 'AIBlueprintHelperLibrary'". That is
    the ScriptName trap, not a missing API: the UCLASS carries
    meta=(ScriptName="AIHelperLibrary") (AIBlueprintHelperLibrary.h:25), so the
    Python binding is `unreal.AIHelperLibrary`. Resolved through
    common.ai_helper_library() so the spelling lives in exactly one place and a
    future rename raises a diagnosable error instead of an AttributeError.
    """
    cls = unreal.load_class(None, class_path)
    if cls is None:
        raise LookupError("Could not load class '%s' (Blueprints need _C)" % class_path)

    if not _is_pawn_class(cls):
        raise TypeError(
            "'%s' is not a Pawn subclass. SpawnAIFromClass takes "
            "TSubclassOf<APawn>, and there is no reflected way to spawn a "
            "non-pawn actor into the PIE world from editor Python. Place the "
            "actor in the level instead." % class_path)

    helper = common.ai_helper_library()
    spawn_fn = getattr(helper, "spawn_ai_from_class", None)
    if spawn_fn is None:
        raise LookupError(
            "%s resolved but has no spawn_ai_from_class. SpawnAIFromClass is a "
            "plain BlueprintCallable UFUNCTION (AIBlueprintHelperLibrary.h:44-45) "
            "so it should be bound; check for a signature change before looking "
            "for another spawn route." % helper.__name__)

    pawn = spawn_fn(
        world,
        cls,
        behavior_tree,
        unreal.Vector(*location),
        unreal.Rotator(*rotation),
        bool(no_collision_fail))
    if pawn is None:
        raise RuntimeError(
            "SpawnAIFromClass returned None for '%s' at %s. With "
            "no_collision_fail=%s the spawn %s. Check the class loaded and the "
            "location is inside the streamed level."
            % (class_path, list(location), bool(no_collision_fail),
               "should not fail on collision, so suspect the class"
               if no_collision_fail else "was probably refused for colliding"))

    _spawned.append(pawn)
    common.audit("spawn\t%s\t%s" % (class_path, pawn.get_name()))
    return pawn


def _is_pawn_class(cls):
    """True if cls is APawn or a subclass. Blueprint classes included."""
    try:
        return bool(unreal.MathLibrary.class_is_child_of(cls, unreal.Pawn))
    except Exception as exc:
        raise RuntimeError(
            "Could not test '%s' against APawn: %s: %s"
            % (cls, type(exc).__name__, exc))


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
