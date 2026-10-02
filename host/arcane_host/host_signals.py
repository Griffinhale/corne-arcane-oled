"""The opt-in host signals at the detail a desktop shell can take (city ABI 10).

The keyboard hears the desktop's own state folded into the civic mode and
intensity the wire already carries: away is QUIET, a ringing call or critical
notification URGENT, a near-full resource STRAIN, a long command ACTIVE. A
desktop city reads the same producers over the Control interface's
HostSignalsChanged, one enum each, with the steps the wire has no room for.

Every value is a small enum whose zero means "nothing sent": what the service
reports with --host-signals off, and what every shell without the service
sends. No caller, title, command text, process, mount or stream name exists at
this level, and there is no field one could ride in. The values are
``duel_city.h``'s DUEL_CITY_PRESENCE_* .. DUEL_CITY_COMMAND_* and are never
renumbered.
"""

from __future__ import annotations

from enum import IntEnum
from typing import NamedTuple


class Presence(IntEnum):
    """``DUEL_CITY_PRESENCE_*``: the session's idle and lock hints."""

    NONE = 0
    ACTIVE = 1
    IDLE = 2
    LOCKED = 3


class LoadLevel(IntEnum):
    """``DUEL_CITY_LOAD_*``: CPU pressure in six steps where the wire has four."""

    NONE = 0
    IDLE = 1
    LIGHT = 2
    STEADY = 3
    BUSY = 4
    HEAVY = 5
    SATURATED = 6


class StrainKind(IntEnum):
    """``DUEL_CITY_STRAIN_*``: which resource is near full; disk, then memory, then CPU."""

    NONE = 0
    CLEAR = 1
    CPU = 2
    MEMORY = 3
    DISK = 4


class CallState(IntEnum):
    """``DUEL_CITY_CALL_*``: ringing is the wire's URGENT, joined its QUIET."""

    NONE = 0
    CLEAR = 1
    RINGING = 2
    JOINED = 3


class AlertState(IntEnum):
    """``DUEL_CITY_ALERT_*``: a critical notification holding URGENT, apart from a call."""

    NONE = 0
    CLEAR = 1
    CRITICAL = 2


class CommandState(IntEnum):
    """``DUEL_CITY_COMMAND_*``: long commands still running, counted, never named."""

    NONE = 0
    IDLE = 1
    RUNNING = 2
    SEVERAL = 3


class HostSignals(NamedTuple):
    """The six host-signal bytes, in ``duel_city_input_t`` and HostSignalsChanged order."""

    presence: Presence = Presence.NONE
    load: LoadLevel = LoadLevel.NONE
    strain: StrainKind = StrainKind.NONE
    call: CallState = CallState.NONE
    alert: AlertState = AlertState.NONE
    command: CommandState = CommandState.NONE

    @classmethod
    def from_bytes(cls, values: tuple[int, ...]) -> HostSignals:
        """Read six bytes back into enums; a value outside its enum raises ValueError."""
        if len(values) != len(HOST_SIGNAL_FIELDS):
            raise ValueError(f"expected {len(HOST_SIGNAL_FIELDS)} host signals, got {len(values)}")
        return cls(*(kind(value) for (_, kind), value in zip(HOST_SIGNAL_FIELDS, values)))

    def as_bytes(self) -> tuple[int, ...]:
        return tuple(int(value) for value in self)


# Struct and signal order, with each field's enum.
HOST_SIGNAL_FIELDS: tuple[tuple[str, type[IntEnum]], ...] = (
    ("presence", Presence),
    ("load", LoadLevel),
    ("strain", StrainKind),
    ("call", CallState),
    ("alert", AlertState),
    ("command", CommandState),
)

NO_HOST_SIGNALS = HostSignals()
