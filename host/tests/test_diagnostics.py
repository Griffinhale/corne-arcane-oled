from __future__ import annotations

import contextlib
import io
import json
import struct
import unittest
from dataclasses import replace
from unittest.mock import patch

from arcane_host import diagnostics
from arcane_host.diagnostics import (
    DIAGNOSTIC_PAGES,
    DIAGNOSTIC_RESPONSE,
    DIAGNOSTIC_VERSION,
    DiagnosticSnapshot,
    MasterMetrics,
    PeerMetrics,
    build_request,
    decode_pages,
    evaluate_observation,
    parse_response,
    query,
)
from arcane_host.protocol import MAGIC, REPORT_SIZE, crc8


def response(
    page: int,
    nonce: int,
    payload: bytes,
    *,
    version: int = DIAGNOSTIC_VERSION,
    page_count: int = DIAGNOSTIC_PAGES,
) -> bytes:
    assert len(payload) == 23
    report = bytearray(REPORT_SIZE)
    struct.pack_into(
        "<BBBBBBH",
        report,
        0,
        MAGIC[0],
        MAGIC[1],
        version,
        DIAGNOSTIC_RESPONSE,
        page,
        page_count,
        nonce,
    )
    report[8:31] = payload
    report[-1] = crc8(report[:-1])
    return bytes(report)


def snapshot(**changes: object) -> DiagnosticSnapshot:
    master = MasterMetrics(
        queue_overflow=10,
        catchup_ticks=20,
        missed_tick_resyncs=30,
        stale_split_events=40,
        split_protocol_errors=50,
        host_malformed_errors=60,
        host_stale_errors=70,
        peak_housekeeping_us=1_999,
        peak_render_blit_us=4_999,
        peak_split_tx_us=800,
        split_tx_success=100,
        split_tx_failure=4,
        stack_min_free_bytes=512,
    )
    peer = PeerMetrics(
        valid=True,
        accepted_seq=13,
        snapshot_age_ms=1_000,
        peak_housekeeping_us=1_999,
        peak_render_us=4_999,
        queue_overflow=2,
        missed_tick_resyncs=3,
        stale_events=4,
        stack_min_free_bytes=256,
    )
    master_changes = changes.pop("master", {})
    peer_changes = changes.pop("peer", {})
    return DiagnosticSnapshot(
        cadence=str(changes.pop("cadence", "adaptive-250ms-repair")),
        master=replace(master, **master_changes),
        peer=replace(peer, **peer_changes),
    )


class ProtocolTests(unittest.TestCase):
    def test_request_known_vector_and_bounds(self) -> None:
        request = build_request(1, 0x1234)
        self.assertEqual(
            request.hex(),
            "ca8e0270010034120000000000000000000000000000000000000000000000bd",
        )
        with self.assertRaises(ValueError):
            build_request(3, 1)
        with self.assertRaises(ValueError):
            build_request(0, 0x10000)

    def test_response_rejects_crc_identity_v1_and_mixed_page_count(self) -> None:
        report = bytearray(response(0, 7, bytes(23)))
        self.assertEqual(parse_response(bytes(report), page=0, nonce=7), bytes(23))
        report[12] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            parse_response(bytes(report), page=0, nonce=7)
        with self.assertRaisesRegex(ValueError, "match"):
            parse_response(response(0, 8, bytes(23)), page=0, nonce=7)
        with self.assertRaisesRegex(ValueError, "v2"):
            parse_response(response(0, 7, bytes(23), version=1), page=0, nonce=7)
        with self.assertRaisesRegex(ValueError, "match"):
            parse_response(response(0, 7, bytes(23), page_count=2), page=0, nonce=7)

    def test_decodes_all_pages_and_stack_margins(self) -> None:
        page0 = struct.pack("<7H2IB", 1, 2, 3, 4, 5, 6, 7, 800, 900, 1)
        page1 = struct.pack("<I2HB7H", 1000, 11, 12, 1, 13, 14, 15, 16, 17, 18, 19)
        page2 = struct.pack("<HH19x", 20, 21)
        decoded = decode_pages(page0, page1, page2)
        self.assertEqual(decoded.cadence, "fixed-80ms")
        self.assertEqual(decoded.master.peak_split_tx_us, 1000)
        self.assertEqual(decoded.master.host_stale_errors, 7)
        self.assertEqual(decoded.master.stack_min_free_bytes, 20)
        self.assertTrue(decoded.peer.valid)
        self.assertEqual(decoded.peer.stale_events, 19)
        self.assertEqual(decoded.peer.stack_min_free_bytes, 21)

    def test_rejects_nonzero_page2_reserved_bytes_and_noncanonical_flags(self) -> None:
        page0 = bytearray(23)
        page1 = bytearray(23)
        page2 = bytearray(23)
        page2[22] = 1
        with self.assertRaisesRegex(ValueError, "reserved"):
            decode_pages(bytes(page0), bytes(page1), bytes(page2))
        page2[22] = 0
        page0[22] = 2
        with self.assertRaisesRegex(ValueError, "flag"):
            decode_pages(bytes(page0), bytes(page1), bytes(page2))
        page0[22] = 0
        page1[8] = 2
        with self.assertRaisesRegex(ValueError, "canonical"):
            decode_pages(bytes(page0), bytes(page1), bytes(page2))


class FakeDevice:
    def __init__(self) -> None:
        self.reports: list[bytes] = []

    def send(self, report: bytes) -> None:
        page = report[4]
        nonce = int.from_bytes(report[6:8], "little")
        payload = bytearray([page] * 23)
        if page == 0:
            payload[22] = 0
        elif page == 1:
            payload[8] = 1
        elif page == 2:
            payload[4:] = bytes(19)
        self.reports.extend((report, response(page, nonce, bytes(payload))))

    def receive(self, _timeout: float) -> bytes:
        return self.reports.pop(0)

    def close(self) -> None:
        pass


class QueryTests(unittest.TestCase):
    def test_queries_three_pages(self) -> None:
        decoded = query(FakeDevice(), nonce=0x2345)
        self.assertEqual(decoded.cadence, "adaptive-250ms-repair")
        self.assertEqual(decoded.master.queue_overflow, 0)
        self.assertEqual(decoded.master.peak_split_tx_us, 0x01010101)
        self.assertEqual(decoded.master.stack_min_free_bytes, 0x0202)
        self.assertTrue(decoded.peer.valid)

    def test_requires_exact_via_echo_before_metrics(self) -> None:
        class Mismatch:
            def send(self, _report: bytes) -> None:
                pass

            def receive(self, _timeout: float) -> bytes:
                return bytes(32)

            def close(self) -> None:
                pass

        with self.assertRaisesRegex(ValueError, "mismatched VIA echo"):
            query(Mismatch(), nonce=1)

    def test_release_echo_without_metrics_times_out(self) -> None:
        class EchoOnly:
            request = b""

            def send(self, report: bytes) -> None:
                self.request = report

            def receive(self, _timeout: float) -> bytes:
                if self.request:
                    report, self.request = self.request, b""
                    return report
                raise TimeoutError("release firmware")

            def close(self) -> None:
                pass

        with self.assertRaises(TimeoutError):
            query(EchoOnly(), nonce=1)


class ObservationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.before = snapshot()
        self.after = snapshot(master={"split_tx_success": 101, "catchup_ticks": 22})

    def test_passes_at_inclusive_age_and_strict_timing_boundaries(self) -> None:
        result = evaluate_observation(self.before, self.after)
        self.assertTrue(result.passed)
        self.assertEqual(result.deltas["master"]["split_tx_success"], 1)
        self.assertEqual(result.deltas["master"]["catchup_ticks"], 2)

    def test_every_snapshot_gate_fails_independently(self) -> None:
        cases = {
            "cadence": {"cadence": "fixed-80ms"},
            "peer_valid": {"peer": {"valid": False}},
            "peer_age": {"peer": {"snapshot_age_ms": 1_001}},
            "master_stack": {"master": {"stack_min_free_bytes": 0}},
            "peer_stack": {"peer": {"stack_min_free_bytes": 0}},
            "master_housekeeping": {"master": {"peak_housekeeping_us": 2_000}},
            "peer_housekeeping": {"peer": {"peak_housekeeping_us": 2_000}},
            "master_render": {"master": {"peak_render_blit_us": 5_000}},
            "peer_render": {"peer": {"peak_render_us": 5_000}},
            "split_success": {"master": {"split_tx_success": 100}},
        }
        for name, changes in cases.items():
            with self.subTest(name=name):
                after = snapshot(master={"split_tx_success": 101})
                if "master" in changes:
                    after = replace(after, master=replace(after.master, **changes["master"]))
                if "peer" in changes:
                    after = replace(after, peer=replace(after.peer, **changes["peer"]))
                if "cadence" in changes:
                    after = replace(after, cadence=str(changes["cadence"]))
                self.assertFalse(evaluate_observation(self.before, after).passed)

    def test_every_forbidden_counter_growth_fails_but_catchup_growth_does_not(self) -> None:
        master_fields = diagnostics.MASTER_ZERO_GROWTH
        peer_fields = diagnostics.PEER_COUNTERS
        for field in master_fields:
            with self.subTest(side="master", field=field):
                value = getattr(self.after.master, field)
                after = replace(self.after, master=replace(self.after.master, **{field: value + 1}))
                self.assertFalse(evaluate_observation(self.before, after).passed)
        for field in peer_fields:
            with self.subTest(side="peer", field=field):
                value = getattr(self.after.peer, field)
                after = replace(self.after, peer=replace(self.after.peer, **{field: value + 1}))
                self.assertFalse(evaluate_observation(self.before, after).passed)
        self.assertTrue(evaluate_observation(self.before, self.after).passed)

    def test_any_counter_decrease_is_a_reset(self) -> None:
        for field in diagnostics.MASTER_COUNTERS:
            with self.subTest(side="master", field=field):
                after = replace(
                    self.after,
                    master=replace(
                        self.after.master, **{field: getattr(self.before.master, field) - 1}
                    ),
                )
                self.assertFalse(
                    evaluate_observation(self.before, after).checks["counters_monotonic"]
                )
        for field in diagnostics.PEER_COUNTERS:
            with self.subTest(side="peer", field=field):
                after = replace(
                    self.after,
                    peer=replace(self.after.peer, **{field: getattr(self.before.peer, field) - 1}),
                )
                self.assertFalse(
                    evaluate_observation(self.before, after).checks["counters_monotonic"]
                )


class NullOwnership:
    def __init__(self, *_args: object, **_kwargs: object):
        pass

    def __enter__(self) -> None:
        return None

    def __exit__(self, *_args: object) -> None:
        return None


class NullDevice(NullOwnership):
    pass


class MainTests(unittest.TestCase):
    def test_observation_json_shape_is_stable_and_gate_failure_exits_two(self) -> None:
        before = snapshot()
        failed = evaluate_observation(before, snapshot(master={"split_tx_success": 100}))
        output = io.StringIO()
        with (
            patch.object(diagnostics, "ExclusiveHidOwnership", NullOwnership),
            patch.object(diagnostics, "choose_device", return_value="/dev/hidraw0"),
            patch.object(diagnostics, "Device", NullDevice),
            patch.object(diagnostics, "observe", return_value=failed),
            contextlib.redirect_stdout(output),
        ):
            self.assertEqual(diagnostics.main(["--observe", "300", "--json"]), 2)
        payload = json.loads(output.getvalue())
        self.assertEqual(set(payload), {"before", "after", "deltas", "checks", "passed"})
        self.assertFalse(payload["passed"])
        self.assertEqual(output.getvalue(), json.dumps(payload, sort_keys=True) + "\n")

    def test_passing_observation_exits_zero_and_operational_failure_exits_one(self) -> None:
        passed = evaluate_observation(
            snapshot(), snapshot(master={"split_tx_success": 101, "catchup_ticks": 21})
        )
        with (
            patch.object(diagnostics, "ExclusiveHidOwnership", NullOwnership),
            patch.object(diagnostics, "choose_device", return_value="/dev/hidraw0"),
            patch.object(diagnostics, "Device", NullDevice),
            patch.object(diagnostics, "observe", return_value=passed),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(diagnostics.main(["--observe", "1"]), 0)
        with (
            patch.object(diagnostics, "ExclusiveHidOwnership", NullOwnership),
            patch.object(diagnostics, "choose_device", side_effect=OSError("missing")),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(diagnostics.main([]), 1)

    def test_no_service_handoff_is_forwarded_explicitly(self) -> None:
        seen: list[bool] = []

        class RecordingOwnership(NullOwnership):
            def __init__(self, *, service_handoff: bool):
                seen.append(service_handoff)

        with (
            patch.object(diagnostics, "ExclusiveHidOwnership", RecordingOwnership),
            patch.object(diagnostics, "choose_device", return_value="/dev/hidraw0"),
            patch.object(diagnostics, "Device", NullDevice),
            patch.object(diagnostics, "query", return_value=snapshot()),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(diagnostics.main(["--no-service-handoff"]), 0)
        self.assertEqual(seen, [False])


if __name__ == "__main__":
    unittest.main()
