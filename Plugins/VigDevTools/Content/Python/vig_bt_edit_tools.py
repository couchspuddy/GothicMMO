"""
Vigil Behavior Tree Node Editor

WHY THIS EXISTS
---------------
aimodule_toolset...BehaviorTreeTools can READ a BT (list_nodes, get_children,
get_node_depths) but has no write path, so every BT tweak in this project has
been a hand edit in the editor. This adds get/set on node, decorator and
service properties, plus the dirty+save+re-read cycle that makes the change
stick.

WHAT IT DELIBERATELY DOES NOT DO
--------------------------------
No structural editing: no reordering a composite's children, no reparenting,
no inserting nodes. This is not laziness, it is how the asset is built.

A UBehaviorTree carries TWO representations: the runtime tree
(RootNode -> UBTCompositeNode::Children, an array of FBTCompositeChild) and the
editor-only UEdGraph in UBehaviorTree::BTGraph. The graph is authoritative:
UBehaviorTreeGraph::UpdateAsset() rebuilds RootNode and the whole Children
array from graph node pin order whenever the graph is loaded or edited. Reorder
the runtime array from Python and the next time anyone opens BT_EnemyCombat the
graph silently rebuilds the old order back -- a change that appears to work,
verifies clean on re-read, and then evaporates. That is worse than not having
the feature.

Property edits are safe under the same mechanism for the opposite reason:
each UBehaviorTreeGraphNode holds a POINTER to the very UBTNode object this
toolset mutates (NodeInstance), so UpdateAsset re-links the same instances and
the property values ride along.

Reordering the Strike Sequence's children therefore remains an editor-side job.
Setting bTrackMovingGoal does not.

Node identifiers are the object names BehaviorTreeTools already returns as the
suffix of its refPaths ("BTTask_MoveTo_0"), so identifiers are interoperable
between the two toolsets in both directions.
"""

import json

import unreal
import toolset_registry

import vig_asset_common as common

_BT_CLASS_PATH = ("/Script/AIModule", "BehaviorTree")


# -----------------------------------------------------------------------------
# Tree walk
# -----------------------------------------------------------------------------

def _bt(path_or_name):
    return common.load_asset(path_or_name,
                             expected_class=unreal.BehaviorTree,
                             class_path=_BT_CLASS_PATH)


def _prop(obj, name, default=None):
    """get_editor_property that returns a default instead of raising.

    Used only for OPTIONAL structural fields (services, decorators) which are
    legitimately absent on some node classes. Never used to paper over a
    property the caller explicitly asked for.
    """
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def _walk(behavior_tree):
    """[(node, role, depth, parent_name)] in BehaviorTreeTools.list_nodes order.

    That order is: root decorators, then DFS emitting composite, its services,
    and for each child its decorators followed by the child itself. Verified
    against BehaviorTreeTools.list_nodes on BT_EnemyCombat -- the two lists
    agree node for node, which is the point: identifiers must line up.

    Reads UBehaviorTree::RootDecorators and RootNode, UBTCompositeNode::Children
    (FBTCompositeChild: ChildComposite, ChildTask, Decorators) and the Services
    array shared by UBTCompositeNode and UBTTaskNode.
    """
    out = []

    for decorator in _prop(behavior_tree, "root_decorators", []) or []:
        if decorator:
            out.append((decorator, "root_decorator", 0, "<tree>"))

    root = _prop(behavior_tree, "root_node")
    if root is None:
        raise common.VigNodeNotFound(
            "%s has no RootNode. Either the asset is empty or "
            "UBehaviorTree::RootNode is not reflected on this build."
            % behavior_tree.get_name())

    _walk_node(root, "node", 1, "<root>", out)
    return out


def _walk_node(node, role, depth, parent_name, out):
    out.append((node, role, depth, parent_name))
    name = node.get_name()

    for service in _prop(node, "services", []) or []:
        if service:
            out.append((service, "service", depth + 1, name))

    for child in _prop(node, "children", []) or []:
        for decorator in _prop(child, "decorators", []) or []:
            if decorator:
                out.append((decorator, "decorator", depth + 1, name))
        sub = _prop(child, "child_composite") or _prop(child, "child_task")
        if sub:
            _walk_node(sub, "node", depth + 1, name, out)


def _find(behavior_tree, node_id):
    """Resolve a node identifier to the UBTNode object. Raises on miss.

    Exact object-name match first, then a unique case-insensitive substring,
    matching how resolve_actor works elsewhere in this plugin. Ambiguity raises
    rather than picking one -- silently editing the wrong MoveTo in a tree with
    four of them is exactly the failure this project keeps paying for.
    """
    entries = _walk(behavior_tree)
    for node, _role, _depth, _parent in entries:
        if node.get_name() == node_id:
            return node

    needle = node_id.lower()
    hits = [n for n, _r, _d, _p in entries if needle in n.get_name().lower()]
    if len(hits) == 1:
        return hits[0]
    if not hits:
        raise common.VigNodeNotFound(
            "No node '%s' in %s. Present: %s"
            % (node_id, behavior_tree.get_name(),
               [n.get_name() for n, _r, _d, _p in entries]))
    raise common.VigNodeNotFound(
        "'%s' is ambiguous in %s, matches: %s"
        % (node_id, behavior_tree.get_name(), [h.get_name() for h in hits]))


def _property_names(obj):
    """Every reflected property on a node, from the generated Python type."""
    return sorted(
        name for name in dir(type(obj))
        if not name.startswith("_")
        and isinstance(getattr(type(obj), name, None), property)
    )


def _apply(node, property_name, parsed):
    """Set one property, handling the AI data-provider structs BT nodes use.

    bTrackMovingGoal is not a bool: on UE 5.6+ UBTTask_MoveTo declares it as an
    FValueOrBBKey_Bool, a struct of {DefaultValue, Key} -- which is why
    ObjectTools.get_properties reports it as {"defaultValue": true, "key":
    "None"}. Handing set_editor_property a bare Python bool fails with a type
    error.

    Those structs have no generated Python type, so hasattr(type(x),
    'default_value') is False and dir() lists nothing; the only way in is to
    call get/set_editor_property on the struct instance and let it fail. Hence
    the try, and hence the failure message enumerating what the struct DID
    answer to.

    A scalar goes into DefaultValue. A dict is applied field by field. Anything
    else is passed straight through, and any failure is raised, not swallowed.
    """
    current = node.get_editor_property(property_name)

    if isinstance(current, unreal.StructBase):
        if isinstance(parsed, dict):
            for field, value in parsed.items():
                current.set_editor_property(field, value)
        else:
            try:
                current.set_editor_property("default_value", parsed)
            except Exception as exc:
                raise TypeError(
                    "%s is a %s struct and would not take %r as DefaultValue "
                    "(%s: %s). Readable fields: %s. Pass a JSON object naming "
                    "the fields to set."
                    % (property_name, type(current).__name__, parsed,
                       type(exc).__name__, exc,
                       common._struct_fields(current) or "<none>"))
        node.set_editor_property(property_name, current)
    else:
        node.set_editor_property(property_name, parsed)


@unreal.uclass()
class VigBTEditTools(unreal.ToolsetDefinition):
    """Read and WRITE Behavior Tree node properties, then save the asset.

    Covers tasks, decorators and services by node identifier (the same
    identifiers BehaviorTreeTools.list_nodes returns). Structural editing --
    reordering children, reparenting, inserting nodes -- is deliberately NOT
    supported; the editor graph rebuilds the runtime tree and would revert it.
    See the module docstring."""

    @toolset_registry.tool_call
    @staticmethod
    def list_bt_nodes(behavior_tree: str) -> str:
        """List every node, decorator and service in a Behavior Tree.

        Args:
            behavior_tree: Asset name ('BT_EnemyCombat') or full path
                ('/Game/Blueprints/AI/BT_EnemyCombat').

        Returns:
            JSON array in BehaviorTreeTools.list_nodes order. Each row carries
            the identifier accepted by get_bt_node_properties /
            set_bt_node_property, its class, role (node / decorator / service /
            root_decorator), tree depth, parent, and the node's display name.
        """
        asset = _bt(behavior_tree)
        rows = []
        for node, role, depth, parent in _walk(asset):
            rows.append({
                "id": node.get_name(),
                "class": node.get_class().get_name(),
                "role": role,
                "depth": depth,
                "parent": parent,
                "node_name": str(_prop(node, "node_name", "")),
            })
        return common.as_json({
            "asset": asset.get_path_name(),
            "count": len(rows),
            "nodes": rows,
        })

    @toolset_registry.tool_call
    @staticmethod
    def get_bt_node_properties(behavior_tree: str, node_id: str,
                               properties: str = "") -> str:
        """Read properties off one BT node, decorator or service.

        Args:
            behavior_tree: Asset name or full path.
            node_id: Node identifier from list_bt_nodes, e.g. 'BTTask_MoveTo_0'.
                A unique substring works; ambiguity raises.
            properties: Comma-separated property names. Empty reads every
                reflected property on the node.

        Returns:
            JSON of the node and its property values. Struct-valued properties
            (FAIDataProviderBoolValue, FBlackboardKeySelector) are expanded
            field by field so the DefaultValue / Key split is visible.
        """
        asset = _bt(behavior_tree)
        node = _find(asset, node_id)

        wanted = [p.strip() for p in properties.split(",") if p.strip()]
        if not wanted:
            wanted = _property_names(node)

        values = {}
        for name in wanted:
            try:
                values[name] = common.describe(node.get_editor_property(name))
            except Exception as exc:
                values[name] = {"error": "%s: %s" % (type(exc).__name__, exc)}

        return common.as_json({
            "asset": asset.get_path_name(),
            "node": node.get_name(),
            "class": node.get_class().get_name(),
            "properties": values,
        })

    @toolset_registry.tool_call
    @staticmethod
    def set_bt_node_property(behavior_tree: str, node_id: str,
                             property_name: str, value_json: str,
                             save: bool = True) -> str:
        """Set one property on a BT node, decorator or service. MUTATES THE ASSET.

        Args:
            behavior_tree: Asset name or full path.
            node_id: Node identifier from list_bt_nodes.
            property_name: Reflected property name, e.g. 'bTrackMovingGoal'
                or 'AcceptableRadius'.
            value_json: The new value as JSON. Scalars ('false', '150') are
                routed into a struct's DefaultValue when the property is an
                FAIDataProviderBoolValue/FloatValue. Pass a JSON object
                ('{"default_value": false}') to set struct fields explicitly.
            save: Mark dirty and write the asset. Leave true unless batching.

        Returns:
            JSON with the before value, the after value re-read from the node,
            and the save result. A property that did not actually change is
            reported as changed=false rather than a bare success.
        """
        asset = _bt(behavior_tree)
        node = _find(asset, node_id)

        try:
            parsed = json.loads(value_json)
        except ValueError as exc:
            raise ValueError(
                "value_json is not valid JSON (%s). Quote strings: '\"Foo\"', "
                "not 'Foo'." % exc)

        before = common.describe(node.get_editor_property(property_name))
        _apply(node, property_name, parsed)
        after = common.describe(node.get_editor_property(property_name))

        result = {
            "asset": asset.get_path_name(),
            "node": node.get_name(),
            "property": property_name,
            "before": before,
            "after": after,
            "changed": before != after,
        }
        if save:
            common.save(asset, "%s (%s)" % (asset.get_name(), node.get_name()))
            reloaded = _bt(behavior_tree)
            result["after_save"] = common.describe(
                _find(reloaded, node_id).get_editor_property(property_name))
            result["saved"] = True
        else:
            result["saved"] = False
            result["note"] = ("Not saved. Call again with save=true, or the "
                              "change is lost when the editor closes.")
        return common.as_json(result)

    @toolset_registry.tool_call
    @staticmethod
    def get_bt_composite_children(behavior_tree: str, node_id: str) -> str:
        """List a composite's children in execution order, with their decorators.

        Read-only orientation call for the Strike Sequence question: it shows
        which child is gated behind which decorator and in what order they run.
        There is no matching setter -- see the module docstring on why
        reordering has to happen in the BT editor.

        Args:
            behavior_tree: Asset name or full path.
            node_id: A composite node id, e.g. 'BTComposite_Sequence_3'.

        Returns:
            JSON array of children in order, each with its decorators.
        """
        asset = _bt(behavior_tree)
        node = _find(asset, node_id)

        children = _prop(node, "children")
        if children is None:
            raise common.VigNodeNotFound(
                "%s (%s) is not a composite -- it has no Children array."
                % (node.get_name(), node.get_class().get_name()))

        rows = []
        for index, child in enumerate(children):
            sub = _prop(child, "child_composite") or _prop(child, "child_task")
            rows.append({
                "index": index,
                "id": sub.get_name() if sub else None,
                "class": sub.get_class().get_name() if sub else None,
                "decorators": [
                    {"id": d.get_name(), "class": d.get_class().get_name()}
                    for d in (_prop(child, "decorators", []) or []) if d
                ],
            })
        return common.as_json({
            "asset": asset.get_path_name(),
            "composite": node.get_name(),
            "children": rows,
            "reordering": ("Not supported by this toolset. The editor graph "
                           "(UBehaviorTreeGraph::UpdateAsset) rebuilds child "
                           "order from graph pin order and would revert it."),
        })
