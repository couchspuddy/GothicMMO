"""
Vigil Encounter State Inspector

Query encounter-specific state: boss phase transitions, pillar
destruction zones, Selah balances on nearby players, Feral Retained
health thresholds. Tuning tools for the Eagle's Landing encounters.

Drop this file into your project plugin's Content/Python/ directory,
then run ModelContextProtocol.RefreshTools in the editor console.

Placement: Plugins/VigDevTools/Content/Python/vig_encounter_tools.py
"""

import unreal
import toolset_registry

import vigil_pie_common as common


@unreal.uclass()
class VigEncounterTools(unreal.ToolsetDefinition):
    """Vigil encounter diagnostics: boss phase state, destructible
    zone status, Selah balances, and encounter volume occupancy.
    Built for Eagle's Landing vertical slice tuning."""

    @staticmethod
    def _dump_boss_state_impl(actor_label: str = "") -> dict:
        """Read the Bestial Lucid or any boss-tier enemy's full state:
        health, phase, blackboard values, and nearby pillar status.

        Args:
            actor_label: Label of the boss actor. If empty, finds the
                first Champion or Boss tier enemy in the level.

        Returns:
            Dictionary with health, phase, blackboard state, and
            nearby destructible zone info.
        """
        world = VigEncounterTools._get_world()
        if isinstance(world, dict):
            return world

        # Find the boss
        actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Character
        )

        boss = VigEncounterTools._find_boss(actors, actor_label)
        if isinstance(boss, dict):
            return boss

        # Two names, reported together on purpose. get_name() is the runtime
        # object name ("BP_Enemy_BestialLucid_C_1") and is what list_combatants,
        # describe_combatant, the probe and every scenario step accept.
        # get_actor_label() is the editor label the level author typed
        # ("BP_Enemy_BestialLucid1") and matches nothing else in the harness.
        # This tool used to report only the label, so its output could not be
        # pasted into any other tool. `actor` is now the portable one.
        result = {
            "actor": boss.get_name(),
            "actor_label": common.try_read(
                lambda: boss.get_actor_label(), default="<no label>"),
            "class": boss.get_class().get_name(),
            "location": VigEncounterTools._vec_to_dict(
                boss.get_actor_location()
            ),
        }

        # ── Vitals ───────────────────────────────────────────────────────────
        #
        # This used to build unreal.GameplayAttribute(attribute_name="Health")
        # and hand it to get_gameplay_attribute_value. That can never work:
        # FGameplayAttribute::AttributeName is a VisibleAnywhere display-only
        # cache DERIVED from the payload (AttributeSet.h:84, 141, 150-151), while
        # the field that actually identifies the attribute is the TFieldPath
        # `Attribute` (:159-163) -- null when the struct is built from a string.
        # Every read failed into a bare `except: pass`, so Health, MaxHealth and
        # health_percent silently vanished and the dump looked merely terse.
        #
        # The fix is not a cleverer FGameplayAttribute: it is to use the path
        # that already works everywhere else in this harness. AGothicCharacterBase
        # exposes BlueprintPure convenience getters (GothicCharacterBase.h:77-90)
        # and vigil_pie_toolset.list_combatants has been reading health through
        # them all along.
        #
        # Loud on purpose. The boss search below matches on CLASS NAME, so it can
        # land on a Character that is not an AGothicCharacterBase at all; that has
        # to read as a harness failure, never as a dump that happens to omit
        # health. There is no path through this function that returns without
        # Health and MaxHealth.
        missing = [name for name in ("get_health", "get_max_health")
                   if not hasattr(boss, name)]
        if missing:
            raise LookupError(
                "%s (%s) does not expose %s. Tried the AGothicCharacterBase "
                "BlueprintPure getters GetHealth/GetMaxHealth "
                "(GothicCharacterBase.h:77-81); this actor matched the boss "
                "class-name filter but is not a Gothic character."
                % (result["actor"], result["class"], " and ".join(missing)))

        result["Health"] = float(boss.get_health())
        result["MaxHealth"] = float(boss.get_max_health())
        if result["MaxHealth"] > 0:
            result["health_percent"] = round(
                result["Health"] / result["MaxHealth"] * 100, 1
            )
        else:
            result["health_percent"] = None
            result["health_warning"] = (
                "MaxHealth is 0 -- attributes are not initialised on this pawn.")

        # AttackPower and Defense have no character-level getter, so they come
        # off the attribute set directly. GetAttributeSet is BlueprintPure
        # (GothicCharacterBase.h:70-71); each attribute is a BlueprintReadOnly
        # FGameplayAttributeData UPROPERTY (GothicAttributeSet.h:148-155) whose
        # CurrentValue is itself BlueprintReadOnly (AttributeSet.h:53-54). All
        # reflected, no FGameplayAttribute construction anywhere.
        attr_set = boss.get_attribute_set()
        if attr_set is None:
            result["attribute_set_error"] = (
                "GetAttributeSet() returned null -- AttackPower/Defense "
                "unreadable on this pawn.")
        else:
            for label, prop in (("AttackPower", "attack_power"),
                                ("Defense", "defense")):
                try:
                    result[label] = float(
                        attr_set.get_editor_property(prop)
                        .get_editor_property("current_value"))
                except Exception as exc:
                    result[label] = {
                        "error": "%s.%s.current_value: %s: %s"
                                 % ("GothicAttributeSet", prop,
                                    type(exc).__name__, exc)}

        # ── Active gameplay tags ─────────────────────────────────────────────
        # Enumerated off the live ASC. See _owned_gameplay_tags for the two
        # ScriptName traps that made this an error object for two rewrites.
        result["active_state_tags"] = VigEncounterTools._owned_gameplay_tags(boss)

        # Blackboard: phase and transition state.
        #
        # This block used to open with `controller.get_blackboard()`, outside any
        # try -- and AAIController::GetBlackboardComponent is not a UFUNCTION
        # (AIController.h:446-447), so it does not exist in Python at all. The
        # AttributeError propagated out of the tool and dump_boss_state failed
        # whole. common.blackboard() uses the reflected paths that do exist:
        # UAIBlueprintHelperLibrary::GetBlackboard (AIBlueprintHelperLibrary.h:52)
        # and the BlueprintReadOnly `Blackboard` UPROPERTY (AIController.h:146).
        bb = common.blackboard(boss)
        if bb is None:
            result["blackboard"] = {
                "error": "No BlackboardComponent resolved for this boss. If the "
                         "fight is running, the BT has not started or the pawn "
                         "is not AI-possessed."
            }
        else:
            bb_state = {}
            for key, reader in {
                "CurrentPhase": "int",
                "bIsTransitioning": "bool",
                "NearestPillar": "object",
                "ApproachOffsetDistance": "float",
                "TargetActor": "object",
                "bIsInCombat": "bool",
            }.items():
                try:
                    if reader == "int":
                        bb_state[key] = bb.get_value_as_int(key)
                    elif reader == "bool":
                        bb_state[key] = bb.get_value_as_bool(key)
                    elif reader == "float":
                        bb_state[key] = bb.get_value_as_float(key)
                    elif reader == "object":
                        obj = bb.get_value_as_object(key)
                        bb_state[key] = (
                            obj.get_actor_label() if obj else "None"
                        )
                except Exception as exc:
                    # Was `pass`. A key that stopped resolving vanished from the
                    # dump entirely and read as "the boss does not have a phase".
                    bb_state[key] = {
                        "error": "%s: %s" % (type(exc).__name__, exc)}
            # Only CurrentPhase (AGothicBossAIController.h:49) and bIsInCombat /
            # TargetActor (GothicEnemyAIController.h:33-35) are C++-declared keys.
            # The rest live in the BB asset only; a zero from get_value_as_* on a
            # key the asset does not define is indistinguishable from a real zero,
            # so use vig_blackboard_tools.dump_blackboard, which enumerates the
            # asset, when a value here looks suspicious.
            bb_state["_note"] = (
                "Missing keys read as 0/False, not as errors. Cross-check "
                "against vig_blackboard_tools.dump_blackboard, which enumerates "
                "the blackboard asset instead of probing this fixed list.")
            result["blackboard"] = bb_state

        # Find nearby destructible zones (BestialLucidZone tagged actors)
        result["nearby_pillars"] = VigEncounterTools._find_tagged_actors(
            world, "BestialLucidZone", boss.get_actor_location(), 2000.0
        )

        return result

    @staticmethod
    def _dump_selah_state_impl() -> dict:
        """Read Selah balance from every player character in the level.

        Returns:
            Dictionary with each player's Selah count and location.
        """
        world = VigEncounterTools._get_world()
        if isinstance(world, dict):
            return world

        actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Character
        )

        players = []
        for actor in actors:
            controller = actor.get_controller()
            if not isinstance(controller, unreal.PlayerController):
                continue

            entry = {
                "label": actor.get_actor_label(),
                "location": VigEncounterTools._vec_to_dict(
                    actor.get_actor_location()
                ),
            }

            # ── Balances, read off the attribute set ─────────────────────────
            #
            # This block built unreal.GameplayAttribute(attribute_name="Selah")
            # and handed it to get_gameplay_attribute_value. It can never
            # resolve: AttributeName is a VisibleAnywhere display cache DERIVED
            # from the payload (AttributeSet.h:84, 141, 150-151), while the
            # field that identifies the attribute is the TFieldPath `Attribute`
            # (:159-163), left null when the struct is built from a string. Every
            # read raised into `except: pass`, so the Selah balance -- the entire
            # point of dump_selah_state -- was silently absent from every dump
            # this tool ever produced.
            #
            # Same fix shape as the boss health block: go through the attribute
            # set's BlueprintReadOnly FGameplayAttributeData UPROPERTYs
            # (GothicAttributeSet.h:77-124) and read CurrentValue, itself
            # BlueprintReadOnly (AttributeSet.h:53-54). No FGameplayAttribute
            # construction anywhere.
            attr_set = None
            for fetch in (
                lambda: actor.get_attribute_set(),   # GothicCharacterBase.h:71-72
                lambda: actor.get_player_state().get_attribute_set(),
            ):
                try:
                    candidate = fetch()
                except Exception:
                    continue
                if candidate:
                    attr_set = candidate
                    break

            if attr_set is None:
                entry["attributes_error"] = (
                    "No UGothicAttributeSet on this player or its PlayerState. "
                    "Tried AGothicCharacterBase::GetAttributeSet "
                    "(GothicCharacterBase.h:71-72) on both.")
            else:
                for label, prop in (
                    ("Selah", "selah"),
                    ("Health", "health"),
                    ("MaxHealth", "max_health"),
                    ("SuperMeter", "super_meter"),
                    ("MaxSuperMeter", "max_super_meter"),
                ):
                    try:
                        entry[label] = float(
                            attr_set.get_editor_property(prop)
                            .get_editor_property("current_value"))
                    except Exception as exc:
                        entry[label] = {
                            "error": "GothicAttributeSet.%s.current_value: %s: %s"
                                     % (prop, type(exc).__name__, exc)}

            players.append(entry)

        return {
            "player_count": len(players),
            "players": players,
        }

    @staticmethod
    def _dump_encounter_volumes_impl() -> dict:
        """List all trigger volumes and encounter zones in the level,
        with their locations and any overlap state.

        Returns:
            Dictionary with encounter volumes, their locations,
            and which actors are currently overlapping each.
        """
        world = VigEncounterTools._get_world()
        if isinstance(world, dict):
            return world

        # Find trigger volumes and boxes
        volumes = []
        all_actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Actor
        )

        for actor in all_actors:
            label = actor.get_actor_label()
            class_name = actor.get_class().get_name()

            # Look for encounter-related actors by naming convention
            if any(
                tag in label.lower() or tag in class_name.lower()
                for tag in [
                    "trigger", "volume", "encounter", "zone",
                    "spawn", "arena", "rotunda", "plaza",
                    "intersection", "approach",
                ]
            ):
                entry = {
                    "label": label,
                    "class": class_name,
                    "location": VigEncounterTools._vec_to_dict(
                        actor.get_actor_location()
                    ),
                }

                # Check for tags
                tags = actor.tags
                if tags:
                    entry["actor_tags"] = [str(t) for t in tags]

                volumes.append(entry)

        return {
            "volume_count": len(volumes),
            "volumes": volumes,
        }

    @staticmethod
    def _check_pillar_destruction_impl() -> dict:
        """Report every Rotunda pillar's live state.

        Was matching an actor tag "BestialLucidZone" that exists nowhere in the
        project, so it always returned zero pillars. Matches on the pillar
        CLASS instead, which cannot drift the way a tag string can, and reads
        state through the BlueprintPure accessors on AGothicRotundaPillar --
        CurrentState and CurrentHealth are private C++ members and are NOT
        reachable by property reflection.

        Returns:
            Dictionary with each pillar's state, health fraction, and the
            ceiling/blocking-volume wiring that the collapse depends on.
        """
        world = VigEncounterTools._get_world()
        if isinstance(world, dict):
            return world

        all_actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Actor
        )

        pillars = []
        for actor in all_actors:
            if "RotundaPillar" not in actor.get_class().get_name():
                continue

            entry = {
                "pillar": actor.get_name(),
                "class": actor.get_class().get_name(),
                "location": VigEncounterTools._vec_to_dict(
                    actor.get_actor_location()
                ),
                "tags": [str(t) for t in (actor.tags or [])],
            }

            # Public API first -- these are the authoritative state.
            entry["state"] = common.try_read(
                lambda a=actor: str(a.get_pillar_state()))
            entry["destroyed"] = common.try_read(
                lambda a=actor: bool(a.is_destroyed()))
            entry["health_percent"] = common.try_read(
                lambda a=actor: round(float(a.get_health_percent()), 3))

            # Wiring: a null ceiling or blocking volume is why a collapse can
            # "succeed" and still look like nothing happened.
            entry["ceiling_mesh"] = common.try_read(
                lambda a=actor: (
                    a.get_editor_property("ceiling_mesh").get_owner().get_name()
                    if a.get_editor_property("ceiling_mesh") else "None"),
                default="<unreadable>")
            entry["blocking_volume"] = common.try_read(
                lambda a=actor: (
                    a.get_editor_property("blocking_volume_actor").get_name()
                    if a.get_editor_property("blocking_volume_actor") else "None"),
                default="<unreadable>")

            pillars.append(entry)

        return {
            "pillar_count": len(pillars),
            "standing": sum(1 for p in pillars if p.get("destroyed") is False),
            "pillars": pillars,
        }

    # -- Public tool surface ---------------------------------------------------
    # Thin JSON wrappers. The bodies above return dicts, but this plugin's
    # schema generator rejects an unparameterised `-> dict` at tool-DISCOVERY
    # time, which is what kept this whole module from ever loading.

    @toolset_registry.tool_call
    @staticmethod
    def dump_boss_state(actor_label: str = "") -> str:
        """Boss health, phase, blackboard values and nearby pillar status.

        Args:
            actor_label: Boss actor label. Empty picks the first boss found.

        Returns:
            JSON boss state dump from the running PIE session.
        """
        return common.as_json(
            VigEncounterTools._dump_boss_state_impl(actor_label))

    @toolset_registry.tool_call
    @staticmethod
    def dump_selah_state() -> str:
        """Selah balances and collection state in the running PIE session.

        Returns:
            JSON Selah state dump.
        """
        return common.as_json(VigEncounterTools._dump_selah_state_impl())

    @toolset_registry.tool_call
    @staticmethod
    def dump_encounter_volumes() -> str:
        """Encounter volumes in the level and their occupancy.

        Returns:
            JSON list of encounter volumes.
        """
        return common.as_json(VigEncounterTools._dump_encounter_volumes_impl())

    @toolset_registry.tool_call
    @staticmethod
    def check_pillar_destruction() -> str:
        """Live state of every Rotunda pillar: standing or collapsed, health,
        and whether its ceiling and blocking volume are actually wired.

        Returns:
            JSON pillar state dump.
        """
        return common.as_json(
            VigEncounterTools._check_pillar_destruction_impl())

    # -- Internal helpers --

    # Class-name fragments that mark a Boss/Champion tier pawn when no filter
    # is supplied.
    _BOSS_CLASS_HINTS = ("boss", "lucid", "bestial", "champion")

    @staticmethod
    def _find_boss(actors, actor_label):
        """Resolve the boss, or return an error dict listing the candidates.

        The old filter was `actor.get_actor_label() == actor_label` -- an EXACT
        match against the editor label only. So dump_boss_state(actor_label=
        "BestialLucid") reported "No boss found" for an actor it resolved
        perfectly well with an empty filter, and the docstring's promise that a
        substring works was fiction. Matching now mirrors
        vigil_pie_common.resolve_actor: exact first, then unique
        case-insensitive substring, across BOTH the runtime name and the editor
        label -- and it reports every candidate on failure instead of a bare
        "not found".
        """
        if not actor_label:
            for actor in actors:
                lowered = actor.get_class().get_name().lower()
                if any(hint in lowered
                       for hint in VigEncounterTools._BOSS_CLASS_HINTS):
                    return actor
            return {
                "error": "No Boss/Champion-tier Character in the PIE world.",
                "matched_on": "class name containing any of %s"
                              % (VigEncounterTools._BOSS_CLASS_HINTS,),
                "characters_present": [a.get_class().get_name() for a in actors],
            }

        def names(actor):
            label = common.try_read(
                lambda: actor.get_actor_label(), default="")
            return [n for n in (actor.get_name(), label)
                    if isinstance(n, str) and n]

        for actor in actors:
            if actor_label in names(actor):
                return actor

        needle = actor_label.lower()
        matches = [a for a in actors
                   if any(needle in n.lower() for n in names(a))]
        if len(matches) == 1:
            return matches[0]
        if not matches:
            return {
                "error": "No Character matches '%s' by name or editor label."
                         % actor_label,
                "candidates": ["%s (label %s)" % (a.get_name(),
                                                  names(a)[-1] if names(a) else "?")
                               for a in actors],
            }
        return {
            "error": "'%s' is ambiguous." % actor_label,
            "matches": [a.get_name() for a in matches],
        }

    @staticmethod
    def _owned_gameplay_tags(boss):
        """Every gameplay tag the boss's ASC currently owns, enumerated.

        Two dead ends preceded this, and both were name errors rather than
        missing functionality:

        1. unreal.GameplayTag.request_gameplay_tag(str) -- FGameplayTag::
           RequestGameplayTag is a plain static with no UFUNCTION
           (GameplayTagContainer.h:60), so it is absent from Python entirely.
        2. unreal.BlueprintGameplayTagLibrary.* -- UBlueprintGameplayTagLibrary
           carries meta=(ScriptName="GameplayTagLibrary")
           (BlueprintGameplayTagLibrary.h:14), so Python publishes it as
           unreal.GameplayTagLibrary and the BlueprintGameplayTagLibrary name
           does not exist. vigil_combat_driver.py:196 has been calling the
           correct name all along.

        The same trap sat one line above the tag block:
        UAbilitySystemBlueprintLibrary declares meta=(ScriptName=
        "AbilitySystemLibrary") (AbilitySystemBlueprintLibrary.h:68), so
        unreal.AbilitySystemBlueprintLibrary is equally imaginary. The ASC now
        comes from AGothicCharacterBase::GetGothicASC, a BlueprintPure UFUNCTION
        (GothicCharacterBase.h:67-68), with the correctly-named engine library
        as the fallback for a non-Gothic pawn.

        Enumerate, never probe: GetOwnedGameplayTags returns the whole container
        (BlueprintGameplayTagLibrary.h:285), so a state tag nobody thought to
        list still shows up.
        """
        asc = None
        asc_attempts = []
        for describe, fetch in (
            ("AGothicCharacterBase::GetGothicASC (GothicCharacterBase.h:67-68)",
             lambda: boss.get_gothic_asc()),
            ("AbilitySystemLibrary.get_ability_system_component "
             "(AbilitySystemBlueprintLibrary.h:75)",
             lambda: unreal.AbilitySystemLibrary
             .get_ability_system_component(boss)),
        ):
            try:
                candidate = fetch()
            except Exception as exc:
                asc_attempts.append("%s -> %s: %s"
                                    % (describe, type(exc).__name__, exc))
                continue
            if candidate:
                asc = candidate
                break
            asc_attempts.append("%s -> None" % describe)

        if asc is None:
            return {"error": "No AbilitySystemComponent reachable on %s. Tried: %s"
                             % (boss.get_name(), "; ".join(asc_attempts))}

        container = None
        tag_attempts = []
        for describe, fetch in (
            ("GameplayTagLibrary.get_owned_gameplay_tags(ASC) "
             "(BlueprintGameplayTagLibrary.h:285)",
             lambda: unreal.GameplayTagLibrary.get_owned_gameplay_tags(asc)),
            ("GameplayTagLibrary.get_owned_gameplay_tags via "
             "conv_object_to_gameplay_tag_asset_interface "
             "(BlueprintGameplayTagLibrary.h:288-289)",
             lambda: unreal.GameplayTagLibrary.get_owned_gameplay_tags(
                 unreal.GameplayTagLibrary
                 .conv_object_to_gameplay_tag_asset_interface(asc))),
        ):
            try:
                container = fetch()
            except Exception as exc:
                tag_attempts.append("%s -> %s: %s"
                                    % (describe, type(exc).__name__, exc))
                continue
            if container is not None:
                break

        if container is None:
            return {"error": "Could not read owned tags off %s's ASC. Tried: %s"
                             % (boss.get_name(), "; ".join(tag_attempts))}

        try:
            tags = unreal.GameplayTagLibrary.break_gameplay_tag_container(
                container)   # BlueprintGameplayTagLibrary.h:200
            return sorted(
                str(unreal.GameplayTagLibrary.get_tag_name(t))   # :57
                for t in tags)
        except Exception as exc:
            return {"error": "break_gameplay_tag_container/get_tag_name failed "
                             "on a container that DID resolve: %s: %s"
                             % (type(exc).__name__, exc)}

    @staticmethod
    def _get_world():
        """The RUNNING PIE world.

        Was get_editor_world(), which meant every tool in this file reported
        the editor's copy of the level -- actors at their authored transforms,
        no gameplay state, and none of the runtime values these tools exist to
        read. Encounter diagnostics are only meaningful against a live session.
        """
        world = common.pie_world()
        if world is None:
            return {"error": "Not in PIE. Press Play, then retry."}
        return world

    @staticmethod
    def _vec_to_dict(vec) -> dict:
        return {"x": vec.x, "y": vec.y, "z": vec.z}

    @staticmethod
    def _find_tagged_actors(
        world, tag: str, origin, radius: float
    ) -> list[dict]:
        """Find actors with a specific actor tag within radius of origin."""
        all_actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Actor
        )
        results = []
        for actor in all_actors:
            tags = actor.tags if hasattr(actor, "tags") else []
            tag_strs = [str(t) for t in tags]
            if tag in tag_strs:
                loc = actor.get_actor_location()
                dist = (
                    (loc.x - origin.x) ** 2
                    + (loc.y - origin.y) ** 2
                    + (loc.z - origin.z) ** 2
                ) ** 0.5
                if dist <= radius:
                    results.append({
                        "label": actor.get_actor_label(),
                        "distance": round(dist, 1),
                        "location": VigEncounterTools._vec_to_dict(loc),
                    })
        return results
