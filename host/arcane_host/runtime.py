"""GLib scheduling and deterministic daemon lifecycle ownership."""

from __future__ import annotations

import sys
import time
from typing import Any, Callable

from .adapters import SemanticAdapters
from .focus import FocusArbiter
from .heartbeat import HidHeartbeat
from .policy import NotificationPolicy
from .protocol import NotificationSummary
from .semantic import SemanticResolver


class DaemonRuntime:
    """Own scheduling, semantic dispatch, subscriptions, and shutdown."""

    def __init__(
        self,
        Gio,
        GLib,
        loop,
        heartbeat: HidHeartbeat,
        resolver: SemanticResolver,
        policy: NotificationPolicy,
        arbiter: FocusArbiter,
        *,
        fixed_summary: NotificationSummary | None = None,
        focus_override: bool = False,
        once: bool = False,
        verbose: bool = False,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self.Gio = Gio
        self.GLib = GLib
        self.loop = loop
        self.heartbeat = heartbeat
        self.resolver = resolver
        self.policy = policy
        self.arbiter = arbiter
        self.fixed_summary = fixed_summary
        self.focus_override = focus_override
        self.once = once
        self.verbose = verbose
        self.clock = clock
        self.adapters: SemanticAdapters | None = None
        self.source_id = 0
        self.last_revision = resolver.state.revision
        self.in_tick = False
        self.wake_pending = False
        self.owner_id = 0
        self.paused_by = ""
        self._status_listeners: list[Callable[[tuple[str, str, bool, str]], None]] = []
        self._last_status: tuple[str, str, bool, str] | None = None
        self._owned: list[Any] = []
        self._closed = False

    def bind_adapters(self, adapters: SemanticAdapters) -> None:
        self.adapters = adapters

    def own(self, resource: Any) -> Any:
        self._owned.append(resource)
        return resource

    def _debug(self, message: str) -> None:
        if self.verbose:
            print(f"arcane-host: {message}", file=sys.stderr, flush=True)

    def set_bus_owner(self, owner_id: int) -> None:
        self.owner_id = owner_id

    @property
    def paused(self) -> bool:
        return bool(self.paused_by)

    def status(self) -> tuple[str, str, bool, str]:
        """(link, device, paused, owner), as the Control interface reports it."""
        if self.paused:
            return ("paused", "", True, self.paused_by)
        device = self.heartbeat.device
        path = "" if device is None else str(getattr(device, "path", ""))
        return (self.heartbeat.link or "starting", path, False, "")

    def add_status_listener(self, listener: Callable[[tuple[str, str, bool, str]], None]) -> None:
        self._status_listeners.append(listener)

    def _publish_status(self) -> None:
        status = self.status()
        if status == self._last_status:
            return
        self._last_status = status
        for listener in tuple(self._status_listeners):
            listener(status)

    def pause(self, owner: str) -> None:
        """Close the keyboard now and keep it closed until resume()."""
        self.paused_by = owner or "unnamed client"
        self.heartbeat.close()
        # Forget the link, so the reconnect after resume() is logged again.
        self.heartbeat.link = None
        print(f"arcane-host: keyboard lent to {self.paused_by}", file=sys.stderr, flush=True)
        self._publish_status()

    def resume(self) -> None:
        if not self.paused:
            return
        self.paused_by = ""
        self.heartbeat.next_connect = 0.0
        print("arcane-host: keyboard returned; reconnecting", file=sys.stderr, flush=True)
        self._publish_status()
        self.wake()

    def _deadline_delay_ms(self, now: float) -> int:
        if self.adapters is None:
            raise RuntimeError("semantic adapters are not bound")
        # Paused, the heartbeat's reconnect deadline is always due; it must not
        # turn into a 1 ms spin.
        deadlines = [now + 1.0] if self.paused else [self.heartbeat.next_deadline(now), now + 1.0]
        focus_deadline = None if self.focus_override else self.arbiter.next_deadline()
        policy_deadline = None if self.fixed_summary is not None else self.policy.next_deadline(now)
        adapter_deadline = self.adapters.next_deadline(now)
        deadlines.extend(
            deadline
            for deadline in (focus_deadline, policy_deadline, adapter_deadline)
            if deadline is not None
        )
        return max(1, min(1000, int(max(0.0, min(deadlines) - now) * 1000)))

    def tick(self) -> bool:
        if self.adapters is None:
            raise RuntimeError("semantic adapters are not bound")
        self.source_id = 0
        self.in_tick = True
        now = self.clock()
        try:
            self.adapters.poll(now)
            if not self.focus_override:
                self.arbiter.poll(now)
            summary = self.fixed_summary or self.policy.summary(now)
            self.resolver.update(
                summary=summary,
                focus_scene=self.arbiter.scene,
                focus_floor=self.arbiter.floor,
                focus_matched=self.arbiter.matched,
            )
            if self.resolver.state.revision != self.last_revision:
                self.last_revision = self.resolver.state.revision
                self.heartbeat.request_notify()
            sent = False if self.paused else self.heartbeat.tick(now)
            self._publish_status()
            if sent and self.once:
                self.loop.quit()
                return False
            delay_ms = 1 if self.wake_pending else self._deadline_delay_ms(now)
            self.wake_pending = False
            self.source_id = self.GLib.timeout_add(delay_ms, self.tick)
            return False
        finally:
            self.in_tick = False

    def wake(self) -> None:
        if self._closed:
            return
        if self.in_tick:
            self.wake_pending = True
            return
        if self.source_id:
            try:
                self.GLib.source_remove(self.source_id)
            except Exception as error:
                self._debug(f"removing tick source failed ({error})")
        self.source_id = self.GLib.idle_add(self.tick)

    def run(self) -> None:
        self.wake()
        try:
            self.loop.run()
        except KeyboardInterrupt:
            if self.verbose:
                print("arcane-host: stopped; firmware context expires within 1.5 s")
        finally:
            self.close()

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self.source_id:
            try:
                self.GLib.source_remove(self.source_id)
            except Exception as error:
                self._debug(f"removing tick source failed ({error})")
            self.source_id = 0
        for resource in reversed(self._owned):
            close = getattr(resource, "close", None)
            if close is not None:
                try:
                    close()
                except Exception as error:
                    self._debug(f"closing {type(resource).__name__} failed ({error})")
        self._owned.clear()
        if self.owner_id:
            self.Gio.bus_unown_name(self.owner_id)
            self.owner_id = 0
        self.heartbeat.close()
