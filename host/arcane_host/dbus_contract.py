"""Stable public D-Bus names, methods, XML, and bounded enums."""

from enum import IntEnum

BUS_NAME = "io.github.Griffinhale.CorneArcane"
OBJECT_PATH = "/io/github/Griffinhale/CorneArcane"
FOCUS_INTERFACE = "io.github.Griffinhale.CorneArcane.Focus"
EVENTS_INTERFACE = "io.github.Griffinhale.CorneArcane.Events"
KWIN_SERVICE = "org.kde.KWin"

REPORT_ACTIVE_WINDOW = "ReportActiveWindow"
REPORT_TERMINAL_COMPLETION = "ReportTerminalCompletion"
REPORT_REPOSITORY_STATE = "ReportRepositoryState"
INJECT_SYNTHETIC = "InjectSynthetic"
CLEAR_NOTIFICATIONS = "ClearNotifications"


class RepositoryState(IntEnum):
    CLEAN = 0
    DIRTY = 1
    OPERATION = 2
    COMPLETION = 3


FOCUS_XML = f"""
<node>
  <interface name='{FOCUS_INTERFACE}'>
    <method name='{REPORT_ACTIVE_WINDOW}'>
      <arg type='s' name='resourceClass' direction='in'/>
      <arg type='s' name='desktopFileName' direction='in'/>
    </method>
  </interface>
</node>
"""

EVENTS_XML = f"""
<node>
  <interface name='{EVENTS_INTERFACE}'>
    <method name='{REPORT_TERMINAL_COMPLETION}'>
      <arg type='u' name='durationMilliseconds' direction='in'/>
      <arg type='i' name='exitStatus' direction='in'/>
    </method>
    <method name='{REPORT_REPOSITORY_STATE}'>
      <arg type='y' name='state' direction='in'/>
      <arg type='b' name='success' direction='in'/>
    </method>
    <method name='{INJECT_SYNTHETIC}'>
      <arg type='y' name='category' direction='in'/>
      <arg type='y' name='priority' direction='in'/>
      <arg type='b' name='persistent' direction='in'/>
    </method>
    <method name='{CLEAR_NOTIFICATIONS}'/>
  </interface>
</node>
"""
