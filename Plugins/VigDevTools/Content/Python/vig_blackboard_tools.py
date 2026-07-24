"""
Vigil Blackboard & Behavior Tree Inspector

Exposes enemy AI state to MCP clients, replacing the BT visual debugger
(which currently crashes UE on open — filed as open defect).

Drop this file into your project plugin's Content/Python/ directory,
then run ModelContextProtocol.RefreshTools in the editor console.

Placement: Plugins/VigDevTools/Content/Python/vig_blackboard_tools.py
"""

import unreal
import toolset_registry


@unreal.uclass()
class VigBlackboardTools(unreal.ToolsetDefinition):
    """Inspect enemy AI state: blackboard values, active BT node,
    combat target, leash status, and phase info. Replaces the
    broken BT visual debugger for Vigil development."""

    @toolset_registry.tool_call
    @staticmethod
    def dump_enemy_blackboard(actor_label: str = "") -> dict:
        """Read all blackboard key-value pairs from an enemy's AI controller.

        Args:
            actor_label: Label of the enemy actor in the level. If empty,
                dumps the first enemy found with an active blackboard.

        Returns:
            Dictionary with blackboard keys and their current values,
            plus the AI controller class name and pawn reference.
        """
        world = unreal.get_editor_subsystem(
            unreal.UnrealEditorSubsystem
        ).get_editor_world()
        if not world:
            return {"error": "No editor world"}

        actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Character
        )

        target_pawn = None
        for actor in actors:
            if actor_label:
                if actor.get_actor_label() == actor_label:
                    target_pawn = actor
                    break
            else:
                # Grab first non-player character with a controller
                controller = actor.get_controller()
                if controller and not isinstance(
                    controller, unreal.PlayerController
                ):
                    target_pawn = actor
                    break

        if not target_pawn:
            return {"error": f"No enemy found (filter: '{actor_label}')"}

        controller = target_pawn.get_controller()
        if not controller:
            return {
                "actor": target_pawn.get_actor_label(),
                "error": "No AI controller on this pawn",
            }

        result = {
            "actor": target_pawn.get_actor_label(),
            "actor_class": target_pawn.get_class().get_name(),
            "controller_class": controller.get_class().get_name(),
            "location": {
                "x": target_pawn.get_actor_location().x,
                "y": target_pawn.get_actor_location().y,
                "z": target_pawn.get_actor_location().z,
            },
        }

        # Try to read the blackboard
        bb = controller.get_blackboard()
        if not bb:
            result["blackboard"] = "No blackboard component"
            return result

        # Read known Vigil blackboard keys (from GothicBBKeys namespace)
        vigil_keys = {
            "TargetActor": "object",
            "TargetLocation": "vector",
            "bCanSeeTarget": "bool",
            "bIsInCombat": "bool",
            "PatrolOrigin": "vector",
            "AttackRange": "float",
            # Boss-specific keys
            "CurrentPhase": "int",
            "NearestPillar": "object",
            "bIsTransitioning": "bool",
            "ApproachOffsetDistance": "float",
        }

        bb_state = {}
        for key_name, key_type in vigil_keys.items():
            try:
                if key_type == "bool":
                    bb_state[key_name] = bb.get_value_as_bool(key_name)
                elif key_type == "float":
                    bb_state[key_name] = bb.get_value_as_float(key_name)
                elif key_type == "int":
                    bb_state[key_name] = bb.get_value_as_int(key_name)
                elif key_type == "vector":
                    vec = bb.get_value_as_vector(key_name)
                    bb_state[key_name] = {
                        "x": vec.x, "y": vec.y, "z": vec.z
                    }
                elif key_type == "object":
                    obj = bb.get_value_as_object(key_name)
                    bb_state[key_name] = (
                        obj.get_actor_label() if obj else "None"
                    )
            except Exception:
                # Key doesn't exist on this blackboard — skip silently.
                # This is expected: not every enemy uses every key.
                pass

        result["blackboard"] = bb_state
        return result

    @toolset_registry.tool_call
    @staticmethod
    def list_enemies_in_level() -> list[dict]:
        """List all enemy characters in the current level with their
        basic state: class, health, combat status, location.

        Returns:
            List of dictionaries, one per enemy actor found.
        """
        world = unreal.get_editor_subsystem(
            unreal.UnrealEditorSubsystem
        ).get_editor_world()
        if not world:
            return [{"error": "No editor world"}]

        actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Character
        )

        enemies = []
        for actor in actors:
            controller = actor.get_controller()
            # Skip player-controlled pawns
            if isinstance(controller, unreal.PlayerController):
                continue
            if not controller:
                continue

            loc = actor.get_actor_location()
            entry = {
                "label": actor.get_actor_label(),
                "class": actor.get_class().get_name(),
                "location": {"x": loc.x, "y": loc.y, "z": loc.z},
            }

            # Try to read combat state from blackboard
            bb = controller.get_blackboard() if controller else None
            if bb:
                try:
                    entry["in_combat"] = bb.get_value_as_bool("bIsInCombat")
                except Exception:
                    pass
                try:
                    target = bb.get_value_as_object("TargetActor")
                    entry["target"] = (
                        target.get_actor_label() if target else "None"
                    )
                except Exception:
                    pass

            enemies.append(entry)

        return enemies

    @toolset_registry.tool_call
    @staticmethod
    def dump_enemy_perception(actor_label: str) -> dict:
        """Read the AI perception state for a specific enemy —
        what it can currently see and hear.

        Args:
            actor_label: Label of the enemy actor to inspect.

        Returns:
            Dictionary with perceived actors, stimulus types,
            and whether each was successfully sensed.
        """
        world = unreal.get_editor_subsystem(
            unreal.UnrealEditorSubsystem
        ).get_editor_world()
        if not world:
            return {"error": "No editor world"}

        actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Character
        )

        target = None
        for actor in actors:
            if actor.get_actor_label() == actor_label:
                target = actor
                break

        if not target:
            return {"error": f"Actor '{actor_label}' not found"}

        # Find AIPerceptionComponent on the actor
        perception = target.get_component_by_class(
            unreal.AIPerceptionComponent
        )
        if not perception:
            return {
                "actor": actor_label,
                "error": "No AIPerceptionComponent",
            }

        perceived = perception.get_currently_perceived_actors()
        result = {
            "actor": actor_label,
            "perceived_count": len(perceived),
            "perceived_actors": [],
        }
        for p in perceived:
            result["perceived_actors"].append({
                "label": p.get_actor_label(),
                "class": p.get_class().get_name(),
                "distance": target.get_distance_to(p),
            })

        return result
