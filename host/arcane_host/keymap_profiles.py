"""Save and load whole keymaps as named profiles on this computer.

The keyboard keeps one keymap. A profile is a copy of it in
~/.local/share/corne-arcane/keymaps/NAME.json, read and written with the same
VIA commands Vial uses (dynamic keymap get/set buffer), under the shared
exclusive guard so neither the daemon nor Vial is talking to the keyboard at
the same time.

A profile records a layout hash: the keyboard's Vial UID, its layer count and
the matrix size. Loading onto a keyboard whose hash differs is refused, since
the same bytes would mean different keys there.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

from .heartbeat import HidTransport
from .hid_ownership import ExclusiveHidOwnership, OwnershipSignal
from .hidraw import Device, choose_device
from .protocol import REPORT_SIZE

# crkbd rev1: 4 rows x 6 columns per half. VIA addresses the whole matrix,
# including positions with no switch, two bytes (big-endian keycode) each.
MATRIX_ROWS = 8
MATRIX_COLS = 6
KEY_BYTES = 2

VIA_GET_LAYER_COUNT = 0x11
VIA_GET_BUFFER = 0x12
VIA_SET_BUFFER = 0x13
VIAL_PREFIX = 0xFE
VIAL_GET_KEYBOARD_ID = 0x00
CHUNK = 28  # via.c: size <= 28 per request
TIMEOUT = 1.0

FORMAT = 1
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}")


class ProfileError(RuntimeError):
    pass


def profiles_dir() -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "corne-arcane" / "keymaps"


def profile_path(name: str) -> Path:
    if not NAME.fullmatch(name):
        raise ProfileError(
            f"invalid profile name {name!r}: use letters, digits, '.', '_' or '-', "
            "starting with a letter or digit, at most 64 characters"
        )
    return profiles_dir() / f"{name}.json"


def layout_hash(keyboard_uid: bytes, layers: int) -> str:
    material = b"corne-arcane-keymap/1" + keyboard_uid + bytes((layers, MATRIX_ROWS, MATRIX_COLS))
    return hashlib.sha256(material).hexdigest()[:16]


class ViaKeymap:
    """The keymap half of VIA's Raw HID protocol, over any HidTransport."""

    def __init__(self, device: HidTransport) -> None:
        self.device = device

    def _exchange(self, request: bytes) -> bytes:
        report = request.ljust(REPORT_SIZE, b"\0")
        self.device.send(report)
        reply = self.device.receive(TIMEOUT)
        if len(reply) != REPORT_SIZE:
            raise ProfileError(
                f"short reply ({len(reply)} bytes) to VIA command 0x{request[0]:02x}"
            )
        return reply

    def layer_count(self) -> int:
        reply = self._exchange(bytes((VIA_GET_LAYER_COUNT,)))
        if reply[0] != VIA_GET_LAYER_COUNT or not 1 <= reply[1] <= 32:
            raise ProfileError("the keyboard did not report a layer count; is this the Corne?")
        return reply[1]

    def keyboard_uid(self) -> bytes:
        # Vial clears the whole report before answering, so the reply cannot be
        # checked against the command byte; eight zero bytes mean no answer.
        reply = self._exchange(bytes((VIAL_PREFIX, VIAL_GET_KEYBOARD_ID)))
        uid = reply[4:12]
        if not any(uid):
            raise ProfileError("the keyboard did not report a Vial keyboard ID")
        return uid

    def size(self, layers: int) -> int:
        return layers * MATRIX_ROWS * MATRIX_COLS * KEY_BYTES

    def read(self, layers: int) -> bytes:
        data = bytearray()
        total = self.size(layers)
        while len(data) < total:
            offset, count = len(data), min(CHUNK, total - len(data))
            request = bytes((VIA_GET_BUFFER, offset >> 8, offset & 0xFF, count))
            reply = self._exchange(request)
            if reply[:4] != request:
                raise ProfileError(f"keymap read at offset {offset} was not answered")
            data += reply[4 : 4 + count]
        return bytes(data)

    def write(self, keymap: bytes) -> None:
        for offset in range(0, len(keymap), CHUNK):
            chunk = keymap[offset : offset + CHUNK]
            request = bytes((VIA_SET_BUFFER, offset >> 8, offset & 0xFF, len(chunk))) + chunk
            if self._exchange(request)[: len(request)] != request:
                raise ProfileError(f"keymap write at offset {offset} was not acknowledged")


def save(via: ViaKeymap, name: str, *, force: bool = False) -> Path:
    path = profile_path(name)
    if path.exists() and not force:
        raise ProfileError(f"profile {name!r} exists; pass --force to replace it")
    layers = via.layer_count()
    keymap = via.read(layers)
    keys = MATRIX_ROWS * MATRIX_COLS
    codes = [int.from_bytes(keymap[i : i + 2], "big") for i in range(0, len(keymap), 2)]
    document = {
        "format": FORMAT,
        "layout_hash": layout_hash(via.keyboard_uid(), layers),
        "layers": layers,
        "rows": MATRIX_ROWS,
        "cols": MATRIX_COLS,
        "keymap": [codes[layer * keys : (layer + 1) * keys] for layer in range(layers)],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(document, indent=1) + "\n")
    temporary.replace(path)
    return path


def read_profile(name: str) -> dict:
    path = profile_path(name)
    try:
        document = json.loads(path.read_text())
    except FileNotFoundError:
        raise ProfileError(f"no profile named {name!r} in {profiles_dir()}") from None
    except (OSError, ValueError) as error:
        raise ProfileError(f"profile {name!r} is unreadable ({error})") from None
    if not isinstance(document, dict) or document.get("format") != FORMAT:
        raise ProfileError(f"profile {name!r} is not a format {FORMAT} keymap profile")
    return document


def profile_bytes(document: dict) -> bytes:
    layers, keys = document.get("layers"), MATRIX_ROWS * MATRIX_COLS
    keymap = document.get("keymap")
    if (
        not isinstance(layers, int)
        or not isinstance(keymap, list)
        or len(keymap) != layers
        or any(not isinstance(layer, list) or len(layer) != keys for layer in keymap)
        or any(
            not isinstance(code, int) or not 0 <= code <= 0xFFFF
            for layer in keymap
            for code in layer
        )
    ):
        raise ProfileError("profile keymap does not match its declared shape")
    return b"".join(code.to_bytes(2, "big") for layer in keymap for code in layer)


def load(via: ViaKeymap, name: str) -> None:
    document = read_profile(name)
    keymap = profile_bytes(document)
    layers = via.layer_count()
    expected = layout_hash(via.keyboard_uid(), layers)
    if document.get("layout_hash") != expected:
        raise ProfileError(
            f"profile {name!r} was saved from a different layout "
            f"({document.get('layout_hash')} here {expected}); not loaded"
        )
    via.write(keymap)
    if via.read(layers) != keymap:
        raise ProfileError("the keyboard did not keep the loaded keymap; load it again")


def list_profiles() -> list[str]:
    try:
        return sorted(path.stem for path in profiles_dir().glob("*.json"))
    except OSError:
        return []


def delete(name: str) -> None:
    try:
        profile_path(name).unlink()
    except FileNotFoundError:
        raise ProfileError(f"no profile named {name!r} in {profiles_dir()}") from None


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="corne-arcane-keymap", description=__doc__.split("\n")[0])
    parser.add_argument("--device", help="explicit /dev/hidrawN path")
    commands = parser.add_subparsers(dest="command", required=True)
    save_parser = commands.add_parser("save", help="copy the keyboard's keymap into a profile")
    save_parser.add_argument("name")
    save_parser.add_argument("--force", action="store_true", help="replace an existing profile")
    commands.add_parser("load", help="write a profile to the keyboard").add_argument("name")
    commands.add_parser("list", help="list saved profiles")
    commands.add_parser("delete", help="remove a saved profile").add_argument("name")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "list":
            for name in list_profiles():
                print(name)
            return 0
        if args.command == "delete":
            delete(args.name)
            return 0
        profile_path(args.name)  # a bad name fails before the daemon is paused
        if args.command == "load":
            read_profile(args.name)
        with ExclusiveHidOwnership(holder="corne-arcane-keymap", device=args.device):
            with Device(choose_device(args.device)) as device:
                via = ViaKeymap(device)
                if args.command == "save":
                    print(f"saved {save(via, args.name, force=args.force)}")
                else:
                    load(via, args.name)
                    print(f"loaded {args.name}")
    except OwnershipSignal as interrupted:
        print(f"corne-arcane-keymap: interrupted by {interrupted}", file=sys.stderr)
        return 1
    except (OSError, RuntimeError, ValueError) as error:
        print(f"corne-arcane-keymap: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
