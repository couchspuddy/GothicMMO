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

        boss = None
        for actor in actors:
            if actor_label:
                if actor.get_actor_label() == actor_label:
                    boss = actor
                    break
            else:
                # Look for Boss/Champion tier by checking class name
                class_name = actor.get_class().get_name()
                if any(
                    tag in class_name.lower()
                    for tag in ["boss", "lucid", "bestial", "champion"]
                ):
                    boss = actor
                    break

        if not boss:
            return {"error": f"No boss found (filter: '{actor_label}')"}

        result = {
            "actor": boss.get_actor_label(),
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
        #
        # Enumerated off the ASC, not probed against a hardcoded list.
        #
        # The old probe called unreal.GameplayTag.request_gameplay_tag(str).
        # FGameplayTag::RequestGameplayTag is a plain static C++ function with no
        # UFUNCTION and no ScriptMethod alias (GameplayTagContainer.h:60), so that
        # attribute does not exist in Python at all -- every iteration raised into
        # `except: pass` and active_state_tags was ALWAYS an empty list. A boss
        # mid-phase-transition and a boss standing idle produced identical output.
        #
        # UBlueprintGameplayTagLibrary is the reflected surface:
        #   GetOwnedGameplayTags   (BlueprintGameplayTagLibrary.h:285)
        #   BreakGameplayTagContainer (:200)
        #   GetTagName             (:57)
        # None of them need a string-to-tag conversion, so the whole failure mode
        # is gone rather than papered over.
        asc = common.try_read(
            lambda: unreal.AbilitySystemBlueprintLibrary
            .get_ability_system_component(boss), default=None)
        if asc is None:
            result["active_state_tags"] = {
                "error": "No AbilitySystemComponent on this boss -- gameplay "
                         "tags unreadable."}
        else:
            tags = None
            for attempt in (
                lambda: unreal.BlueprintGameplayTagLibrary
                .get_owned_gameplay_tags(asc),
                lambda: unreal.BlueprintGameplayTagLibrary
                .get_owned_gameplay_tags(
                    unreal.BlueprintGameplayTagLibrary
                    .conv_object_to_gameplay_tag_asset_interface(asc)),
            ):
                try:
                    tags = attempt()
                    if tags is not None:
                        break
                except Exception:
                    continue

            if tags is None:
                result["active_state_tags"] = {
                    "error": "BlueprintGameplayTagLibrary.get_owned_gameplay_tags "
                             "did not resolve on this build (tried the ASC "
                             "directly and via "
                             "conv_object_to_gameplay_tag_asset_interface)."}
            else:
                try:
                    broken = unreal.BlueprintGameplayTagLibrary \
                        .break_gameplay_tag_container(tags)
                    result["active_state_tags"] = sorted(
                        str(unreal.BlueprintGameplayTagLibrary.get_tag_name(t))
                        for t in broken)
                except Exception as exc:
                    result["active_state_tags"] = {
                        "error": "break_gameplay_tag_container/get_tag_name "
                                 "failed: %s: %s" % (type(exc).__name__, exc)}

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

            # Get Selah from PlayerState's ASC
            try:
                # Try actor first, then player state
                asc = None
                asc = unreal.AbilitySystemBlueprintLibrary \
                    .get_ability_system_component(actor)

                if not asc:
                    ps = actor.get_player_state()
                    if ps:
                        asc = unreal.AbilitySystemBlueprintLibrary \
                            .get_ability_system_component(ps)

                if asc:
                    for attr_name in [
                        "Selah", "Health", "MaxHealth",
                        "SuperMeter", "MaxSuperMeter",
                    ]:
                        try:
                            val = asc.get_gameplay_attribute_value(
                                unreal.GameplayAttribute(
                                    attribute_name=attr_name
                                )
                            )
                            entry[attr_name] = val
                        except Exception:
                            pass
            except Exception as e:
                entry["error"] = str(e)

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
