"""
vigil_pie_probe.py

Buffered state recorder for PIE debugging.

WHY THIS EXISTS
---------------
Unreal MCP dispatches every tool call on the GAME THREAD, serially. A tool call
cannot wait, sleep, or poll for a duration -- doing so would stall PIE for
exactly as long as it waited, corrupting the very timing being observed.

So observation-over-time is split in two:
  1. A Slate post-tick callback samples state into an in-memory ring buffer.
  2. A separate, instantaneous tool call flushes that buffer to JSON on disk.

Samples carry BOTH wall time and game time. Game time is the one to correlate
scenario events against -- see vigil_pie_common.game_time for why.
"""

import time

import unreal

import vigil_pie_common as common

# --------------------------------------------------------------------------
# Module state. Deliberately module-level: the Python interpreter persists
# across PIE sessions, so a probe can outlive the world it was sampling.
# That is exactly why _validate_targets() exists.
# --------------------------------------------------------------------------
_tick_handle = None
_samples = []
_targets = []          # list of (label, unreal.Actor)
_metrics = []
_interval = 0.1
_accumulator = 0.0
_max_samples = 20000   # ~33 min at 10Hz; hard cap so a forgotten probe can't OOM
_started_at = None     # wall clock origin
_game_origin = None    # game time origin, for correlating with scenarios
_world = None
_stop_reason = None
_error_streak = 0
_MAX_ERROR_STREAK = 5

# Metric key -> reader. Adding a metric means adding one function here.
_METRIC_READERS = {}


def _reader(name):
    def deco(fn):
        _METRIC_READERS[name] = fn
        return fn
    return deco


@_reader("health")
def _m_health(actor):
    return round(float(actor.get_health()), 2)


@_reader("max_health")
def _m_max_health(actor):
    return round(float(actor.get_max_health()), 2)


@_reader("stamina")
def _m_stamina(actor):
    return round(float(actor.get_stamina()), 2)


@_reader("ether")
def _m_ether(actor):
    return round(float(actor.get_ether()), 2)


@_reader("alive")
def _m_alive(actor):
    return bool(actor.is_alive())


@_reader("steadfast")
def _m_steadfast(actor):
    comp = common.component(actor, "GothicSteadfastComponent")
    if comp is None:
        raise LookupError("no GothicSteadfastComponent")
    return round(float(comp.get_current_steadfast()), 3)


@_reader("steadfast_max")
def _m_steadfast_max(actor):
    comp = common.component(actor, "GothicSteadfastComponent")
    if comp is None:
        raise LookupError("no GothicSteadfastComponent")
    return round(float(comp.get_max_steadfast()), 3)


@_reader("converting_steadfast")
def _m_converting(actor):
    # Directly relevant to the hold-to-convert trigger bug: shows whether the
    # conversion latch is set, independent of whether the bar renders.
    return bool(actor.is_converting_steadfast())


@_reader("vital_index")
def _m_vital_index(actor):
    comp = common.component(actor, "GothicVitalPointComponent")
    if comp is None:
        raise LookupError("no GothicVitalPointComponent")
    return int(comp.get_active_vital_index())


@_reader("vital_location")
def _m_vital_location(actor):
    comp = common.component(actor, "GothicVitalPointComponent")
    if comp is None:
        raise LookupError("no GothicVitalPointComponent")
    return common.vec(comp.get_current_vital_world_location())


@_reader("in_combat")
def _m_in_combat(actor):
    """True while this pawn is fighting.

    Two sources, because there are genuinely two -- and the harness only ever
    read the one the Accursed do not have:

    1. UGothicCombatStateComponent::IsInCombat (GothicCombatStateComponent.h:44).
       Real, but the component is never created in C++ (no CreateDefaultSubobject
       anywhere in GothicEnemyBase.cpp) and only BP_GothicPlayerCharacter adds it
       in Blueprint. On every enemy this column was an error object, which is why
       the recorded fights came back with an empty in_combat.
    2. The AI blackboard's bIsInCombat key, written by
       AGothicEnemyAIController::SetBlackboardTarget (GothicEnemyAIController.cpp:98)
       and cleared by ClearCombatTarget (:124). This is the enemy-side truth and
       the same value every BT decorator gates on.

    Note the State.InCombat gameplay tag is NOT a third source for enemies: it is
    applied only by the component above, so an enemy ASC never carries it.
    """
    comp = common.component(actor, "GothicCombatStateComponent")
    if comp is not None:
        return bool(comp.is_in_combat())

    bb = common.blackboard(actor)
    if bb is not None:
        return bool(bb.get_value_as_bool("bIsInCombat"))

    raise LookupError(
        "no GothicCombatStateComponent (only BP_GothicPlayerCharacter has one) "
        "and no blackboard exposing bIsInCombat -- nothing on this pawn knows "
        "whether it is in combat")


@_reader("pack_id")
def _m_pack_id(actor):
    # NAME_None here is the diagnostic for the PackSubsystem registration gap.
    return str(actor.get_pack_id())


@_reader("combat_target")
def _m_combat_target(actor):
    """Who this pawn is currently fighting.

    Reads the blackboard TargetActor key FIRST, not the pawn.

    AGothicEnemyBase::CombatTarget (GothicEnemyBase.h:256) is set by
    SetCombatTarget (GothicEnemyBase.cpp:294) and then NEVER CLEARED:
    AGothicEnemyAIController::ClearCombatTarget (GothicEnemyAIController.cpp:116-124)
    clears the blackboard key and the focus, and leaves the pawn's pointer latched
    at the last target forever. Sampling get_combat_target() therefore produced a
    column that went high once and stayed high for the rest of the recording --
    it could never show a disengage, which is exactly the transition the boss-fight
    loop is trying to observe.

    The blackboard TargetActor key (set at GothicEnemyAIController.cpp:96, cleared
    at :122) is the value every BT task actually resolves, and it does go back to
    None. GothicBTService_CombatSync reconciles the two each tick, so the pawn-side
    latch remains a useful fallback when no blackboard exists yet.
    """
    bb = common.blackboard(actor)
    if bb is not None:
        target = bb.get_value_as_object("TargetActor")
        return target.get_name() if common.is_valid(target) else None

    getter = getattr(actor, "get_combat_target", None)
    if getter is None:
        raise LookupError(
            "no blackboard and no GetCombatTarget -- this pawn has no combat "
            "target concept (AGothicEnemyBase only)")
    target = getter()
    return target.get_name() if common.is_valid(target) else None


# Actor name -> True once ChosenAction has been confirmed present on that
# pawn's blackboard asset. Checked once, not every sample: _key_entries walks
# the asset's parent chain and this reader runs at 10Hz per target.
_chosen_action_verified = {}

CHOSEN_ACTION_KEY = "ChosenAction"


def _verify_chosen_action_key(actor, bb):
    """Confirm the blackboard ASSET declares ChosenAction. Raises if it does not.

    UBlackboardComponent::GetValueAsName (BlackboardComponent.h:123-124) returns
    NAME_None for a key that does not exist, which is indistinguishable from a
    key the service has legitimately cleared -- and clearing it is exactly what
    UGothicBTService_WeightedActionSelect does when nothing is eligible
    (GothicBTService_WeightedActionSelect.cpp:331, .h:181). A silent None column
    would therefore read as "the BT chose nothing" when the truth might be "this
    pawn has no such key". Verify once, then trust the reads.
    """
    name = actor.get_name()
    if _chosen_action_verified.get(name):
        return

    from vig_blackboard_tools import _key_entries
    entries = _key_entries(bb, actor.get_controller())
    if not entries:
        raise LookupError(
            "could not enumerate %s's blackboard asset, so a NAME_None from "
            "ChosenAction cannot be told apart from a missing key" % name)
    declared = {key for key, _ in entries}
    if CHOSEN_ACTION_KEY not in declared:
        raise LookupError(
            "%s's blackboard declares no '%s' key. Present: %s"
            % (name, CHOSEN_ACTION_KEY, sorted(declared)))
    _chosen_action_verified[name] = True


@_reader("chosen_action")
def _m_chosen_action(actor):
    """Which weighted action the BT has committed to this tick.

    Same live source as combat_target: the blackboard, not the pawn.
    UGothicBTService_WeightedActionSelect writes the winning entry's ActionID
    into the ChosenAction Name key and clears it when nothing is eligible
    (GothicBTService_WeightedActionSelect.cpp:352, :372, and the clear at :331),
    and every equality decorator in the tree gates on that key. Sampling it here
    turns "what is the boss doing and for how long" into a probe column instead
    of one MCP call per sample.

    Returns None when the key is genuinely empty -- that is the service reporting
    nothing eligible, which is a real BT state and a finding in its own right.
    """
    bb = common.blackboard(actor)
    if bb is None:
        raise LookupError(
            "no blackboard on %s -- ChosenAction is a BT key and only exists on "
            "an AI-possessed pawn" % actor.get_name())

    _verify_chosen_action_key(actor, bb)

    value = str(bb.get_value_as_name(CHOSEN_ACTION_KEY))
    return None if value in ("None", "") else value


# --------------------------------------------------------------------------
# Gameplay tags and active effects
#
# WHY THESE EXIST
# ---------------
# Every link in the boss's stun chain -- the 600uu overlap, the
# ApplyEffectToASC call, the GE's InheritableOwnedTagsContainer, the player's
# tag-change callback that halts movement -- has been verified by reading
# source. Whether State.Stunned ever actually LANDS was unanswerable across
# three PIE runs, because nothing in this harness could see a tag at runtime:
# probe metrics carried no tag column, describe_combatant returns vitals and
# cooldowns only, and AbilitySystemInspectorToolset wants an asset refPath,
# which no PIE-world actor has. A single game-thread snapshot also cannot be
# timed against a Roar release; a 10Hz column can.
#
# THE API HUNT, AND THE THREE ROUTES THAT ARE CLOSED
# --------------------------------------------------
# Non-UFUNCTION methods are absent from Python entirely, so "the ASC has
# GetOwnedGameplayTags" is not the same as "Python can call it". It cannot:
#
#   1. UAbilitySystemComponent::GetOwnedGameplayTags
#      (AbilitySystemComponent.h:596 and :602, UE 5.8) -- BOTH overloads are
#      plain inline methods with no UFUNCTION at all. Invisible to Python.
#   2. IGameplayTagAssetInterface::GetOwnedGameplayTags
#      (GameplayTagAssetInterface.h) -- also no UFUNCTION; it is a pure virtual
#      with an out-param, which UHT could not expose anyway.
#   3. IGameplayTagAssetInterface::BP_GetOwnedGameplayTags -- this one IS a
#      UFUNCTION(BlueprintCallable) returning an FGameplayTagContainer by
#      value, and it looks like the answer. It is not: it is declared
#      meta=(BlueprintInternalUseOnly), and PyGenUtil::IsScriptExposedFunction
#      (PyGenUtil.cpp:1621) rejects any function carrying that key. It is
#      exported to Blueprint and to nothing else.
#
# What IS reachable is the static the engine's own comment points at -- "In
# Blueprints, new nodes will use BlueprintGameplayTagLibrary's version":
#
#      UBlueprintGameplayTagLibrary::GetOwnedGameplayTags(
#          TScriptInterface<IGameplayTagAssetInterface>)
#      (BlueprintGameplayTagLibrary.h:284-285) -- BlueprintPure, static,
#      returns the container by value.
#
# Two traps on the way to calling it, both already catalogued in
# vigil_pie_common:
#   * The class is UCLASS(meta=(ScriptName="GameplayTagLibrary")), so
#     unreal.BlueprintGameplayTagLibrary does not exist. Resolved through
#     common.resolve_class so a rename reports itself instead of reading as a
#     missing engine feature.
#   * The parameter is a TScriptInterface. Passing the ASC object straight in
#     works -- PyConversion handles FInterfaceProperty (PyConversion.cpp:896)
#     -- because UAbilitySystemComponent implements IGameplayTagAssetInterface.
#
# WHAT THE TAGS ACTUALLY ARE
# --------------------------
# ASC::GetOwnedGameplayTags returns GameplayTagCountContainer's EXPLICIT tags
# (AbilitySystemComponent.h:604 -> GameplayEffectTypes.h:1339). Explicit means
# exactly what was added: a GE granting State.Stunned puts "State.Stunned" in
# this list and NOT its parent "State". So match on the full tag string, and do
# not expect a parent to show up as its own row -- HasMatchingGameplayTag
# expands parents, this list does not.
#
# COST
# ----
# One container copy plus one wrapped FGameplayTag per tag per sample. Actors
# here carry a handful of tags, so this is tens of small allocations per second
# at 10Hz -- cheap enough for the 0.05s floor. It is still the most expensive
# reader in this file; do not add it to a 0.016s single-frame recording of a
# dozen actors without checking the frame cost first.
# --------------------------------------------------------------------------

def _tag_library():
    return common.resolve_class(
        ["GameplayTagLibrary", "BlueprintGameplayTagLibrary"],
        "reading an actor's owned gameplay tags",
        ("GameplayTag",))


def _ability_system_library():
    return common.resolve_class(
        ["AbilitySystemLibrary", "AbilitySystemBlueprintLibrary"],
        "resolving an actor's AbilitySystemComponent",
        ("AbilitySystem",))


def _asc(actor):
    """This actor's ASC, or None. Never raises.

    AGothicCharacterBase implements IAbilitySystemInterface
    (GothicCharacterBase.h:52, :64), so the library call resolves for both the
    player and every Accursed. The PlayerState hop stays as a fallback because
    AGothicPlayerState implements the same interface (GothicPlayerState.h:22,
    :30) and a pawn mid-respawn can be possessed before its own ASC is wired.

    Returns None rather than raising on a dead actor: this reader is sampled
    across the player's death and respawn, and the pawn is genuinely gone for
    several frames while the buffer keeps recording.
    """
    if not common.is_valid(actor):
        return None

    library = _ability_system_library()

    asc = common.try_read(lambda: library.get_ability_system_component(actor))
    if common.is_valid(asc):
        return asc

    # NOT getattr(actor, "get_player_state"): that method is non-UFUNCTION and
    # absent from every pawn, so this fallback never once ran. See
    # vigil_pie_common.player_state.
    state = common.player_state(actor)
    if common.is_valid(state):
        asc = common.try_read(
            lambda: library.get_ability_system_component(state))
        if common.is_valid(asc):
            return asc

    return None


@_reader("tags")
def _m_tags(actor):
    """Every gameplay tag this actor's ASC currently owns, sorted.

    THIS READER RETURNS [] INSTEAD OF RAISING, UNLIKE ITS NEIGHBOURS
    ---------------------------------------------------------------
    steadfast and vital_index raise LookupError when their component is
    missing, and that is right for them: a pawn with no vital point component
    can never produce a vital_index, so an error cell is the true answer.

    A tag column is different. It is read to answer "did State.Stunned appear
    at t=4.2 and expire at t=6.2", and the actor under observation dies and
    respawns under a NEW pawn name during exactly the runs this exists for.
    An empty list is a truthful reading of "this actor owns no tags right now",
    and it keeps the column plottable across the gap instead of turning a
    stretch of it into error objects that no timeline can read through.

    The cost of that choice: [] does not distinguish "no ASC" from "an ASC with
    nothing on it". Accept it knowingly -- every actor this probe targets is an
    AGothicCharacterBase and therefore always has an ASC, so in practice [] on
    a live actor means the tag genuinely is not applied. If a run ever needs
    that distinction, sample active_effects alongside: an ASC-less actor
    returns [] there too, but describe_combatant will say so outright.
    """
    asc = _asc(actor)
    if asc is None:
        return []

    library = _tag_library()
    container = library.get_owned_gameplay_tags(asc)
    if container is None:
        return []

    # BreakGameplayTagContainer (BlueprintGameplayTagLibrary.h:199-200) is the
    # only exposed way to enumerate a container; FGameplayTagContainer's own
    # GameplayTags array is a bare UPROPERTY with no Blueprint visibility, so
    # get_editor_property does not reach it. GetTagName (:56-57) turns each
    # FGameplayTag into its FName -- str() on the struct itself would give a
    # wrapper repr, not the tag.
    #
    # Note both calls are left UNGUARDED, deliberately: if a future engine
    # build renames or drops them, that must surface as an error cell in the
    # column, not as a silent [] that reads exactly like "the stun never
    # landed" -- the one wrong conclusion this metric exists to prevent.
    tags = library.break_gameplay_tag_container(container) or []
    return sorted(str(library.get_tag_name(tag)) for tag in tags)


@_reader("active_effects")
def _m_active_effects(actor):
    """Which GameplayEffects are currently active on this actor's ASC.

    Rides the same _asc() lookup as tags, which is why it is here at all --
    it costs one extra ASC call and nothing else structurally.

    WHY IT IS WORTH SAMPLING NEXT TO tags
    -------------------------------------
    tags answers "is State.Stunned present". This answers "is the GE that
    grants it still applying". They come apart in both directions: an effect
    whose duration has ended still shows for the frame before removal, and a
    tag added by anything other than a GE (an ability's ActivationOwnedTags,
    say) shows in tags with no effect behind it. Seeing which of the two moved
    first is what tells a stun that never landed apart from a stun that landed
    and was cleared early.

    THE EMPTY-CONTAINER QUERY
    -------------------------
    UAbilitySystemComponent::GetActiveEffectsWithAllTags
    (AbilitySystemComponent.h:819-820) is BlueprintCallable and is the only
    exposed enumerator -- GetActiveEffects takes an FGameplayEffectQuery, which
    has to be built, and there is no exposed "give me all of them". Passing an
    EMPTY container is the way to ask for all: it builds a match-all-owning-
    tags query over zero tags, and an all-match over an empty set is vacuously
    true for every effect.

    IDENTITY COMES FROM THE DEBUG STRING, BECAUSE NOTHING ELSE IS EXPOSED
    --------------------------------------------------------------------
    A FActiveGameplayEffectHandle has no Blueprint-exposed route back to its
    UGameplayEffect class. Everything on UAbilitySystemBlueprintLibrary that
    takes a handle returns a number (stack count, start time, expected end
    time) except GetActiveGameplayEffectDebugString
    (AbilitySystemBlueprintLibrary.h:564-565), which is the engine's own
    display string for the effect. Treat these as labels for correlating
    against a known GE, not as a stable API -- the format is a debug format and
    may change between engine versions.

    Returns [] for the same reasons _m_tags does; see that docstring.
    """
    asc = _asc(actor)
    if asc is None:
        return []

    library = _ability_system_library()
    handles = common.try_read(
        lambda: asc.get_active_effects_with_all_tags(
            unreal.GameplayTagContainer()))
    # common.try_read hands back an ERROR DICT on failure, not None, and a dict
    # is iterable -- iterating it would walk the string key "error" and produce
    # a row of nonsense. Test for the dict explicitly.
    if handles is None or isinstance(handles, dict):
        return []

    labels = []
    for handle in handles:
        label = common.try_read(
            lambda h=handle: str(
                library.get_active_gameplay_effect_debug_string(h)))
        labels.append(label if isinstance(label, str) else "<unreadable handle>")
    return sorted(labels)


@_reader("location")
def _m_location(actor):
    return common.vec(actor.get_actor_location())


@_reader("speed")
def _m_speed(actor):
    v = actor.get_velocity()
    return round((v.x ** 2 + v.y ** 2 + v.z ** 2) ** 0.5, 1)


# --------------------------------------------------------------------------
# Loadout power. Record these alongside ANY damage measurement.
#
# The starting kit is rolled with unseeded RNG on every spawn, so raw shot
# damage has been observed between 22.0 and ~31 across sessions with nothing
# else changed. Both multipliers in UGA_Fire::PerformFireTrace
# (GA_Fire.cpp:289-298) come from that roll:
#
#     FinalDamage = EffectiveDamage
#                 * (AggregateGearPower / BaselineGearPower)
#                 * (1 + ArchetypeBonusPct / 100)
#
# Every banked combat number carries that spread as an unlabelled confound.
# Sampling these turns it from a confound into a covariate.
#
# NOTE, against the brief that asked for these: both values were ALREADY
# reflected -- AGothicPlayerCharacter::GetAggregateGearPower and
# GetArchetypeDamageBonusPct are BlueprintPure UFUNCTIONs
# (GothicPlayerCharacter.h:300-306). No reconstruction from
# PlayerState.InventoryComponent.EquippedItems is needed.
# --------------------------------------------------------------------------

@_reader("gear_power")
def _m_gear_power(actor):
    """Aggregate Gear Power -- the damage floor across ALL equipped gear."""
    getter = getattr(actor, "get_aggregate_gear_power", None)
    if getter is None:
        raise LookupError(
            "get_aggregate_gear_power not on %s; it is declared on "
            "AGothicPlayerCharacter (GothicPlayerCharacter.h:300-301), so this "
            "is probably not the player pawn." % actor.get_name())
    return int(getter())


@_reader("active_gear_power")
def _m_active_gear_power(actor):
    """The ACTIVE weapon slot's own Gear Power. Usually 0, and that is CORRECT.

    WHY THIS DISAGREES WITH gear_power, AND WHY NEITHER IS WRONG
    -----------------------------------------------------------
    Reading 0 here against gear_power's 100 on the same pawn looks like a broken
    probe. It is not. The two metrics read two different stores:

      gear_power        AGothicPlayerCharacter::GetAggregateGearPower
                        (GothicPlayerCharacter.cpp:1275-1280) -> the PlayerState's
                        inventory, averaged across equipped items.
      active_gear_power AGothicPlayerCharacter::GetActiveGearPower
                        (GothicPlayerCharacter.cpp:1266-1273) -> literally
                        WeaponSlots[ActiveWeaponIndex].GearPower.

    FGothicWeaponSlot::GearPower defaults to 0 and is only filled when the slot
    is populated FROM THE INVENTORY with a rolled copy. A weapon assigned
    directly on the Blueprint default loadout leaves it at 0, and that zero means
    "baseline, no scaling" by design (GothicWeaponData.h:310-314,
    GothicPlayerCharacter.h:290-293).

    It also feeds nothing. GetActiveGearPower has no callers anywhere in
    Source/; the damage math in UGA_Fire::PerformFireTrace scales off the
    AGGREGATE (GA_Fire.cpp:336-339). So this metric is a loadout-provenance
    signal, not a damage input -- do not put it in a damage regression.

    Returned as a dict rather than a bare int precisely so that zero cannot be
    mistaken for a failed read or for a damage-relevant measurement.
    """
    getter = getattr(actor, "get_active_gear_power", None)
    if getter is None:
        raise LookupError(
            "get_active_gear_power not on %s (GothicPlayerCharacter.h:295-296)."
            % actor.get_name())
    value = int(getter())

    index = common.try_read(
        lambda: int(actor.get_editor_property("active_weapon_index")))
    slots = common.try_read(
        lambda: len(actor.get_editor_property("weapon_slots") or []))

    if isinstance(index, dict) or isinstance(slots, dict):
        meaning = ("could not read active_weapon_index / weapon_slots to "
                   "disambiguate; see the error objects in this row")
    elif not isinstance(slots, int) or slots == 0:
        meaning = ("WeaponSlots is EMPTY, so GetActiveGearPower short-circuits "
                   "to 0 (GothicPlayerCharacter.cpp:1272). This is a missing "
                   "loadout, not a gear reading.")
    elif not (0 <= index < slots):
        meaning = ("active_weapon_index %s is out of range for %d slot(s), so "
                   "GetActiveGearPower returns its fallback 0 "
                   "(GothicPlayerCharacter.cpp:1268-1272). Suspect the slot "
                   "state, not the gear." % (index, slots))
    elif value == 0:
        meaning = ("slot %d exists and its GearPower is genuinely 0 -- a "
                   "Blueprint default-loadout weapon with no rolled copy behind "
                   "it. Expected, and treated as baseline/no scaling." % index)
    else:
        meaning = "slot %d carries a rolled copy with GearPower %d." % (
            index, value)

    return {
        "value": value,
        "active_weapon_index": index,
        "weapon_slot_count": slots,
        "meaning": meaning,
        "feeds_damage": False,
        "note": "GetActiveGearPower has zero callers in Source/; GA_Fire scales "
                "off GetAggregateGearPower (the gear_power metric) instead "
                "(GA_Fire.cpp:336-339). Compare against gear_power only to see "
                "whether the loadout came from the inventory or the Blueprint "
                "default -- never as a contradiction.",
    }


@_reader("archetype_bonus_pct")
def _m_archetype_bonus_pct(actor):
    """Armour's damage bonus for the ACTIVE weapon's archetype, in percent.

    Archetype-matched, exactly as GA_Fire reads it (GA_Fire.cpp:294-296): a
    Revolver line contributes nothing while a Rifle is equipped, so this must be
    resolved against the live weapon rather than summed blind.
    """
    weapon_getter = getattr(actor, "get_active_weapon_data", None)
    bonus_getter = getattr(actor, "get_archetype_damage_bonus_pct", None)
    if weapon_getter is None or bonus_getter is None:
        raise LookupError(
            "get_active_weapon_data / get_archetype_damage_bonus_pct not on %s "
            "(GothicPlayerCharacter.h:287-306)." % actor.get_name())
    weapon = weapon_getter()
    if weapon is None:
        raise LookupError(
            "No active weapon data on %s, so no archetype to score against."
            % actor.get_name())
    return round(float(bonus_getter(weapon.get_editor_property("archetype"))), 3)


def available_metrics():
    return sorted(_METRIC_READERS.keys())


# --------------------------------------------------------------------------
# Lifecycle
# --------------------------------------------------------------------------

def is_running():
    return _tick_handle is not None


def start(targets, metrics, interval_seconds=0.1, max_samples=20000, world=None):
    """Begin sampling.

    targets: list of (label, actor) tuples. Labels appear in the output.
    metrics: list of metric keys (see available_metrics()).
    world:   PIE world, used to stamp game time onto every sample.
    """
    global _tick_handle, _samples, _targets, _metrics, _interval
    global _accumulator, _max_samples, _started_at, _game_origin
    global _world, _stop_reason, _error_streak

    if is_running():
        raise RuntimeError("A probe is already running. Call stop() first.")

    unknown = [m for m in metrics if m not in _METRIC_READERS]
    if unknown:
        raise ValueError(
            "Unknown metrics %s. Available: %s" % (unknown, available_metrics())
        )
    if not targets:
        raise ValueError("No targets to sample.")

    _samples = []
    _targets = list(targets)
    _metrics = list(metrics)
    _interval = max(0.0, float(interval_seconds))
    _max_samples = int(max_samples)
    _accumulator = 0.0
    _started_at = time.time()
    _world = world if world is not None else common.pie_world()
    _game_origin = common.game_time(_world) if _world is not None else None
    _stop_reason = None
    _error_streak = 0
    # The interpreter outlives PIE, so a verdict cached against a previous
    # session's pawn must not be trusted for this one.
    _chosen_action_verified.clear()

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
    return True


def status():
    return {
        "running": is_running(),
        "samples": len(_samples),
        "max_samples": _max_samples,
        "interval_seconds": _interval,
        "metrics": list(_metrics),
        "targets": [label for label, _ in _targets],
        "game_time_origin": _game_origin,
        "elapsed_seconds": round(time.time() - _started_at, 2) if _started_at else None,
        "stop_reason": _stop_reason,
    }


def _validate_targets():
    """Drop targets whose UObject died (PIE ended, actor destroyed).

    Returns False when nothing valid remains, which auto-stops the probe.
    Without this, a stale actor reference raises every single frame.
    """
    global _targets
    _targets = [(label, a) for label, a in _targets if common.is_valid(a)]
    return bool(_targets)


def _on_tick(delta_seconds):
    global _accumulator, _error_streak

    if not is_running():
        return

    try:
        _accumulator += float(delta_seconds)
        if _accumulator < _interval:
            return
        _accumulator = 0.0

        if not _validate_targets():
            stop("all targets became invalid (PIE ended or actors destroyed)")
            return

        if len(_samples) >= _max_samples:
            stop("hit max_samples cap of %d" % _max_samples)
            return

        now_game = common.game_time(_world) if _world is not None else None
        row = {
            "t": round(time.time() - _started_at, 3),
            "gt": now_game,
            "gt_rel": round(now_game - _game_origin, 3)
                      if (now_game is not None and _game_origin is not None) else None,
            "actors": {},
        }
        for label, actor in _targets:
            values = {}
            for key in _metrics:
                try:
                    values[key] = _METRIC_READERS[key](actor)
                except Exception as exc:
                    # Record the failure rather than dropping the row. A metric
                    # that never resolves is itself a finding.
                    values[key] = {"error": "%s: %s" % (type(exc).__name__, exc)}
            row["actors"][label] = values
        _samples.append(row)
        _error_streak = 0

    except Exception as exc:
        _error_streak += 1
        unreal.log_error("[VigilProbe] tick error: %s" % exc)
        if _error_streak >= _MAX_ERROR_STREAK:
            stop("auto-stopped after %d consecutive tick errors" % _error_streak)


def dump(filename_stem="probe", keep_buffer=False):
    """Write the buffer to JSON and return the path.

    Returning a path rather than the data keeps the MCP response tiny -- the
    agent reads the file with its own tools and can grep a 20k-row time series
    without ever putting it in a tool result.
    """
    global _samples

    payload = {
        "recorded_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "meta": status(),
        "schema": {
            "t": "wall seconds since probe start",
            "gt": "absolute game time (respects pause and slomo)",
            "gt_rel": "game seconds since probe start -- correlate scenarios on this",
            "actors": "label -> {metric: value}; value may be {'error': ...}",
        },
        "samples": _samples,
    }
    path = common.write_json(payload, filename_stem)
    count = len(_samples)
    if not keep_buffer:
        _samples = []
    return {"path": path, "samples_written": count}
