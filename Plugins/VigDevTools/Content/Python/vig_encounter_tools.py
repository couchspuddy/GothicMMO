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

        # Health from ASC
        try:
            asc = unreal.AbilitySystemBlueprintLibrary \
                .get_ability_system_component(boss)
            if asc:
                # Read health attributes
                for attr_name in [
                    "Health", "MaxHealth", "AttackPower", "Defense"
                ]:
                    try:
                        val = asc.get_gameplay_attribute_value(
                            unreal.GameplayAttribute(
                                attribute_name=attr_name
                            )
                        )
                        result[attr_name] = val
                    except Exception:
                        pass

                if "Health" in result and "MaxHealth" in result:
                    max_hp = result["MaxHealth"]
                    if max_hp > 0:
                        result["health_percent"] = round(
                            result["Health"] / max_hp * 100, 1
                        )

                # Check state tags
                state_tags = [
                    "State.Dead",
                    "State.PhaseTransition",
                    "State.Attacking",
                    "State.Stunned",
                ]
                active_states = []
                for tag_str in state_tags:
                    try:
                        tag = unreal.GameplayTag.request_gameplay_tag(
                            tag_str
                        )
                        if asc.has_matching_gameplay_tag(tag):
                            active_states.append(tag_str)
                    except Exception:
                        pass
                result["active_state_tags"] = active_states
        except Exception as e:
            result["asc_error"] = str(e)

        # Blackboard: phase and transition state
        controller = boss.get_controller()
        if controller:
            bb = controller.get_blackboard()
            if bb:
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
                                obj.get_actor_label()
                                if obj
                                else "None"
                            )
                    except Exception:
                        pass
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
