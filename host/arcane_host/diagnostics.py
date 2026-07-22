"""Read and observe diagnostics-build timing, stack, and split counters."""

from __future__ import annotations

import argparse
import json
import secrets
import struct
import sys
import time
from dataclasses import asdict, dataclass
from typing import Any

from .heartbeat import HidTransport
from .hid_ownership import ExclusiveHidOwnership, OwnershipSignal
from .hidraw import Device, choose_device
from .protocol import MAGIC, REPORT_SIZE, crc8

DIAGNOSTIC_VERSION = 2
DIAGNOSTIC_PAGES = 3
DIAGNOSTIC_REQUEST = 0x70
DIAGNOSTIC_RESPONSE = 0x71
FIXED_SPLIT_CADENCE = 0x01
MAX_SNAPSHOT_AGE_MS = 1_000
MAX_HOUSEKEEPING_US = 2_000
MAX_RENDER_US = 5_000

MASTER_COUNTERS = (
    "queue_overflow",
    "catchup_ticks",
    "missed_tick_resyncs",
    "stale_split_events",
    "split_protocol_errors",
    "host_malformed_errors",
    "host_stale_errors",
    "split_tx_success",
    "split_tx_failure",
)
PEER_COUNTERS = ("queue_overflow", "missed_tick_resyncs", "stale_events")
MASTER_ZERO_GROWTH = tuple(name for name in MASTER_COUNTERS if name != "catchup_ticks")
MASTER_ZERO_GROWTH = tuple(name for name in MASTER_ZERO_GROWTH if name != "split_tx_success")


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
    stack_min_free_bytes: int


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
    stack_min_free_bytes: int


@dataclass(frozen=True)
class DiagnosticSnapshot:
    cadence: str
    master: MasterMetrics
    peer: PeerMetrics


@dataclass(frozen=True)
class Observation:
    before: DiagnosticSnapshot
    after: DiagnosticSnapshot
    deltas: dict[str, dict[str, int]]
    checks: dict[str, bool]
    passed: bool


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
        raise ValueError("report is not a supported diagnostic v2 response")
    if actual_page != page or page_count != DIAGNOSTIC_PAGES or actual_nonce != nonce:
        raise ValueError("diagnostic response does not match the request")
    if report[-1] != crc8(report[:-1]):
        raise ValueError("diagnostic response has bad CRC")
    return report[8:31]


def decode_pages(page0: bytes, page1: bytes, page2: bytes) -> DiagnosticSnapshot:
    if any(len(page) != 23 for page in (page0, page1, page2)):
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
    if flags & ~FIXED_SPLIT_CADENCE:
        raise ValueError("diagnostic page 0 has nonzero reserved flag bits")
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
    if peer_valid not in (0, 1):
        raise ValueError("diagnostic peer validity flag is not canonical")
    stack_min_free_bytes, peer_stack_min_free_bytes = struct.unpack_from("<HH", page2)
    if any(page2[4:]):
        raise ValueError("diagnostic page 2 reserved bytes must be zero")
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
            stack_min_free_bytes=stack_min_free_bytes,
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
            stack_min_free_bytes=peer_stack_min_free_bytes,
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
            last_error = error


def query(
    device: HidTransport, *, timeout: float = 1.0, nonce: int | None = None
) -> DiagnosticSnapshot:
    if timeout <= 0:
        raise ValueError("timeout must be positive")
    request_nonce = secrets.randbelow(0x10000) if nonce is None else nonce
    pages = [_read_page(device, page, request_nonce, timeout) for page in range(DIAGNOSTIC_PAGES)]
    return decode_pages(*pages)


def evaluate_observation(before: DiagnosticSnapshot, after: DiagnosticSnapshot) -> Observation:
    deltas = {
        "master": {
            name: getattr(after.master, name) - getattr(before.master, name)
            for name in MASTER_COUNTERS
        },
        "peer": {
            name: getattr(after.peer, name) - getattr(before.peer, name) for name in PEER_COUNTERS
        },
    }
    checks = {
        "cadence_unchanged": after.cadence == before.cadence,
        "counters_monotonic": all(
            value >= 0 for group in deltas.values() for value in group.values()
        ),
        "peer_valid": after.peer.valid,
        "peer_snapshot_age_at_most_1000_ms": after.peer.snapshot_age_ms <= MAX_SNAPSHOT_AGE_MS,
        "split_success_increased": deltas["master"]["split_tx_success"] > 0,
        "master_stack_nonzero": after.master.stack_min_free_bytes > 0,
        "peer_stack_nonzero": after.peer.stack_min_free_bytes > 0,
        "master_housekeeping_below_2000_us": after.master.peak_housekeeping_us
        < MAX_HOUSEKEEPING_US,
        "peer_housekeeping_below_2000_us": after.peer.peak_housekeeping_us < MAX_HOUSEKEEPING_US,
        "master_render_below_5000_us": after.master.peak_render_blit_us < MAX_RENDER_US,
        "peer_render_below_5000_us": after.peer.peak_render_us < MAX_RENDER_US,
    }
    for name in MASTER_ZERO_GROWTH:
        checks[f"master_{name}_unchanged"] = deltas["master"][name] == 0
    for name in PEER_COUNTERS:
        checks[f"peer_{name}_unchanged"] = deltas["peer"][name] == 0
    return Observation(
        before=before,
        after=after,
        deltas=deltas,
        checks=checks,
        passed=all(checks.values()),
    )


def observe(device: HidTransport, seconds: float, *, timeout: float = 1.0) -> Observation:
    if seconds <= 0:
        raise ValueError("observation duration must be positive")
    before = query(device, timeout=timeout)
    time.sleep(seconds)
    after = query(device, timeout=timeout)
    return evaluate_observation(before, after)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", help="explicit /dev/hidrawN path")
    parser.add_argument("--timeout", type=float, default=1.0, metavar="SECONDS")
    parser.add_argument("--observe", type=float, metavar="SECONDS")
    parser.add_argument("--json", action="store_true", help="emit stable machine-readable JSON")
    parser.add_argument(
        "--no-service-handoff",
        action="store_true",
        help="do not inspect, stop, or restore the host daemon (development only)",
    )
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.observe is not None and args.observe <= 0:
        parser.error("--observe must be positive")
    return args


def _print_snapshot(snapshot: DiagnosticSnapshot) -> None:
    print(f"cadence: {snapshot.cadence}")
    print("master:")
    for name, value in asdict(snapshot.master).items():
        print(f"  {name}: {value}")
    print("peer:")
    for name, value in asdict(snapshot.peer).items():
        print(f"  {name}: {str(value).lower() if isinstance(value, bool) else value}")


def _print_observation(result: Observation) -> None:
    print("final:")
    _print_snapshot(result.after)
    print("deltas:")
    for side, values in result.deltas.items():
        print(f"  {side}:")
        for name, value in values.items():
            print(f"    {name}: {value:+d}")
    print("checks:")
    for name, passed in result.checks.items():
        print(f"  {name}: {'pass' if passed else 'FAIL'}")
    print(f"passed: {str(result.passed).lower()}")


def _json_value(value: DiagnosticSnapshot | Observation) -> dict[str, Any]:
    return asdict(value)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        with ExclusiveHidOwnership(service_handoff=not args.no_service_handoff):
            with Device(choose_device(args.device)) as device:
                result: DiagnosticSnapshot | Observation
                if args.observe is None:
                    result = query(device, timeout=args.timeout)
                else:
                    result = observe(device, args.observe, timeout=args.timeout)
    except TimeoutError as error:
        print(
            "corne-arcane-diagnostics: no metrics response; "
            "flash firmware built with ARCANE_DIAGNOSTICS=yes "
            f"({error})",
            file=sys.stderr,
        )
        return 1
    except OwnershipSignal as interrupted:
        print(f"corne-arcane-diagnostics: interrupted by {interrupted}", file=sys.stderr)
        return 1
    except (OSError, RuntimeError, ValueError) as error:
        print(f"corne-arcane-diagnostics: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(_json_value(result), sort_keys=True))
    elif isinstance(result, Observation):
        _print_observation(result)
    else:
        _print_snapshot(result)
    return 2 if isinstance(result, Observation) and not result.passed else 0


if __name__ == "__main__":
    raise SystemExit(main())
