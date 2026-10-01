"""The typing-summary reducer: one window of (row, time) keydowns in, four enums out."""

from __future__ import annotations

import ast
import random
import unittest
from pathlib import Path

from arcane_host import typing_summary
from arcane_host.typing_summary import (
    BURST_GAP_MS,
    MIN_GAPS,
    MIN_KEYDOWNS,
    WINDOW_MS,
    Row,
    RowSpread,
    Spread,
    Tempo,
    TypingSummary,
    summarize,
)


def stream(gaps: list[int], rows: list[Row], start: int = 1_000) -> list[tuple[Row, int]]:
    """Keydowns at start, then each gap after the last; rows in order."""
    assert len(rows) == len(gaps) + 1
    times = [start]
    for gap in gaps:
        times.append(times[-1] + gap)
    return list(zip(rows, times))


def busy(gap: int = 180, count: int = 50, row: Row = Row.HOME) -> list[tuple[Row, int]]:
    return stream([gap] * (count - 1), [row] * count)


class ShapeTests(unittest.TestCase):
    def test_output_is_bounded(self) -> None:
        summary = summarize(busy())
        self.assertIsInstance(summary, TypingSummary)
        self.assertIsInstance(summary, tuple)
        self.assertEqual(len(summary), 4)
        self.assertEqual([type(field) for field in summary], [Tempo, Spread, Row, RowSpread])
        # Four enums of at most four values: eight bits a minute, no more.
        for kind in (Tempo, Spread, Row, RowSpread):
            self.assertLessEqual(len(kind), 4)
            self.assertEqual({int(value) for value in kind}, set(range(len(kind))))

    def test_the_spec_numbers(self) -> None:
        self.assertEqual(WINDOW_MS, 60_000)
        self.assertEqual(MIN_KEYDOWNS, 40)
        self.assertEqual(MIN_GAPS, 20)
        self.assertEqual(BURST_GAP_MS, 520)

    def test_sparse_window_dropped(self) -> None:
        self.assertIsNone(summarize([]))
        self.assertIsNone(summarize(busy(count=MIN_KEYDOWNS - 1)))
        self.assertIsNotNone(summarize(busy(count=MIN_KEYDOWNS)))
        # Forty keys, but too few of them in bursts: every other pause is long.
        gaps = [180 if index % 3 == 0 else 900 for index in range(MIN_KEYDOWNS - 1)]
        self.assertLess(sum(gap < BURST_GAP_MS for gap in gaps), MIN_GAPS)
        self.assertIsNone(summarize(stream(gaps, [Row.HOME] * MIN_KEYDOWNS)))

    def test_a_window_longer_than_a_minute_is_refused(self) -> None:
        events = busy(gap=1_600, count=MIN_KEYDOWNS)
        with self.assertRaises(ValueError):
            summarize(events)

    def test_only_rows_are_accepted(self) -> None:
        with self.assertRaises(ValueError):
            summarize([(7, 1_000)] * MIN_KEYDOWNS)


class BucketTests(unittest.TestCase):
    def test_tempo_cut_points(self) -> None:
        for gap, tempo in (
            (90, Tempo.FRANTIC),
            (109, Tempo.FRANTIC),
            (110, Tempo.RAPID),
            (149, Tempo.RAPID),
            (150, Tempo.FLOWING),
            (219, Tempo.FLOWING),
            (220, Tempo.DELIBERATE),
            (400, Tempo.DELIBERATE),
        ):
            self.assertEqual(summarize(busy(gap=gap, count=45)).tempo, tempo, gap)

    def test_pauses_split_bursts_and_are_not_counted(self) -> None:
        gaps = [100] * 44 + [5_000]
        self.assertEqual(summarize(stream(gaps, [Row.HOME] * 46)).tempo, Tempo.FRANTIC)

    def test_spread_follows_the_interquartile_range(self) -> None:
        self.assertEqual(summarize(busy(gap=180)).spread, Spread.STEADY)
        varied = [130, 230] * 25
        self.assertEqual(summarize(stream(varied, [Row.HOME] * 51)).spread, Spread.VARIED)
        irregular = [60, 400] * 25
        self.assertEqual(summarize(stream(irregular, [Row.HOME] * 51)).spread, Spread.IRREGULAR)

    def test_row_is_the_busiest_and_ties_go_home(self) -> None:
        rows = [Row.TOP] * 30 + [Row.HOME] * 10 + [Row.THUMB] * 10
        self.assertEqual(summarize(stream([180] * 49, rows)).row, Row.TOP)
        tied = [Row.BOTTOM] * 25 + [Row.HOME] * 25
        self.assertEqual(summarize(stream([180] * 49, tied)).row, Row.HOME)

    def test_row_spread_is_the_busiest_rows_share(self) -> None:
        for rows, spread in (
            ([Row.HOME] * 30 + [Row.TOP] * 20, RowSpread.FOCUSED),
            ([Row.HOME] * 25 + [Row.TOP] * 15 + [Row.THUMB] * 10, RowSpread.MIXED),
            (
                [Row.HOME] * 15 + [Row.TOP] * 12 + [Row.BOTTOM] * 12 + [Row.THUMB] * 11,
                RowSpread.EVEN,
            ),
        ):
            self.assertEqual(summarize(stream([180] * 49, rows)).row_spread, spread)


class PrivacyTests(unittest.TestCase):
    def test_same_bucket_same_output(self) -> None:
        """Streams that differ key by key but share every bucket are indistinguishable.

        Each trial reorders which key had which gap and which row, and nudges
        every gap, while keeping the mean and the spread inside their buckets.
        The reducer must give the same four values, so no per-key time, no
        pair latency and no order can be read back out of a summary.
        """
        generator = random.Random(0xC0)
        for _ in range(200):
            count = generator.randint(MIN_KEYDOWNS, 120)
            gaps = [generator.randint(160, 200) for _ in range(count - 1)]
            rows = [generator.choice(list(Row)) for _ in range(count)]
            reference = summarize(stream(gaps, rows))
            self.assertIsNotNone(reference)

            shuffled_gaps = gaps[:]
            generator.shuffle(shuffled_gaps)
            nudged = [gap + generator.choice((-3, 0, 3)) for gap in shuffled_gaps]
            shuffled_rows = rows[:]
            generator.shuffle(shuffled_rows)
            other = stream(nudged, shuffled_rows, start=generator.randint(0, 5_000))
            self.assertEqual(summarize(other), reference)

    def test_order_of_events_is_irrelevant(self) -> None:
        events = stream([150 + (index * 7) % 60 for index in range(59)], [Row.TOP] * 60)
        reversed_input = list(reversed(events))
        self.assertEqual(summarize(events), summarize(reversed_input))

    def test_the_reducer_keeps_nothing_and_reaches_nothing(self) -> None:
        """No I/O imports and no module state a call could leave behind."""
        tree = ast.parse(Path(typing_summary.__file__).read_text())
        imported = {
            node.module or "" for node in ast.walk(tree) if isinstance(node, ast.ImportFrom)
        } | {
            alias.name
            for node in ast.walk(tree)
            if isinstance(node, ast.Import)
            for alias in node.names
        }
        self.assertLessEqual(
            imported, {"__future__", "collections.abc", "enum", "statistics", "typing"}
        )
        for node in ast.walk(tree):
            self.assertNotIsInstance(node, (ast.Global, ast.Nonlocal))
        for node in tree.body:
            if isinstance(node, (ast.Assign, ast.AnnAssign)) and node.value is not None:
                self.assertNotIsInstance(
                    node.value, (ast.List, ast.Dict, ast.Set, ast.ListComp, ast.DictComp, ast.Call)
                )


if __name__ == "__main__":
    unittest.main()
