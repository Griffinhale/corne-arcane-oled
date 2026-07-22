"""Pure privacy-bounded semantic state adapters."""

from __future__ import annotations

import hashlib
import secrets
import time
from dataclasses import dataclass
from typing import Callable

from .dbus_contract import RepositoryState
from .policy import NotificationPolicy
from .protocol import Category, Intensity, Priority, Secondary
from .semantic import SemanticResolver


@dataclass(slots=True)
class AdapterCounters:
    updates: int = 0
    errors: int = 0
    events: int = 0


class SemanticAdapters:
    """Pure state transitions used by D-Bus monitors and unit tests."""

    def __init__(
        self,
        resolver: SemanticResolver,
        policy: NotificationPolicy,
        changed: Callable[[], None],
        clock: Callable[[], float] = time.monotonic,
        token_key: bytes | None = None,
        pomodoro_duration: float = 1500.0,
    ) -> None:
        self.resolver = resolver
        self.policy = policy
        self.changed = changed
        self.clock = clock
        self._token_key = token_key or secrets.token_bytes(16)
        self.counters = AdapterCounters()
        self._track_token: bytes | None = None
        self._pomodoro_warning = False
        self._pomodoro_active = False
        self._pomodoro_deadline: float | None = None
        self._pomodoro_duration = max(1.0, pomodoro_duration)
        self._pomodoro_stage = Intensity.CALM
        self._browser_pending: tuple[Secondary, Intensity] | None = None
        self._browser_expiry: float | None = None
        self._browser_next_emit = 0.0
        self._network_offline = False
        self._network_status: str | None = None
        self._vpn = False

    def _changed(self, policy_changed: bool = False) -> None:
        self.counters.updates += 1
        if policy_changed:
            self.counters.events += 1
        self.changed()

    def media(self, playback_status: str, track_id: str | None = None) -> None:
        playing = playback_status.lower() == "playing"
        state_changed = self.resolver.update(media_playing=playing)
        event_changed = False
        if track_id:
            token = hashlib.blake2s(
                track_id.encode("utf-8", "surrogatepass"),
                key=self._token_key,
                digest_size=8,
            ).digest()
            if token != self._track_token:
                self._track_token = token
                event_changed = self.policy.inject(
                    Category.OTHER, Priority.LOW, False, self.clock(), key=("mpris", token)
                )
        if state_changed or event_changed:
            self._changed(event_changed)

    def dnd(self, inhibited: bool) -> None:
        if self.resolver.update(dnd=bool(inhibited)):
            self._changed()

    def pomodoro(
        self,
        active: bool,
        remaining_seconds: float | None = None,
        failed: bool = False,
    ) -> None:
        now = self.clock()
        if active and remaining_seconds is not None:
            self._pomodoro_deadline = now + max(0.0, remaining_seconds)
            elapsed = max(
                0.0, min(self._pomodoro_duration, self._pomodoro_duration - remaining_seconds)
            )
            self._pomodoro_stage = Intensity(min(3, int(elapsed * 4.0 / self._pomodoro_duration)))
        elif not active:
            self._pomodoro_stage = Intensity.CALM
        changed = self.resolver.update(pomodoro=bool(active), pomodoro_stage=self._pomodoro_stage)
        event_changed = False
        if failed:
            event_changed = self.policy.inject(
                Category.CALENDAR, Priority.CRITICAL, False, now, key="pomodoro-failed"
            )
            self._pomodoro_warning = False
            self._pomodoro_deadline = None
        elif active and remaining_seconds is not None and remaining_seconds <= 60:
            if not self._pomodoro_warning:
                event_changed = self.policy.inject(
                    Category.CALENDAR, Priority.NORMAL, False, now, key="pomodoro-warning"
                )
            self._pomodoro_warning = True
        elif self._pomodoro_active and not active:
            event_changed = self.policy.inject(
                Category.CALENDAR, Priority.NORMAL, False, now, key="pomodoro-complete"
            )
            self._pomodoro_warning = False
            self._pomodoro_deadline = None
        elif not active:
            self._pomodoro_deadline = None
        self._pomodoro_active = bool(active)
        if changed or event_changed:
            self._changed(event_changed)

    def next_deadline(self, now: float) -> float | None:
        deadlines: list[float] = []
        if self._pomodoro_active and self._pomodoro_deadline is not None:
            if not self._pomodoro_warning:
                warning = self._pomodoro_deadline - 60.0
                if warning > now:
                    deadlines.append(warning)
            start = self._pomodoro_deadline - self._pomodoro_duration
            next_stage = int(self._pomodoro_stage) + 1
            if next_stage < 4:
                deadlines.append(max(now, start + self._pomodoro_duration * next_stage / 4.0))
            deadlines.append(max(now, self._pomodoro_deadline))
        if self._browser_pending is not None:
            deadlines.append(max(now, self._browser_next_emit))
        if self._browser_expiry is not None:
            deadlines.append(max(now, self._browser_expiry))
        return min(deadlines) if deadlines else None

    def poll(self, now: float) -> bool:
        """Advance deadline-only semantics without introducing a polling loop."""
        before = (self.resolver.state.revision, self.counters.events)
        if self._browser_expiry is not None and now >= self._browser_expiry:
            self._browser_pending = None
            self._browser_expiry = None
            if self.resolver.update(
                browser_activity=Secondary.NONE, browser_intensity=Intensity.CALM
            ):
                self._changed()
        elif self._browser_pending is not None and now >= self._browser_next_emit:
            kind, intensity = self._browser_pending
            self._browser_pending = None
            self._browser_next_emit = now + 0.25
            if self.resolver.update(browser_activity=kind, browser_intensity=intensity):
                self._changed()
        if self._pomodoro_active and self._pomodoro_deadline is not None:
            remaining = self._pomodoro_deadline - now
            if remaining <= 0:
                self.pomodoro(False, 0)
            else:
                elapsed = max(
                    0.0, min(self._pomodoro_duration, self._pomodoro_duration - remaining)
                )
                stage = Intensity(min(3, int(elapsed * 4.0 / self._pomodoro_duration)))
                if stage != self._pomodoro_stage or (
                    remaining <= 60 and not self._pomodoro_warning
                ):
                    self.pomodoro(True, remaining)
        return before != (self.resolver.state.revision, self.counters.events)

    def browser(self, kind: Secondary, intensity: Intensity) -> None:
        if kind not in {Secondary.SCROLL, Secondary.TAB, Secondary.PAGE}:
            raise ValueError("invalid browser activity kind")
        if not 0 <= int(intensity) <= 3:
            raise ValueError("invalid browser activity intensity")
        now = self.clock()
        self._browser_expiry = now + 1.5
        if now >= self._browser_next_emit:
            self._browser_pending = None
            self._browser_next_emit = now + 0.25
            if self.resolver.update(browser_activity=kind, browser_intensity=intensity):
                self._changed()
        else:
            self._browser_pending = (kind, intensity)
            # Wake the runtime so it can reschedule for the coalescing boundary.
            self.changed()

    def network(self, connectivity: str, vpn: bool = False) -> None:
        now = self.clock()
        status = connectivity.lower()
        event_changed = False
        status_changed = status != self._network_status
        vpn_changed = bool(vpn) != self._vpn
        if status == "offline" and status_changed:
            event_changed = self.policy.inject(
                Category.SYSTEM,
                Priority.CRITICAL,
                True,
                now,
                key="network-offline",
                replacement=self._network_offline,
            )
            self._network_offline = True
        elif status != "offline" and self._network_offline:
            event_changed = self.policy.close("network-offline")
            self._network_offline = False
        if status == "limited" and status_changed:
            event_changed |= self.policy.inject(
                Category.SYSTEM, Priority.NORMAL, False, now, key="network-limited"
            )
        if vpn and vpn_changed:
            event_changed |= self.policy.inject(
                Category.SECURITY, Priority.NORMAL, False, now, key="network-vpn"
            )
        self._network_status = status
        self._vpn = bool(vpn)
        secondary_changed = self.resolver.update(system_alert=status in {"offline", "limited"})
        if event_changed or secondary_changed:
            self._changed(event_changed)

    def repository(self, state: RepositoryState, success: bool) -> bool:
        priority = {
            RepositoryState.CLEAN: Priority.LOW,
            RepositoryState.DIRTY: Priority.NORMAL,
            RepositoryState.OPERATION: Priority.NORMAL,
            RepositoryState.COMPLETION: (Priority.NORMAL if success else Priority.CRITICAL),
        }[state]
        changed = self.policy.inject(
            Category.TRANSFER,
            priority,
            False,
            self.clock(),
            key="repository-state",
            replacement=True,
        )
        secondary_changed = self.resolver.update(transfer_active=state == RepositoryState.OPERATION)
        if changed or secondary_changed:
            self._changed(changed)
        return changed
