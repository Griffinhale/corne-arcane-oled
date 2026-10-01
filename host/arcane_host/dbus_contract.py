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
REPORT_BROWSER_ACTIVITY = "ReportBrowserActivity"
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
    <method name='{REPORT_BROWSER_ACTIVITY}'>
      <arg type='y' name='kind' direction='in'/>
      <arg type='y' name='intensity' direction='in'/>
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

# Control: read the keyboard link and lend it out without stopping the unit.
# Status is (link, device, paused, owner). link is one of CONTROL_LINKS; device
# is the hidraw path while connected, else ""; owner is the label the pausing
# client gave, else "". Nothing here carries desktop content.
CONTROL_INTERFACE = "io.github.Griffinhale.CorneArcane.Control"
STATUS = "Status"
PAUSE = "Pause"
RESUME = "Resume"
STATUS_CHANGED = "StatusChanged"
STATUS_SIGNATURE = "(ssbs)"
CONTROL_BUSY = f"{CONTROL_INTERFACE}.Busy"
CONTROL_LINKS = ("starting", "connected", "absent", "denied", "several", "failed", "paused")
OWNER_LABEL_MAX = 80
# World is the eight bytes every heartbeat already carries to the keyboard, so
# a desktop view draws the city the keyboard shows without a daemon of its own:
# scene, notification count, category, priority, age, persistent, civic and
# secondary. Integer enums only; no title, path or notification text exists at
# this level. WorldChanged fires when the resolved state changes.
WORLD = "World"
WORLD_CHANGED = "WorldChanged"
WORLD_SIGNATURE = "(yyyyyyyy)"

CONTROL_XML = f"""
<node>
  <interface name='{CONTROL_INTERFACE}'>
    <method name='{STATUS}'>
      <arg type='s' name='link' direction='out'/>
      <arg type='s' name='device' direction='out'/>
      <arg type='b' name='paused' direction='out'/>
      <arg type='s' name='owner' direction='out'/>
    </method>
    <method name='{PAUSE}'>
      <arg type='s' name='owner' direction='in'/>
    </method>
    <method name='{RESUME}'/>
    <method name='{WORLD}'>
      <arg type='y' name='scene' direction='out'/>
      <arg type='y' name='notifCount' direction='out'/>
      <arg type='y' name='category' direction='out'/>
      <arg type='y' name='priority' direction='out'/>
      <arg type='y' name='age' direction='out'/>
      <arg type='y' name='persistent' direction='out'/>
      <arg type='y' name='civic' direction='out'/>
      <arg type='y' name='secondary' direction='out'/>
    </method>
    <signal name='{STATUS_CHANGED}'>
      <arg type='s' name='link'/>
      <arg type='s' name='device'/>
      <arg type='b' name='paused'/>
      <arg type='s' name='owner'/>
    </signal>
    <signal name='{WORLD_CHANGED}'>
      <arg type='y' name='scene'/>
      <arg type='y' name='notifCount'/>
      <arg type='y' name='category'/>
      <arg type='y' name='priority'/>
      <arg type='y' name='age'/>
      <arg type='y' name='persistent'/>
      <arg type='y' name='civic'/>
      <arg type='y' name='secondary'/>
    </signal>
  </interface>
</node>
"""
