"""Read diagnostics-build timing and split counters over QMK Raw HID."""

from __future__ import annotations

import argparse
import json
import secrets
import struct
import sys
import time
from dataclasses import asdict, dataclass

from .heartbeat import HidTransport
from .hidraw import Device, choose_device
from .protocol import MAGIC, REPORT_SIZE, crc8

DIAGNOSTIC_VERSION = 1
DIAGNOSTIC_PAGES = 2
DIAGNOSTIC_REQUEST = 0x70
DIAGNOSTIC_RESPONSE = 0x71
FIXED_SPLIT_CADENCE = 0x01


@dataclass(frozen=True)
class MasterMetrics:
    queue_overflow: int
    catchup_ticks: int
    missed_tick_resyncs: int
    stale_split_events: int
    split_protocol_errors: int
    host_malformed_errors: int
    host_stale_errors: int
    peak_housekeeping_us: int
    peak_render_blit_us: int
    peak_split_tx_us: int
    split_tx_success: int
    split_tx_failure: int


@dataclass(frozen=True)
class PeerMetrics:
    valid: bool
    accepted_seq: int
    snapshot_age_ms: int
    peak_housekeeping_us: int
    peak_render_us: int
    queue_overflow: int
    missed_tick_resyncs: int
    stale_events: int


@dataclass(frozen=True)
class DiagnosticSnapshot:
    cadence: str
    master: MasterMetrics
    peer: PeerMetrics


def build_request(page: int, nonce: int) -> bytes:
    if not 0 <= page < DIAGNOSTIC_PAGES:
        raise ValueError(f"diagnostic page must be in 0..{DIAGNOSTIC_PAGES - 1}")
    if not 0 <= nonce <= 0xFFFF:
        raise ValueError("diagnostic nonce must fit uint16")
    report = bytearray(REPORT_SIZE)
    struct.pack_into(
        "<BBBBBBH",
        report,
        0,
        MAGIC[0],
        MAGIC[1],
        DIAGNOSTIC_VERSION,
        DIAGNOSTIC_REQUEST,
        page,
        0,
        nonce,
    )
    report[-1] = crc8(report[:-1])
    return bytes(report)


def parse_response(report: bytes, *, page: int, nonce: int) -> bytes:
    if len(report) != REPORT_SIZE:
        raise ValueError(f"diagnostic response must be {REPORT_SIZE} bytes")
    magic0, magic1, version, message, actual_page, page_count, actual_nonce = struct.unpack_from(
        "<BBBBBBH", report
    )
    if (magic0, magic1) != MAGIC:
        raise ValueError("diagnostic response has bad magic")
    if version != DIAGNOSTIC_VERSION or message != DIAGNOSTIC_RESPONSE:
        raise ValueError("report is not a supported diagnostic response")
    if actual_page != page or page_count != DIAGNOSTIC_PAGES or actual_nonce != nonce:
        raise ValueError("diagnostic response does not match the request")
    if report[-1] != crc8(report[:-1]):
        raise ValueError("diagnostic response has bad CRC")
    return report[8:31]


def decode_pages(page0: bytes, page1: bytes) -> DiagnosticSnapshot:
    if len(page0) != 23 or len(page1) != 23:
        raise ValueError("diagnostic payload pages must be 23 bytes")
    (
        queue_overflow,
        catchup_ticks,
        missed_tick_resyncs,
        stale_split_events,
        split_protocol_errors,
        host_malformed_errors,
        host_stale_errors,
        peak_housekeeping_us,
        peak_render_blit_us,
        flags,
    ) = struct.unpack("<7H2IB", page0)
    (
        peak_split_tx_us,
        split_tx_success,
        split_tx_failure,
        peer_valid,
        accepted_seq,
        snapshot_age_ms,
        peer_peak_housekeeping_us,
        peer_peak_render_us,
        peer_queue_overflow,
        peer_missed_tick_resyncs,
        peer_stale_events,
    ) = struct.unpack("<I2HB7H", page1)
    return DiagnosticSnapshot(
        cadence="fixed-80ms" if flags & FIXED_SPLIT_CADENCE else "adaptive-250ms-repair",
        master=MasterMetrics(
            queue_overflow=queue_overflow,
            catchup_ticks=catchup_ticks,
            missed_tick_resyncs=missed_tick_resyncs,
            stale_split_events=stale_split_events,
            split_protocol_errors=split_protocol_errors,
            host_malformed_errors=host_malformed_errors,
            host_stale_errors=host_stale_errors,
            peak_housekeeping_us=peak_housekeeping_us,
            peak_render_blit_us=peak_render_blit_us,
            peak_split_tx_us=peak_split_tx_us,
            split_tx_success=split_tx_success,
            split_tx_failure=split_tx_failure,
        ),
        peer=PeerMetrics(
            valid=bool(peer_valid),
            accepted_seq=accepted_seq,
            snapshot_age_ms=snapshot_age_ms,
            peak_housekeeping_us=peer_peak_housekeeping_us,
            peak_render_us=peer_peak_render_us,
            queue_overflow=peer_queue_overflow,
            missed_tick_resyncs=peer_missed_tick_resyncs,
            stale_events=peer_stale_events,
        ),
    )


def _read_page(device: HidTransport, page: int, nonce: int, timeout: float) -> bytes:
    request = build_request(page, nonce)
    device.send(request)
    deadline = time.monotonic() + timeout
    echo = device.receive(timeout)
    if echo != request:
        raise ValueError(f"diagnostic page {page} received a mismatched VIA echo")
    last_error: ValueError | None = None
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            detail = f" ({last_error})" if last_error else ""
            raise TimeoutError(f"timed out waiting for diagnostic page {page}{detail}")
        report = device.receive(remaining)
        try:
            return parse_response(report, page=page, nonce=nonce)
        except ValueError as error:
            # A stale reply from an earlier invocation is harmless; retain its
            # reason for a useful timeout error and continue until the deadline.
            last_error = error


def query(
    device: HidTransport, *, timeout: float = 1.0, nonce: int | None = None
) -> DiagnosticSnapshot:
    if timeout <= 0:
        raise ValueError("timeout must be positive")
    request_nonce = secrets.randbelow(0x10000) if nonce is None else nonce
    page0 = _read_page(device, 0, request_nonce, timeout)
    page1 = _read_page(device, 1, request_nonce, timeout)
    return decode_pages(page0, page1)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", help="explicit /dev/hidrawN path")
    parser.add_argument("--timeout", type=float, default=1.0, metavar="SECONDS")
    parser.add_argument("--json", action="store_true", help="emit machine-comparable JSON")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def _print_human(snapshot: DiagnosticSnapshot) -> None:
    print(f"cadence: {snapshot.cadence}")
    print("master:")
    for name, value in asdict(snapshot.master).items():
        print(f"  {name}: {value}")
    print("peer:")
    for name, value in asdict(snapshot.peer).items():
        print(f"  {name}: {str(value).lower() if isinstance(value, bool) else value}")


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        with Device(choose_device(args.device)) as device:
            snapshot = query(device, timeout=args.timeout)
    except TimeoutError as error:
        print(
            "corne-arcane-diagnostics: no metrics response; "
            "flash firmware built with ARCANE_DIAGNOSTICS=yes "
            f"({error})",
            file=sys.stderr,
        )
        return 1
    except (OSError, RuntimeError, ValueError) as error:
        print(f"corne-arcane-diagnostics: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(asdict(snapshot), sort_keys=True))
    else:
        _print_human(snapshot)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
