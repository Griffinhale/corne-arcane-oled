"""Reduce one window of typing to the four bucketed values the helper may share.

The opt-in typing helper (NF-D3, spec NF-D6) reads keydowns on a keyboard
without this firmware. This module is the only thing that turns them into
something that leaves the helper, and it is shaped so nothing else can: its
input is a window of (row, time) pairs, never a key or keycode, and its output
is a fixed tuple of four small enums -- eight bits a minute. No per-key time,
pair latency, order or count survives, and a window too sparse to hide a short
secret in yields nothing at all.

It is pure: no I/O, no clock, no state between calls. The caller holds one
window's events, calls :func:`summarize` once at the window's edge, and drops
them.
"""

from __future__ import annotations

import statistics
from collections.abc import Iterable
from enum import IntEnum
from typing import NamedTuple

# Tumbling windows aligned to the wall-clock minute; the caller cuts them.
WINDOW_MS = 60_000
# A window with fewer keydowns or in-burst gaps is dropped, not reported.
MIN_KEYDOWNS = 40
MIN_GAPS = 20
# Gaps this long or longer split bursts and are not counted: the firmware's
# 13-tick commit pause (duel_incantation.h).
BURST_GAP_MS = 520

# Tempo cut points on the mean in-burst gap, host-only and tuned to desk
# typing (NF-D6 Q2): a typical desk typist's mean gap is about 240 ms, so the
# buckets centre there rather than on the firmware's faster spell cadence.
FRANTIC_BELOW_MS = 110
RAPID_BELOW_MS = 150
FLOWING_BELOW_MS = 220
# Spread cut points on the interquartile range of in-burst gaps.
STEADY_BELOW_MS = 40
VARIED_BELOW_MS = 160
# Row spread: the busiest row's share of keydowns, in percent.
FOCUSED_FROM_PERCENT = 60
MIXED_FROM_PERCENT = 40


class Tempo(IntEnum):
    DELIBERATE = 0
    FLOWING = 1
    RAPID = 2
    FRANTIC = 3


class Spread(IntEnum):
    STEADY = 0
    VARIED = 1
    IRREGULAR = 2


class Row(IntEnum):
    """Firmware row index: the number row folds into TOP, space and modifiers into THUMB."""

    TOP = 0
    HOME = 1
    BOTTOM = 2
    THUMB = 3


class RowSpread(IntEnum):
    FOCUSED = 0
    MIXED = 1
    EVEN = 2


class TypingSummary(NamedTuple):
    """Everything one window says: four enums, nothing else."""

    tempo: Tempo
    spread: Spread
    row: Row
    row_spread: RowSpread


def summarize(events: Iterable[tuple[Row | int, int]]) -> TypingSummary | None:
    """Reduce one window of (row, keydown time in ms) to a summary, or None.

    Events may arrive in any order. None means the window was too sparse to
    report; the caller sends nothing for it. A window spanning more than
    WINDOW_MS is a caller error and raises ValueError, as does a row outside
    :class:`Row`.
    """
    keydowns = sorted((time, Row(row)) for row, time in events)
    if len(keydowns) < MIN_KEYDOWNS:
        return None
    if keydowns[-1][0] - keydowns[0][0] > WINDOW_MS:
        raise ValueError("events span more than one window")
    gaps = [
        later - earlier
        for (earlier, _), (later, _) in zip(keydowns, keydowns[1:])
        if later - earlier < BURST_GAP_MS
    ]
    if len(gaps) < MIN_GAPS:
        return None
    counts = [0] * len(Row)
    for _, row in keydowns:
        counts[row] += 1
    return TypingSummary(
        _tempo(statistics.fmean(gaps)),
        _spread(gaps),
        _busiest(counts),
        _row_spread(max(counts), len(keydowns)),
    )


def _tempo(mean_gap: float) -> Tempo:
    if mean_gap < FRANTIC_BELOW_MS:
        return Tempo.FRANTIC
    if mean_gap < RAPID_BELOW_MS:
        return Tempo.RAPID
    if mean_gap < FLOWING_BELOW_MS:
        return Tempo.FLOWING
    return Tempo.DELIBERATE


def _spread(gaps: list[int]) -> Spread:
    lower, _, upper = statistics.quantiles(gaps, n=4, method="inclusive")
    spread = upper - lower
    if spread < STEADY_BELOW_MS:
        return Spread.STEADY
    if spread < VARIED_BELOW_MS:
        return Spread.VARIED
    return Spread.IRREGULAR


def _busiest(counts: list[int]) -> Row:
    """The row with the most keydowns; a tie goes to HOME, else to the lower row."""
    most = max(counts)
    if counts[Row.HOME] == most:
        return Row.HOME
    return Row(counts.index(most))


def _row_spread(most: int, total: int) -> RowSpread:
    if most * 100 >= FOCUSED_FROM_PERCENT * total:
        return RowSpread.FOCUSED
    if most * 100 >= MIXED_FROM_PERCENT * total:
        return RowSpread.MIXED
    return RowSpread.EVEN
