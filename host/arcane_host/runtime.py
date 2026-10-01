"""GLib scheduling and deterministic daemon lifecycle ownership."""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path
from typing import Any, Callable

from .adapters import SemanticAdapters
from .dbus_contract import OWNER_LABEL_MAX
from .focus import FocusArbiter
from .heartbeat import HidHeartbeat
from .hid_ownership import OpenWatch, holds_node, node_openers
from .policy import NotificationPolicy
from .protocol import NotificationSummary
from .semantic import SemanticResolver, world_bytes


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
        lend_check: Callable[[Path], bool] | None = None,
        lend_scan_window: float = 5.0,
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
        self._world_listeners: list[Callable[[tuple[int, ...]], None]] = []
        self._owned: list[Any] = []
        self._closed = False
        # Lending to a program that opened the keyboard without the guard (Vial
        # from its own icon). lend_check says which nodes to watch; None is off.
        self.lend_check = lend_check
        self.open_watch: OpenWatch | None = None
        self._checked_device: Any = None
        self._unwatchable: Path | None = None
        self.lent_to: tuple[int, ...] = ()
        self.lent_node: Path | None = None
        # A program that opens the keyboard over and over gets one scan per
        # window; an open inside the window waits for its end.
        self.lend_scan_window = lend_scan_window
        self._scan_pending = False
        self._next_open_scan = 0.0

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

    def world(self) -> tuple[int, ...]:
        """The eight payload bytes the keyboard is being sent, as World reports them."""
        return world_bytes(self.resolver.state)

    def add_world_listener(self, listener: Callable[[tuple[int, ...]], None]) -> None:
        self._world_listeners.append(listener)

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
        self.lent_to = ()
        self.heartbeat.next_connect = 0.0
        print("arcane-host: keyboard returned; reconnecting", file=sys.stderr, flush=True)
        self._publish_status()
        self.wake()

    def _check_openers(self, now: float) -> None:
        """Lend the keyboard to another opener; take it back once all have closed it."""
        if self.lend_check is None:
            return
        if self.lent_to:
            if self.open_watch is not None:
                self.open_watch.opened()
            node = self.lent_node
            self.lent_to = tuple(pid for pid in self.lent_to if holds_node(pid, node))
            if not self.lent_to:
                self.resume()
            return
        device = self.heartbeat.device
        path = getattr(device, "path", None)
        if self.paused or path is None:
            return
        node = Path(os.path.realpath(path))
        if not self.lend_check(node):
            return
        # A new connection is checked once, for a program that opened the node
        # before we did; after that only an open event triggers the scan.
        due = device is not self._checked_device
        self._checked_device = device
        watched = None if self.open_watch is None else self.open_watch.node
        if node not in (watched, self._unwatchable):
            self._close_watch()
            try:
                self.open_watch = OpenWatch(node)
            except OSError as error:
                # Checked at each new connection only, then.
                self._unwatchable = node
                self._debug(f"no open watch on {node} ({error})")
        if self.open_watch is not None and self.open_watch.opened():
            self._scan_pending = True
        if not due and self._scan_pending:
            if now < self._next_open_scan:
                return
            due = True
            self._next_open_scan = now + self.lend_scan_window
        if not due:
            return
        self._scan_pending = False
        openers = node_openers(node)
        if not openers:
            return
        self.lent_node = node
        label = ", ".join(f"{name} (pid {pid})" for pid, name in openers)
        self.pause(label[:OWNER_LABEL_MAX])
        self.lent_to = tuple(pid for pid, _name in openers)

    def _close_watch(self) -> None:
        if self.open_watch is not None:
            self.open_watch.close()
            self.open_watch = None

    def _deadline_delay_ms(self, now: float) -> int:
        if self.adapters is None:
            raise RuntimeError("semantic adapters are not bound")
        # Paused, the heartbeat's reconnect deadline is always due; it must not
        # turn into a 1 ms spin.
        deadlines = [now + 1.0] if self.paused else [self.heartbeat.next_deadline(now), now + 1.0]
        if self._scan_pending and not self.paused:
            deadlines.append(self._next_open_scan)
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
                world = self.world()
                for listener in tuple(self._world_listeners):
                    listener(world)
            sent = False if self.paused else self.heartbeat.tick(now)
            self._check_openers(now)
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
        self._close_watch()
        self.heartbeat.close()
