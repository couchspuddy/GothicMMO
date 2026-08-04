"""
Vigil GAS Diagnostics

Query ability system state on any actor: attributes, active effects,
granted abilities, cooldowns. Replaces the UE_LOG → recompile → PIE
cycle for GAS debugging.

Drop this file into your project plugin's Content/Python/ directory,
then run ModelContextProtocol.RefreshTools in the editor console.

Placement: Plugins/VigDevTools/Content/Python/vig_gas_tools.py
"""

import unreal
import toolset_registry
import vigil_pie_common as common


def _ability_system_library():
    """UAbilitySystemBlueprintLibrary, under whatever name Python sees it by.

    It declares meta=(ScriptName="AbilitySystemLibrary")
    (AbilitySystemBlueprintLibrary.h:68), so unreal.AbilitySystemBlueprintLibrary
    does not exist and never did. Both call sites in _get_asc used that
    imaginary name inside a bare `except Exception: pass`, so every lookup
    raised AttributeError, was swallowed, and fell through to the "No
    AbilitySystemComponent found" return -- for both the actor and the
    PlayerState path. This whole toolset reported "no ASC" for every actor
    in the project and never said why.

    resolve_class raises a LookupError naming the candidates tried and the
    near-misses found, so the next rename is diagnosable from the error text
    without an editor session -- and, more importantly, an API regression can
    never again masquerade as "this actor has no ability system".
    """
    return common.resolve_class(
        ["AbilitySystemLibrary", "AbilitySystemBlueprintLibrary"],
        "resolving an actor's AbilitySystemComponent",
        ("AbilitySystem",))


@unreal.uclass()
class VigGASTools(unreal.ToolsetDefinition):
    """Query Gameplay Ability System state on any actor in the level:
    attribute values, active gameplay effects, granted abilities,
    and cooldown status. Built for Vigil's ASC architecture where
    player ASC lives on PlayerState and enemy ASC lives on the character."""

    @toolset_registry.tool_call
    @staticmethod
    def dump_attributes(actor_label: str) -> dict:
        """Read all GothicAttributeSet values from an actor.

        Args:
            actor_label: Label of the actor to inspect.

        Returns:
            Dictionary with every attribute name and its current value.
        """
        actor = VigGASTools._find_actor(actor_label)
        if isinstance(actor, dict):
            return actor  # error dict

        asc = VigGASTools._get_asc(actor)
        if isinstance(asc, dict):
            return asc

        # Read Vigil's known attributes
        vigil_attributes = [
            "Health", "MaxHealth",
            "Stamina", "MaxStamina",
            "Ether", "MaxEther",
            "SuperMeter", "MaxSuperMeter",
            "Selah",
            "AttackPower", "Defense",
            "IncomingDamage",
        ]

        result = {
            "actor": actor_label,
            "actor_class": actor.get_class().get_name(),
            "attributes": {},
        }

        for attr_name in vigil_attributes:
            try:
                # Use the gameplay attribute accessor pattern
                val = asc.get_gameplay_attribute_value(
                    unreal.GameplayAttribute(
                        attribute_name=attr_name,
                        attribute_owner=None,  # will search
                    )
                )
                result["attributes"][attr_name] = val
            except Exception:
                # Attribute might not exist on this actor's set
                pass

        # Derived convenience values
        attrs = result["attributes"]
        if "Health" in attrs and "MaxHealth" in attrs:
            max_hp = attrs["MaxHealth"]
            if max_hp > 0:
                attrs["_HealthPercent"] = round(
                    attrs["Health"] / max_hp * 100, 1
                )

        if "SuperMeter" in attrs and "MaxSuperMeter" in attrs:
            max_sm = attrs["MaxSuperMeter"]
            if max_sm > 0:
                attrs["_SuperPercent"] = round(
                    attrs["SuperMeter"] / max_sm * 100, 1
                )

        return result

    @toolset_registry.tool_call
    @staticmethod
    def dump_active_effects(actor_label: str) -> dict:
        """List all active gameplay effects on an actor, with
        remaining duration and stack count.

        Args:
            actor_label: Label of the actor to inspect.

        Returns:
            Dictionary with a list of active effects and their state.
        """
        actor = VigGASTools._find_actor(actor_label)
        if isinstance(actor, dict):
            return actor

        asc = VigGASTools._get_asc(actor)
        if isinstance(asc, dict):
            return asc

        result = {
            "actor": actor_label,
            "active_effects": [],
        }

        # Get all active effect handles
        try:
            effects = asc.get_active_effects()
            for effect in effects:
                entry = {
                    "effect_class": effect.get_class().get_name()
                    if effect
                    else "Unknown",
                }
                result["active_effects"].append(entry)
        except Exception as e:
            result["note"] = (
                f"Effect enumeration not fully exposed to Python: {e}. "
                "Use dump_attributes for current values instead."
            )

        return result

    @toolset_registry.tool_call
    @staticmethod
    def dump_granted_abilities(actor_label: str) -> dict:
        """List all abilities granted to an actor's ASC, with their
        activation state, input tags, and slot assignments.

        This is the key diagnostic for the 'DA_GothicAbilitySet must
        reference BP_GA_X variants' gotcha — if you see bare C++ class
        names here instead of BP_ prefixed ones, that's the bug.

        Args:
            actor_label: Label of the actor to inspect.

        Returns:
            Dictionary with granted ability classes and their state.
        """
        actor = VigGASTools._find_actor(actor_label)
        if isinstance(actor, dict):
            return actor

        asc = VigGASTools._get_asc(actor)
        if isinstance(asc, dict):
            return asc

        result = {
            "actor": actor_label,
            "abilities": [],
        }

        try:
            specs = asc.get_activatable_abilities()
            for spec in specs:
                ability = spec.get_ability()
                entry = {
                    "class": ability.get_class().get_name()
                    if ability
                    else "None",
                    "is_active": spec.is_active(),
                    "level": spec.get_level() if hasattr(spec, "get_level") else 1,
                }
                # Flag the BP_ vs bare C++ class gotcha
                class_name = entry["class"]
                if (
                    class_name.startswith("GA_")
                    and not class_name.startswith("BP_GA_")
                    and class_name != "None"
                ):
                    entry["WARNING"] = (
                        "Bare C++ class granted — should be BP_ variant. "
                        "Check DA_GothicAbilitySet entries."
                    )
                result["abilities"].append(entry)
        except Exception as e:
            result["note"] = f"Ability enumeration error: {e}"

        return result

    @toolset_registry.tool_call
    @staticmethod
    def dump_gameplay_tags(actor_label: str) -> dict:
        """Read all gameplay tags currently on an actor's ASC.
        Useful for checking State.Dead, State.Attacking, State.Stunned, etc.

        Args:
            actor_label: Label of the actor to inspect.

        Returns:
            Dictionary with a list of tag strings currently active.
        """
        actor = VigGASTools._find_actor(actor_label)
        if isinstance(actor, dict):
            return actor

        asc = VigGASTools._get_asc(actor)
        if isinstance(asc, dict):
            return asc

        result = {
            "actor": actor_label,
            "tags": [],
        }

        try:
            tag_container = asc.get_owned_gameplay_tags()
            # Convert tag container to list of strings
            for i in range(tag_container.num()):
                tag = tag_container.get_gameplay_tag_at_index(i)
                result["tags"].append(str(tag))
        except Exception:
            try:
                # Fallback: try to check specific known tags
                known_tags = [
                    "State.Dead",
                    "State.Attacking",
                    "State.Stunned",
                    "State.Dodging",
                    "State.InCombat",
                    "State.PhaseTransition",
                ]
                for tag_str in known_tags:
                    tag = unreal.GameplayTag.request_gameplay_tag(tag_str)
                    if asc.has_matching_gameplay_tag(tag):
                        result["tags"].append(tag_str)
            except Exception as e:
                result["note"] = f"Tag enumeration error: {e}"

        return result

    # -- Internal helpers (not exposed as tools) --

    @staticmethod
    def _find_actor(actor_label: str):
        """Find an actor by label. Returns the actor or an error dict."""
        world = unreal.get_editor_subsystem(
            unreal.UnrealEditorSubsystem
        ).get_editor_world()
        if not world:
            return {"error": "No editor world"}

        all_actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.Actor
        )
        for actor in all_actors:
            if actor.get_actor_label() == actor_label:
                return actor

        return {"error": f"Actor '{actor_label}' not found in level"}

    @staticmethod
    def _get_asc(actor):
        """Get the AbilitySystemComponent from an actor.
        Checks the actor directly first, then tries PlayerState
        (for player characters where ASC lives on PS)."""
        lib = _ability_system_library()

        asc = lib.get_ability_system_component(actor)
        if asc:
            return asc

        # For player characters, ASC is on PlayerState.
        #
        # Deliberately NOT wrapped in try/except. A bare `except Exception:
        # pass` here is what hid a dead class name through every use of this
        # toolset -- the failure was an AttributeError on the library lookup,
        # which is a code defect, not an absent component. Let it raise. The
        # only thing that legitimately means "this actor has no ASC" is the
        # library returning None, which is handled below.
        # Resolved through common.player_state, NOT hasattr(actor,
        # "get_player_state"): APawn::GetPlayerState is a non-UFUNCTION
        # template and is absent from the bindings on every pawn, so the old
        # hasattr guard was never true and this fallback never ran.
        ps = common.player_state(actor)
        if ps:
            asc = lib.get_ability_system_component(ps)
            if asc:
                return asc

        return {
            "actor": actor.get_actor_label(),
            "error": "No AbilitySystemComponent found "
                     "(checked actor and PlayerState)",
        }
