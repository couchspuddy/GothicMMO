"""
Vigil AnimMontage Notify Editor

WHY THIS EXISTS
---------------
Melee hitbox timing was unreadable through MCP by two independent mechanisms,
which is why it stayed unanswerable:

1. UAnimSequenceBase::Notifies is UPROPERTY(BlueprintReadOnly), and the MCP
   schema generator drops it -- ObjectTools.list_properties on Melee_A_Montage
   does not mention `notifies` at all and get_properties hard-errors on it.
2. Notifies is also PROTECTED, and Python's get_editor_property enforces C++
   access specifiers, so going around the schema generator does not help
   either: it answers "Property 'Notifies' ... is protected and cannot be
   read".

The route that works is UAnimationBlueprintLibrary
(Editor/AnimationBlueprintLibrary/Public/AnimationBlueprintLibrary.h), an
editor-only BlueprintFunctionLibrary whose UFUNCTIONs read and mutate the
array on our behalf. Everything here goes through it.

TIME IS NOT A READABLE FIELD EITHER
-----------------------------------
FAnimNotifyEvent has no TriggerTime property. It inherits FAnimLinkableElement
and stores {LinkMethod, LinkValue, SegmentBeginTime, SegmentLength}, resolved
by FAnimLinkableElement::GetTime(). Those fields are plain UPROPERTY() with no
Blueprint flags, so Python cannot read them -- only SlotIndex and LinkMethod,
which are BlueprintReadOnly, come back. Duration and TrackIndex are in the same
boat.

So the times reported here come from GetAnimNotifyEventTriggerTime and
GetAnimNotifyEventDuration (:318, :322), and track membership from
GetAnimationNotifyEventsForTrack (:330), not from arithmetic on struct fields.
"""

import unreal
import toolset_registry

import vig_asset_common as common

_MONTAGE_CLASS_PATH = ("/Script/Engine", "AnimMontage")

# EAnimLinkMethod, resolved by name so a build that renames the enum fails
# loudly at call time instead of computing times off a wrong constant.
_LINK_ABSOLUTE = "ABSOLUTE"
_LINK_RELATIVE = "RELATIVE"
_LINK_PROPORTIONAL = "PROPORTIONAL"


def _montage(path_or_name):
    return common.load_asset(path_or_name,
                             expected_class=unreal.AnimMontage,
                             class_path=_MONTAGE_CLASS_PATH)


def _notifies(montage):
    """The Notifies array. Raises rather than returning [] when unreachable.

    NOT get_editor_property('notifies'): UAnimSequenceBase::Notifies is a
    PROTECTED UPROPERTY, and Python enforces C++ access specifiers --
    get_editor_property answers "Property 'Notifies' ... is protected and
    cannot be read". That is a second, independent reason this data was
    unreachable, on top of the MCP schema generator dropping BlueprintReadOnly
    properties.

    The way in is UAnimationBlueprintLibrary::GetAnimationNotifyEvents, a
    BlueprintPure UFUNCTION (AnimationBlueprintLibrary.h) that hands back a
    copy of the array. Being a copy, it is read-only in effect: mutations must
    go back through the library's Add/Remove functions.

    An empty array is a legitimate answer (a montage with no notifies), so a
    failed read raises instead, keeping "could not read" distinct from
    "genuinely none".
    """
    try:
        value = unreal.AnimationLibrary.get_animation_notify_events(montage)
    except Exception as exc:
        raise common.VigUnreflected(
            "AnimationLibrary.get_animation_notify_events failed on %s (%s: "
            "%s). Notifies itself is a protected UPROPERTY and cannot be read "
            "directly, so there is no fallback -- report this rather than "
            "working around it."
            % (montage.get_name(), type(exc).__name__, exc))
    if value is None:
        raise common.VigUnreflected(
            "Notifies read as None on %s." % montage.get_name())
    return value


def _link_method(event):
    try:
        return str(event.get_editor_property("link_method")).split(".")[-1].upper()
    except Exception:
        return "<unreadable>"


def _resolve_time(event):
    """Absolute montage trigger time and duration for one notify.

    Not computed from FAnimLinkableElement fields: LinkValue, SegmentBeginTime,
    SegmentLength, TriggerTimeOffset and Duration are all plain UPROPERTY()
    with no Blueprint or edit flags, so Python's get_editor_property refuses
    them ("is protected and cannot be read"). Only the BlueprintReadOnly
    members -- SlotIndex, LinkMethod -- come back at all. An earlier version of
    this module tried to reimplement FAnimLinkableElement::GetTime from those
    fields and got null for every notify.

    The reflected way in is UAnimationBlueprintLibrary::GetAnimNotifyEventTriggerTime
    and ::GetAnimNotifyEventDuration (AnimationBlueprintLibrary.h:318,322),
    which take the struct and do the resolution in C++.
    """
    raw = {"link_method": _link_method(event)}
    for field in ("slot_index",):
        try:
            raw[field] = event.get_editor_property(field)
        except Exception as exc:
            raw[field] = "<unreadable: %s>" % type(exc).__name__

    try:
        trigger = round(float(
            unreal.AnimationLibrary.get_anim_notify_event_trigger_time(event)), 4)
    except Exception as exc:
        raise common.VigUnreflected(
            "AnimationLibrary.get_anim_notify_event_trigger_time failed (%s: "
            "%s). There is no fallback: FAnimNotifyEvent's timing fields are "
            "not Blueprint-visible and cannot be read from Python directly."
            % (type(exc).__name__, exc))

    try:
        duration = round(float(
            unreal.AnimationLibrary.get_anim_notify_event_duration(event)), 4)
    except Exception as exc:
        raise common.VigUnreflected(
            "AnimationLibrary.get_anim_notify_event_duration failed (%s: %s)."
            % (type(exc).__name__, exc))

    return trigger, duration, raw


def _track_names(montage):
    """Notify track names, or None when the helper is not reflected here."""
    try:
        return [str(n) for n in
                unreal.AnimationLibrary.get_animation_notify_track_names(montage)]
    except Exception:
        return None


def _track_lookup(montage, track_names):
    """{(notify_name, trigger_time): track_name}.

    FAnimNotifyEvent::TrackIndex is another non-Blueprint UPROPERTY and reads
    as unavailable, so track membership is recovered the only reflected way
    there is: ask each track for its events
    (UAnimationBlueprintLibrary::GetAnimationNotifyEventsForTrack,
    AnimationBlueprintLibrary.h:330) and index them by name and trigger time.
    """
    lookup = {}
    if not track_names:
        return lookup
    for name in track_names:
        try:
            events = unreal.AnimationLibrary.get_animation_notify_events_for_track(
                montage, name)
        except Exception:
            return lookup
        for event in events:
            try:
                key = (common.try_name(event, "notify_name"),
                       round(float(unreal.AnimationLibrary
                                   .get_anim_notify_event_trigger_time(event)), 4))
            except Exception:
                continue
            lookup[key] = name
    return lookup


def _describe_event(event, index, track_lookup):
    notify = None
    state = None
    try:
        notify = event.get_editor_property("notify")
    except Exception:
        notify = None
    try:
        state = event.get_editor_property("notify_state_class")
    except Exception:
        state = None

    time, duration, raw = _resolve_time(event)
    name = common.try_name(event, "notify_name")

    return {
        "index": index,
        "notify_name": name,
        "kind": "notify_state" if state else ("notify" if notify else "empty"),
        "class": (state or notify).get_class().get_name()
                 if (state or notify) else None,
        "trigger_time": time,
        "duration": duration,
        "end_time": round(time + duration, 4),
        "track_name": (track_lookup or {}).get((name, time)),
        "raw": raw,
    }


@unreal.uclass()
class VigAnimNotifyTools(unreal.ToolsetDefinition):
    """Read and edit AnimMontage notifies -- the data ObjectTools cannot see.

    Notifies is BlueprintReadOnly and therefore absent from the MCP property
    schema; these tools read it through get_editor_property instead. Covers
    listing notifies with resolved trigger times and durations, and adding /
    removing notify states such as AnimNotifyState_MeleeHitbox."""

    @toolset_registry.tool_call
    @staticmethod
    def list_montage_notifies(montage: str) -> str:
        """List every notify and notify state on a montage, with timings.

        Args:
            montage: Asset name ('Melee_A_Montage') or full path
                ('/Game/ParagonKhaimera/.../Melee_A_Montage').

        Returns:
            JSON array, one row per notify: class name, resolved trigger time,
            duration, end time, notify track, and the raw FAnimLinkableElement
            fields the time was computed from. Notify states have a non-zero
            duration; plain notifies are instantaneous.
        """
        asset = _montage(montage)
        events = _notifies(asset)
        tracks = _track_lookup(asset, _track_names(asset))

        rows = [_describe_event(event, index, tracks)
                for index, event in enumerate(events)]

        return common.as_json({
            "asset": asset.get_path_name(),
            "sequence_length": common.try_float(asset, "sequence_length"),
            "rate_scale": common.try_float(asset, "rate_scale"),
            "track_names": _track_names(asset),
            "count": len(rows),
            "notifies": rows,
        })

    @toolset_registry.tool_call
    @staticmethod
    def add_montage_notify_state(montage: str, notify_state_class: str,
                                 start_time: float, duration: float,
                                 track_name: str = "1",
                                 save: bool = True) -> str:
        """Add a notify state to a montage. MUTATES THE ASSET.

        Args:
            montage: Asset name or full path.
            notify_state_class: Class name, e.g. 'AnimNotifyState_MeleeHitbox',
                or a full class path '/Script/GothicMMO.AnimNotifyState_MeleeHitbox'.
            start_time: Trigger time in seconds, montage-absolute.
            duration: Length of the state window in seconds. Must be > 0.
            track_name: Notify track to place it on. Montage tracks are named
                '1', '2', ... by default.
            save: Mark dirty and write the asset.

        Returns:
            JSON with the added notify re-read from the montage after save, so
            the reported time is what the asset holds, not what was requested.
        """
        if duration <= 0.0:
            raise ValueError(
                "duration must be > 0 for a notify STATE (%s given). A "
                "zero-length window never fires." % duration)

        asset = _montage(montage)
        state_class = _resolve_notify_class(notify_state_class)

        before = len(_notifies(asset))
        # UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent is the only
        # reflected add path (BlueprintCallable, AnimationBlueprintLibrary.h).
        # It also creates the track if missing and re-sorts Notifies, which the
        # montage runtime requires -- hand-appending to the array leaves it
        # unsorted and notifies then fire out of order.
        try:
            unreal.AnimationLibrary.add_animation_notify_state_event(
                asset, track_name, start_time, duration, state_class)
        except Exception as exc:
            raise common.VigUnreflected(
                "AnimationLibrary.add_animation_notify_state_event failed on "
                "%s (%s: %s). On some builds UAnimationBlueprintLibrary takes "
                "UAnimSequence rather than UAnimSequenceBase and rejects "
                "montages; if so this tool cannot add notifies and the edit "
                "must happen in the montage editor."
                % (asset.get_name(), type(exc).__name__, exc))

        if save:
            common.save(asset, asset.get_name())
            asset = _montage(montage)

        events = _notifies(asset)
        tracks = _track_lookup(asset, _track_names(asset))
        return common.as_json({
            "asset": asset.get_path_name(),
            "requested": {"class": state_class.get_name(),
                          "start_time": start_time,
                          "duration": duration,
                          "track": track_name},
            "count_before": before,
            "count_after": len(events),
            "saved": bool(save),
            "notifies": [_describe_event(e, i, tracks)
                         for i, e in enumerate(events)],
        })

    @toolset_registry.tool_call
    @staticmethod
    def remove_montage_notifies_by_name(montage: str, notify_name: str,
                                        save: bool = True) -> str:
        """Remove every notify with a given name from a montage. MUTATES THE ASSET.

        By name, not by index, and that is a real limitation: Notifies is a
        protected UPROPERTY so the array cannot be spliced from Python, and the
        only reflected removal path,
        UAnimationBlueprintLibrary::RemoveAnimationNotifyEventsByName, matches
        on name. If a montage carries two hitbox windows sharing one name this
        takes BOTH. Call list_montage_notifies first and check.

        Args:
            montage: Asset name or full path.
            notify_name: NotifyName exactly as list_montage_notifies reports it.
            save: Mark dirty and write the asset.

        Returns:
            JSON with the entries that matched, and the notifies remaining
            after the removal, re-read from the asset.
        """
        asset = _montage(montage)
        events = _notifies(asset)
        tracks = _track_lookup(asset, _track_names(asset))

        matched = [_describe_event(e, i, tracks)
                   for i, e in enumerate(events)
                   if common.try_name(e, "notify_name") == notify_name]
        if not matched:
            raise common.VigNodeNotFound(
                "No notify named '%s' on %s. Present: %s"
                % (notify_name, asset.get_name(),
                   sorted({common.try_name(e, "notify_name") for e in events})))

        try:
            unreal.AnimationLibrary.remove_animation_notify_events_by_name(
                asset, notify_name)
        except Exception as exc:
            raise common.VigUnreflected(
                "AnimationLibrary.remove_animation_notify_events_by_name "
                "failed on %s (%s: %s)."
                % (asset.get_name(), type(exc).__name__, exc))

        removed = matched
        if save:
            common.save(asset, asset.get_name())
            asset = _montage(montage)

        events = _notifies(asset)
        tracks = _track_lookup(asset, _track_names(asset))
        return common.as_json({
            "asset": asset.get_path_name(),
            "removed": removed,
            "count_after": len(events),
            "saved": bool(save),
            "notifies": [_describe_event(e, i, tracks)
                         for i, e in enumerate(events)],
        })


def _resolve_notify_class(name):
    """Resolve a notify class by short name or full path. Raises on miss."""
    if name.startswith("/"):
        cls = unreal.load_class(None, name)
        if cls is None:
            raise common.VigAssetNotFound("load_class('%s') returned None." % name)
        return cls

    cls = getattr(unreal, name, None)
    if cls is None:
        raise common.VigAssetNotFound(
            "unreal.%s does not exist. Pass a full class path such as "
            "'/Script/GothicMMO.AnimNotifyState_MeleeHitbox'." % name)
    return cls
