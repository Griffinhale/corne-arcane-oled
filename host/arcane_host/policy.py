"""Bounded, monotonic-time notification policy for M10."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Hashable

from .protocol import Category, EMPTY_SUMMARY, NotificationSummary, Priority

NORMAL_LIFETIME = 6.0
START_GAP = 10.0
AGE_THRESHOLDS = (0.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0)
MAX_ENTRIES = 64


def age_bucket(seconds: float) -> int:
    if seconds < 0:
        raise ValueError("age cannot be negative")
    bucket = 0
    for index, threshold in enumerate(AGE_THRESHOLDS):
        if seconds >= threshold:
            bucket = index
    return bucket


@dataclass
class _Entry:
    key: Hashable
    category: Category
    priority: Priority
    first: float
    newest_distinct: float
    occurrences: int
    persistent: bool


class NotificationPolicy:
    """Aggregate transient batches and independently tracked critical alerts."""

    def __init__(self, *, max_entries: int = MAX_ENTRIES) -> None:
        if max_entries < 1:
            raise ValueError("max_entries must be positive")
        self.max_entries = max_entries
        self._normal: dict[Hashable, _Entry] = {}
        self._critical: dict[Hashable, _Entry] = {}
        self._batch_started: float | None = None
        self._batch_expires = 0.0
        self._next_start = 0.0
        self.suppressed_cooldown = 0
        self.evicted = 0
        self._event_serial = 0

    @staticmethod
    def _validate(category: Category, priority: Priority, persistent: bool) -> None:
        if category == Category.NONE:
            raise ValueError("events require a category")
        if priority == Priority.NONE:
            raise ValueError("events require a priority")
        if persistent and priority != Priority.CRITICAL:
            raise ValueError("only critical events may persist")

    @staticmethod
    def _inc(value: int) -> int:
        return min(15, value + 1)

    def _expire_batch(self, now: float) -> None:
        if self._batch_started is not None and now >= self._batch_expires:
            self._normal.clear()
            self._batch_started = None

    def _make_room(self, incoming_persistent: bool) -> bool:
        while len(self._normal) + len(self._critical) >= self.max_entries:
            if self._normal:
                oldest = min(self._normal.values(), key=lambda entry: entry.first)
                del self._normal[oldest.key]
            elif incoming_persistent and self._critical:
                oldest = min(self._critical.values(), key=lambda entry: entry.first)
                del self._critical[oldest.key]
            else:
                self.evicted = min(0xFFFF, self.evicted + 1)
                return False
            self.evicted = min(0xFFFF, self.evicted + 1)
        return True

    def inject(
        self,
        category: Category,
        priority: Priority,
        persistent: bool,
        now: float,
        *,
        key: Hashable | None = None,
        replacement: bool = False,
    ) -> bool:
        """Apply an event; return whether externally visible policy may change."""
        self._validate(category, priority, persistent)
        self._expire_batch(now)
        if key is None:
            self._event_serial = (self._event_serial + 1) & 0xFFFFFFFF
            event_key: Hashable = ("event", self._event_serial)
        else:
            event_key = key

        if persistent:
            existing = self._critical.get(event_key)
            if existing is not None:
                existing.category = category
                existing.priority = priority
                if not replacement:
                    existing.occurrences = self._inc(existing.occurrences)
                return True
            prior_normal = self._normal.pop(event_key, None)
            if not self._make_room(True):
                if prior_normal is not None:
                    self._normal[event_key] = prior_normal
                return False
            self._critical[event_key] = _Entry(
                event_key,
                category,
                priority,
                prior_normal.first if prior_normal is not None else now,
                prior_normal.newest_distinct if prior_normal is not None else now,
                prior_normal.occurrences if prior_normal is not None else 1,
                True,
            )
            return True

        if self._batch_started is None:
            if now < self._next_start:
                self.suppressed_cooldown = min(0xFFFF, self.suppressed_cooldown + 1)
                return False
            self._batch_started = now
            self._batch_expires = now + NORMAL_LIFETIME
            self._next_start = now + START_GAP

        existing = self._normal.get(event_key)
        if existing is not None:
            existing.category = category
            existing.priority = priority
            if not replacement:
                existing.occurrences = self._inc(existing.occurrences)
            return True
        if not self._make_room(False):
            return False
        self._normal[event_key] = _Entry(event_key, category, priority, now, now, 1, False)
        return True

    def close(self, key: Hashable) -> bool:
        return self._critical.pop(key, None) is not None

    def clear(self) -> None:
        self._normal.clear()
        self._critical.clear()
        self._batch_started = None
        self._next_start = 0.0

    def summary(self, now: float) -> NotificationSummary:
        self._expire_batch(now)
        active = tuple(self._critical.values()) + tuple(self._normal.values())
        if not active:
            return EMPTY_SUMMARY
        representative = max(active, key=lambda entry: (int(entry.priority), entry.newest_distinct))
        count = min(15, sum(entry.occurrences for entry in active))
        return NotificationSummary(
            count,
            representative.category,
            representative.priority,
            age_bucket(now - representative.first),
            representative.persistent,
        )

    @property
    def entry_count(self) -> int:
        return len(self._normal) + len(self._critical)

    def next_deadline(self, now: float) -> float | None:
        """Return the next expiry or visible age-bucket boundary."""
        self._expire_batch(now)
        deadlines: list[float] = []
        if self._batch_started is not None:
            deadlines.append(self._batch_expires)
        active = tuple(self._critical.values()) + tuple(self._normal.values())
        if active:
            representative = max(
                active, key=lambda entry: (int(entry.priority), entry.newest_distinct)
            )
            age = now - representative.first
            for threshold in AGE_THRESHOLDS:
                if threshold > age:
                    deadlines.append(representative.first + threshold)
                    break
        return min(deadlines) if deadlines else None
