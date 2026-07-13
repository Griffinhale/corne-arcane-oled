"""Minimal M8 heartbeat/scene/synthetic-notification sender."""

from __future__ import annotations

import argparse
import secrets
import time

from .hidraw import Device, choose_device
from .protocol import Message, Scene, build_packet

SCENES = {scene.name.lower(): scene for scene in Scene}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", help="explicit /dev/hidrawN path")
    parser.add_argument("--scene", choices=SCENES, default="duel")
    parser.add_argument("--notify", type=int, default=0, metavar="COUNT")
    parser.add_argument("--interval", type=float, default=0.5, metavar="SECONDS")
    parser.add_argument("--once", action="store_true", help="send one paced probe and exit")
    parser.add_argument("--dry-run", action="store_true", help="print reports instead of opening hidraw")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--session", type=lambda value: int(value, 0), help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 0 <= args.notify <= 15:
        parser.error("--notify must be in 0..15")
    if not 0.1 <= args.interval < 1.5:
        parser.error("--interval must be at least 0.1 and below the 1.5 s firmware timeout")
    return args


def run(args: argparse.Namespace) -> int:
    scene = SCENES[args.scene]
    session = args.session if args.session is not None else (secrets.randbits(32) or 1)
    sequence = 0

    if args.dry_run:
        def send(report: bytes) -> None:
            print(report.hex())
        device = None
    else:
        path = choose_device(args.device)
        device = Device(path)
        if args.verbose:
            print(f"arcane-host: device={path} session=0x{session:08x}")
        send = device.send

    try:
        send(build_packet(Message.HELLO, session, sequence, scene, args.notify))
        # The firmware mailbox is intentionally latest-wins. Pace the greeting
        # so it is adopted before any later semantic report can replace it.
        time.sleep(0.1)
        if args.notify:
            sequence = (sequence + 1) & 0xFFFF
            send(build_packet(Message.NOTIFY, session, sequence, scene, args.notify))
            time.sleep(0.1)

        while True:
            sequence = (sequence + 1) & 0xFFFF
            send(build_packet(Message.HEARTBEAT, session, sequence, scene, args.notify))
            if args.verbose:
                print(f"arcane-host: heartbeat seq={sequence} scene={scene.name.lower()} notify={args.notify}")
            if args.once:
                break
            time.sleep(args.interval)
    except KeyboardInterrupt:
        if args.verbose:
            print("arcane-host: stopped; firmware context will expire within 1.5 s")
    finally:
        if device is not None:
            device.close()
    return 0


def main() -> int:
    return run(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
