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


@unreal.uclass()
class VigEncounterTools(unreal.ToolsetDefinition):
    """Vigil encounter diagnostics: boss phase state, destructible
    zone status, Selah balances, and encounter volume occupancy.
    Built for Eagle's Landing vertical slice tuning."""

    @toolset_registry.tool_call
    @staticmethod
    def dump_boss_state(actor_label: str = "") -> dict:
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

    @toolset_registry.tool_call
    @staticmethod
    def dump_selah_state() -> dict:
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

    @toolset_registry.tool_call
    @staticmethod
    def dump_encounter_volumes() -> dict:
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

    @toolset_registry.tool_call
    @staticmethod
    def check_pillar_destruction() -> dict:
        """Find all actors tagged BestialLucidZone and report their
        state: intact vs destroyed, location, and distance from
        the rotunda center.

        Returns:
            Dictionary with each pillar zone's status.
        """
        world = VigEncounterTools._get_world()
        if isinstance(world, dict):
            return world

        all_actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Actor
        )

        pillars = []
        for actor in all_actors:
            tags = actor.tags if hasattr(actor, "tags") else []
            tag_strs = [str(t) for t in tags]
            if "BestialLucidZone" in tag_strs:
                entry = {
                    "label": actor.get_actor_label(),
                    "class": actor.get_class().get_name(),
                    "location": VigEncounterTools._vec_to_dict(
                        actor.get_actor_location()
                    ),
                    "tags": tag_strs,
                    "visible": actor.is_hidden() is False,
                    "collision_enabled": (
                        actor.get_component_by_class(
                            unreal.StaticMeshComponent
                        ) is not None
                    ),
                }
                pillars.append(entry)

        return {
            "pillar_count": len(pillars),
            "pillars": pillars,
        }

    # -- Internal helpers --

    @staticmethod
    def _get_world():
        try:
            return unreal.get_editor_subsystem(
                unreal.UnrealEditorSubsystem
            ).get_editor_world()
        except Exception:
            return {"error": "No editor world available"}

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
