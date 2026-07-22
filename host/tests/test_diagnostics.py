from __future__ import annotations

import struct
import unittest

from arcane_host.diagnostics import (
    DIAGNOSTIC_PAGES,
    DIAGNOSTIC_RESPONSE,
    DIAGNOSTIC_VERSION,
    build_request,
    decode_pages,
    parse_response,
    query,
)
from arcane_host.protocol import MAGIC, REPORT_SIZE, crc8


def response(page: int, nonce: int, payload: bytes) -> bytes:
    assert len(payload) == 23
    report = bytearray(REPORT_SIZE)
    struct.pack_into(
        "<BBBBBBH",
        report,
        0,
        MAGIC[0],
        MAGIC[1],
        DIAGNOSTIC_VERSION,
        DIAGNOSTIC_RESPONSE,
        page,
        DIAGNOSTIC_PAGES,
        nonce,
    )
    report[8:31] = payload
    report[-1] = crc8(report[:-1])
    return bytes(report)


class ProtocolTests(unittest.TestCase):
    def test_request_known_vector_and_bounds(self) -> None:
        request = build_request(1, 0x1234)
        self.assertEqual(len(request), 32)
        self.assertEqual(request[:8].hex(), "ca8e017001003412")
        self.assertEqual(request[-1], crc8(request[:-1]))
        with self.assertRaises(ValueError):
            build_request(2, 1)
        with self.assertRaises(ValueError):
            build_request(0, 0x10000)

    def test_response_rejects_bad_crc_or_identity(self) -> None:
        report = bytearray(response(0, 7, bytes(23)))
        self.assertEqual(parse_response(bytes(report), page=0, nonce=7), bytes(23))
        report[12] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            parse_response(bytes(report), page=0, nonce=7)
        with self.assertRaisesRegex(ValueError, "match"):
            parse_response(response(0, 8, bytes(23)), page=0, nonce=7)

    def test_decodes_all_pages(self) -> None:
        page0 = struct.pack("<7H2IB", 1, 2, 3, 4, 5, 6, 7, 800, 900, 1)
        page1 = struct.pack("<I2HB7H", 1000, 11, 12, 1, 13, 14, 15, 16, 17, 18, 19)
        snapshot = decode_pages(page0, page1)
        self.assertEqual(snapshot.cadence, "fixed-80ms")
        self.assertEqual(snapshot.master.peak_split_tx_us, 1000)
        self.assertEqual(snapshot.master.host_stale_errors, 7)
        self.assertTrue(snapshot.peer.valid)
        self.assertEqual(snapshot.peer.stale_events, 19)


class FakeDevice:
    def __init__(self) -> None:
        self.reports: list[bytes] = []

    def send(self, report: bytes) -> None:
        page = report[4]
        nonce = int.from_bytes(report[6:8], "little")
        self.reports.extend((report, response(page, nonce, bytes([page]) * 23)))

    def receive(self, _timeout: float) -> bytes:
        return self.reports.pop(0)

    def close(self) -> None:
        pass


class QueryTests(unittest.TestCase):
    def test_queries_both_pages(self) -> None:
        snapshot = query(FakeDevice(), nonce=0x2345)
        self.assertEqual(snapshot.cadence, "adaptive-250ms-repair")
        self.assertEqual(snapshot.master.queue_overflow, 0)
        self.assertEqual(snapshot.master.peak_split_tx_us, 0x01010101)
        self.assertTrue(snapshot.peer.valid)

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


if __name__ == "__main__":
    unittest.main()
