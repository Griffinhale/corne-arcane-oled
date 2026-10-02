"""Whether a microphone is capturing, from ALSA device state alone.

A joined call or meeting holds the microphone open. The kernel reports each
capture substream's state in /proc/asound/cardN/pcmMc/subK/status: "closed",
or a block that starts "state: RUNNING" while something records. PipeWire and
PulseAudio open the device only while a stream captures, and suspend it a few
seconds after the last one ends. Nothing here names an application, stream
or caller: the files hold device state and the sound server's own pid.

Bluetooth headset microphones do not go through ALSA and are not seen, and a
camera has no device-state flag this can read; both are gaps, not leaks.
"""

from __future__ import annotations

import glob
from pathlib import Path
from typing import Callable

CAPTURE_STATUS_GLOB = "/proc/asound/card*/pcm*c/sub*/status"


def capture_running(text: str) -> bool:
    for row in text.splitlines():
        key, _, value = row.partition(":")
        if key.strip() == "state":
            return value.strip() == "RUNNING"
    return False


def capture_sampler(
    paths: Callable[[], list[str]] = lambda: glob.glob(CAPTURE_STATUS_GLOB),
    read: Callable[[str], str] = lambda path: Path(path).read_text(),
) -> Callable[[], bool]:
    """The callable SemanticAdapters polls: is any capture substream running?"""

    def sample() -> bool:
        for path in paths():
            try:
                if capture_running(read(path)):
                    return True
            except (OSError, UnicodeDecodeError):
                continue
        return False

    return sample
