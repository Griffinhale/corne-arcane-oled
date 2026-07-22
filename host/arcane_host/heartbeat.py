"""Reconnectable Raw HID heartbeat transport and state machine."""

from __future__ import annotations

import time
from typing import Callable, Protocol

from .protocol import (
    DEFAULT_CIVIC,
    EMPTY_SUMMARY,
    Category,
    CivicState,
    Message,
    NotificationSummary,
    Priority,
    Scene,
    build_packet,
)


class HidTransport(Protocol):
    """Minimal bidirectional transport required by host protocol clients."""

    def send(self, report: bytes) -> None: ...

    def receive(self, timeout: float) -> bytes: ...

    def close(self) -> None: ...


class DryRunTransport:
    """Print reports and echo them back without opening a HID device."""

    def __init__(self) -> None:
        self.last_report: bytes | None = None

    def send(self, report: bytes) -> None:
        self.last_report = report
        print(report.hex(), flush=True)

    def receive(self, timeout: float) -> bytes:
        del timeout
        if self.last_report is None:
            raise RuntimeError("dry-run receive called before send")
        return self.last_report

    def close(self) -> None:
        pass


class HidHeartbeat:
    """Reconnectable HELLO/heartbeat state machine with injectable I/O."""

    def __init__(
        self,
        scene_provider: Callable[[], Scene],
        device_factory: Callable[[], HidTransport],
        session_factory: Callable[[], int],
        *,
        summary_provider: Callable[[], NotificationSummary] | None = None,
        civic_provider: Callable[[], CivicState] | None = None,
        notification_count: int = 0,
        interval: float = 0.5,
        retry_interval: float = 2.0,
        verbose: bool = False,
    ) -> None:
        self.scene_provider = scene_provider
        self.device_factory = device_factory
        self.session_factory = session_factory
        self.summary_provider = summary_provider or (
            lambda: (
                EMPTY_SUMMARY
                if notification_count == 0
                else NotificationSummary(notification_count, Category.OTHER, Priority.NORMAL)
            )
        )
        self.civic_provider = civic_provider or (lambda: DEFAULT_CIVIC)
        self.interval = interval
        self.retry_interval = retry_interval
        self.verbose = verbose
        self.device: HidTransport | None = None
        self.session = 0
        self.sequence = 0
        self.next_connect = 0.0
        self.next_heartbeat = 0.0
        self.heartbeats = 0
        self.notifications = 0
        self.notify_pending = False

    def _exchange(self, device: HidTransport, report: bytes) -> None:
        device.send(report)
        reply = device.receive(0.25)
        if reply != report:
            raise RuntimeError("Raw HID acknowledgement did not exactly echo request")

    def _log(self, message: str) -> None:
        if self.verbose:
            print(f"arcane-host: {message}", flush=True)

    def _disconnect(self, now: float, error: BaseException | None = None) -> None:
        if self.device is not None:
            try:
                self.device.close()
            except OSError:
                pass
        self.device = None
        self.next_connect = now + self.retry_interval
        if error is not None:
            self._log(f"HID disconnected ({error}); retrying in {self.retry_interval:g}s")

    def _connect(self, now: float) -> None:
        device: HidTransport | None = None
        try:
            device = self.device_factory()
            session = self.session_factory() & 0xFFFFFFFF
            candidate = session or 1
            if candidate == self.session:
                candidate = ((candidate + 1) & 0xFFFFFFFF) or 1
            self.session = candidate
            self.sequence = 0
            self._exchange(
                device,
                build_packet(
                    Message.HELLO,
                    self.session,
                    0,
                    self.scene_provider(),
                    summary=self.summary_provider(),
                    civic=self.civic_provider(),
                ),
            )
        except (OSError, RuntimeError, TimeoutError) as error:
            try:
                if device is not None:
                    device.close()
            except OSError:
                pass
            self.device = None
            self.next_connect = now + self.retry_interval
            self._log(f"HID unavailable ({error}); retrying in {self.retry_interval:g}s")
            return
        self.device = device
        self.next_heartbeat = now + 0.1
        self.notify_pending = False
        self._log(f"connected session=0x{self.session:08x}")

    def tick(self, now: float) -> bool:
        """Advance I/O. Return True exactly when a heartbeat was sent."""
        if self.device is None:
            if now >= self.next_connect:
                self._connect(now)
            return False
        heartbeat_due = now >= self.next_heartbeat
        if not heartbeat_due and not self.notify_pending:
            return False
        self.sequence = (self.sequence + 1) & 0xFFFF
        message = Message.HEARTBEAT if heartbeat_due else Message.NOTIFY
        summary = self.summary_provider()
        try:
            self._exchange(
                self.device,
                build_packet(
                    message,
                    self.session,
                    self.sequence,
                    self.scene_provider(),
                    summary=summary,
                    civic=self.civic_provider(),
                ),
            )
        except (OSError, RuntimeError, TimeoutError) as error:
            self._disconnect(now, error)
            return False
        if message == Message.NOTIFY:
            self.notify_pending = False
            self.notifications += 1
            self._log(
                f"notify seq={self.sequence} category={summary.category.name.lower()} "
                f"priority={summary.priority.name.lower()} count={summary.count}"
            )
            return False
        self.notify_pending = False
        self.next_heartbeat = now + self.interval
        self.heartbeats += 1
        self._log(
            f"heartbeat seq={self.sequence} scene={self.scene_provider().name.lower()} "
            f"notify={summary.count}"
        )
        return True

    def request_heartbeat(self, now: float) -> None:
        """Make a settled semantic change visible without waiting 500 ms."""
        if self.device is not None:
            self.next_heartbeat = min(self.next_heartbeat, now)

    def request_notify(self) -> None:
        """Coalesce an absolute semantic update behind any due heartbeat."""
        if self.device is not None:
            self.notify_pending = True

    def next_deadline(self, now: float) -> float:
        if self.device is None:
            return max(now, self.next_connect)
        if self.notify_pending:
            return now
        return self.next_heartbeat

    def close(self) -> None:
        self._disconnect(time.monotonic())
