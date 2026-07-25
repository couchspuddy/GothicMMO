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
    comp = common.component(actor, "GothicCombatStateComponent")
    if comp is None:
        raise LookupError("no GothicCombatStateComponent")
    return bool(comp.is_in_combat())


@_reader("pack_id")
def _m_pack_id(actor):
    # NAME_None here is the diagnostic for the PackSubsystem registration gap.
    return str(actor.get_pack_id())


@_reader("combat_target")
def _m_combat_target(actor):
    target = actor.get_combat_target()
    return target.get_name() if common.is_valid(target) else None


@_reader("location")
def _m_location(actor):
    return common.vec(actor.get_actor_location())


@_reader("speed")
def _m_speed(actor):
    v = actor.get_velocity()
    return round((v.x ** 2 + v.y ** 2 + v.z ** 2) ** 0.5, 1)


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
