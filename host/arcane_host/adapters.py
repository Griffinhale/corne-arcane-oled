"""Pure privacy-bounded semantic state adapters."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import secrets
import time
from typing import Callable

from .dbus_contract import RepositoryState
from .policy import NotificationPolicy
from .protocol import Category, Priority
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
        changed = self.resolver.update(pomodoro=bool(active))
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
        if not self._pomodoro_active or self._pomodoro_deadline is None:
            return None
        if not self._pomodoro_warning:
            warning = self._pomodoro_deadline - 60.0
            if warning > now:
                return warning
        return max(now, self._pomodoro_deadline)

    def poll(self, now: float) -> bool:
        """Advance deadline-only semantics without introducing a polling loop."""
        if not self._pomodoro_active or self._pomodoro_deadline is None:
            return False
        before = (self.resolver.state.revision, self.counters.events)
        remaining = self._pomodoro_deadline - now
        if remaining <= 0:
            self.pomodoro(False, 0)
        elif remaining <= 60 and not self._pomodoro_warning:
            self.pomodoro(True, remaining)
        return before != (self.resolver.state.revision, self.counters.events)

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
            RepositoryState.COMPLETION: (
                Priority.NORMAL if success else Priority.CRITICAL
            ),
        }[state]
        changed = self.policy.inject(
            Category.TRANSFER,
            priority,
            False,
            self.clock(),
            key="repository-state",
            replacement=True,
        )
        secondary_changed = self.resolver.update(
            transfer_active=state == RepositoryState.OPERATION
        )
        if changed or secondary_changed:
            self._changed(changed)
        return changed
