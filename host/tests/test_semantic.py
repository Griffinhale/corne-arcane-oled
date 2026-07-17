from __future__ import annotations

import unittest

from arcane_host.adapters import DBusAdapterHub, SemanticAdapters
from arcane_host.focus import FocusArbiter, classify_floor
from arcane_host.policy import NotificationPolicy
from arcane_host.profiles import canonical_identifier, resolve_profile
from arcane_host.protocol import (
    Category,
    EMPTY_SUMMARY,
    Floor,
    Intensity,
    Mode,
    Priority,
    Scene,
    Secondary,
)
from arcane_host.semantic import SemanticResolver


class SemanticTests(unittest.TestCase):
    def test_scene_precedence_and_revision(self) -> None:
        resolver = SemanticResolver()
        self.assertFalse(resolver.update(summary=EMPTY_SUMMARY))
        self.assertTrue(resolver.update(focus_scene=Scene.ARCHIVE))
        self.assertEqual(resolver.state.scene, Scene.ARCHIVE)
        # Media keeps the scene at ARCHIVE but now also lights the MEDIA
        # secondary channel, so the civic bytes (and revision) advance.
        self.assertTrue(resolver.update(media_playing=True))
        self.assertEqual(resolver.state.scene, Scene.ARCHIVE)
        self.assertEqual(resolver.state.civic.secondary, Secondary.MEDIA)
        revision = resolver.state.revision
        self.assertFalse(resolver.update(focus_scene=Scene.DUEL))
        self.assertEqual(resolver.state.revision, revision)
        self.assertTrue(resolver.update(media_playing=False))
        self.assertEqual(resolver.state.scene, Scene.DUEL)
        self.assertEqual(resolver.state.civic.secondary, Secondary.NONE)
        self.assertTrue(resolver.update(pomodoro=True))
        self.assertEqual(resolver.state.scene, Scene.FOCUS)
        revision = resolver.state.revision
        self.assertFalse(resolver.update(pomodoro=True))
        self.assertEqual(resolver.state.revision, revision)

    def test_canonical_profiles(self) -> None:
        self.assertEqual(resolve_profile("Firefox", None).identifier, "browser")
        self.assertEqual(resolve_profile(None, "org.kde.konsole.desktop").identifier, "terminal")
        self.assertEqual(canonical_identifier("org.mozilla.firefox.desktop"), "browser")
        self.assertIsNone(resolve_profile("unknown-app", None))

    def test_floor_classification_per_profile(self) -> None:
        self.assertEqual(resolve_profile("firefox", None).floor, Floor.RESEARCH)
        self.assertEqual(resolve_profile("org.kde.konsole", None).floor, Floor.WORKSHOP)
        self.assertEqual(resolve_profile("slack", None).floor, Floor.COMMONS)
        self.assertEqual(resolve_profile("spotify", None).floor, Floor.COMMONS)
        # classify_floor mirrors the arbiter's floor derivation, unknown -> COMMONS.
        self.assertEqual(classify_floor("firefox", None), Floor.RESEARCH)
        self.assertEqual(classify_floor("kitty", None), Floor.WORKSHOP)
        self.assertEqual(classify_floor("unknown-app", None), Floor.COMMONS)

    def test_focus_arbiter_settles_floor_with_scene(self) -> None:
        arbiter = FocusArbiter(settle_seconds=0)
        self.assertEqual(arbiter.floor, Floor.COMMONS)
        arbiter.report("firefox", "firefox.desktop", 0.0)
        arbiter.poll(0.0)
        self.assertEqual((arbiter.scene, arbiter.floor), (Scene.ARCHIVE, Floor.RESEARCH))
        arbiter.report("kitty", "kitty.desktop", 1.0)
        arbiter.poll(1.0)
        self.assertEqual((arbiter.scene, arbiter.floor), (Scene.DUEL, Floor.WORKSHOP))

    def test_civic_bytes_for_representative_states(self) -> None:
        # browser focus -> RESEARCH floor (civic byte 0x01).
        resolver = SemanticResolver()
        self.assertTrue(resolver.update(focus_floor=Floor.RESEARCH))
        self.assertEqual(resolver.state.civic.floor, Floor.RESEARCH)
        self.assertEqual(resolver.state.civic.civic_byte(), 0x01)

        # terminal focus -> WORKSHOP floor (civic byte 0x02).
        resolver.update(focus_floor=Floor.WORKSHOP)
        self.assertEqual(resolver.state.civic.civic_byte(), 0x02)

        # default/unknown focus -> COMMONS floor (civic byte 0x00).
        resolver.update(focus_floor=Floor.COMMONS)
        self.assertEqual(resolver.state.civic.civic_byte(), 0x00)

        # DND -> QUIET mode over COMMONS (civic byte 0x04); intensity stays CALM.
        self.assertTrue(resolver.update(dnd=True))
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)
        self.assertEqual(resolver.state.civic.intensity, Intensity.CALM)
        self.assertEqual(resolver.state.civic.civic_byte(), 0x04)
        resolver.update(dnd=False)
        # Pomodoro alone selects the quiet Observatory.
        resolver.update(pomodoro=True)
        self.assertEqual(resolver.state.civic.floor, Floor.SPECIAL)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)
        resolver.update(pomodoro=False)
        self.assertEqual(resolver.state.civic.floor, Floor.COMMONS)

    def test_pomodoro_floor_precedence_focus_completion_and_dnd(self) -> None:
        resolver = SemanticResolver()
        resolver.update(focus_scene=Scene.ARCHIVE, focus_floor=Floor.RESEARCH)
        resolver.update(dnd=True)
        self.assertEqual(resolver.state.civic.floor, Floor.RESEARCH)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)

        resolver.update(pomodoro=True)
        self.assertEqual(resolver.state.civic.floor, Floor.SPECIAL)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)
        # Focus continues to settle behind the Observatory.
        resolver.update(focus_scene=Scene.DUEL, focus_floor=Floor.WORKSHOP)
        self.assertEqual(resolver.state.civic.floor, Floor.SPECIAL)
        resolver.update(pomodoro=False)
        self.assertEqual(resolver.state.civic.floor, Floor.WORKSHOP)
        self.assertEqual(resolver.state.civic.mode, Mode.QUIET)
        resolver.update(dnd=False)
        self.assertEqual(resolver.state.civic.floor, Floor.WORKSHOP)
        self.assertEqual(resolver.state.civic.mode, Mode.NORMAL)

        # MPRIS playing -> MEDIA secondary channel (secondary byte 0x01).
        self.assertTrue(resolver.update(media_playing=True))
        self.assertEqual(resolver.state.civic.secondary, Secondary.MEDIA)
        self.assertEqual(resolver.state.civic.secondary_byte(), 0x01)

    def test_secondary_channel_precedence(self) -> None:
        resolver = SemanticResolver()
        resolver.update(media_playing=True)
        self.assertEqual(resolver.state.civic.secondary, Secondary.MEDIA)
        # Transfer outranks media; a system/network alert outranks transfer.
        resolver.update(transfer_active=True)
        self.assertEqual(resolver.state.civic.secondary, Secondary.TRANSFER)
        resolver.update(system_alert=True)
        self.assertEqual(resolver.state.civic.secondary, Secondary.SYSTEM)
        resolver.update(system_alert=False, transfer_active=False)
        self.assertEqual(resolver.state.civic.secondary, Secondary.MEDIA)
        resolver.update(media_playing=False)
        self.assertEqual(resolver.state.civic.secondary, Secondary.NONE)

    def test_adapter_secondary_channels(self) -> None:
        now = [20.0]
        resolver = SemanticResolver()
        policy = NotificationPolicy()
        adapters = SemanticAdapters(resolver, policy, lambda: None, lambda: now[0])
        adapters.media("Playing", None)
        self.assertEqual(resolver.state.civic.secondary, Secondary.MEDIA)
        adapters.network("offline")
        self.assertEqual(resolver.state.civic.secondary, Secondary.SYSTEM)
        adapters.network("online")
        self.assertEqual(resolver.state.civic.secondary, Secondary.MEDIA)
        adapters.repository(2, True)  # operation in flight
        self.assertEqual(resolver.state.civic.secondary, Secondary.TRANSFER)
        adapters.repository(3, True)  # completion clears the channel
        self.assertEqual(resolver.state.civic.secondary, Secondary.MEDIA)

    def test_pomodoro_uses_warning_and_completion_deadlines(self) -> None:
        now = [20.0]
        resolver = SemanticResolver()
        policy = NotificationPolicy()
        adapters = SemanticAdapters(resolver, policy, lambda: None, lambda: now[0])
        adapters.pomodoro(True, 120)
        self.assertEqual(adapters.next_deadline(now[0]), 80.0)
        now[0] = 80.0
        self.assertTrue(adapters.poll(now[0]))
        self.assertEqual(policy.summary(now[0]).category, Category.CALENDAR)
        self.assertEqual(adapters.next_deadline(now[0]), 140.0)
        now[0] = 140.0
        self.assertTrue(adapters.poll(now[0]))
        self.assertEqual(resolver.state.scene, Scene.DUEL)
        self.assertEqual(resolver.state.civic.floor, Floor.COMMONS)
        self.assertIsNone(adapters.next_deadline(now[0]))

    def test_all_adapter_mappings(self) -> None:
        now = [20.0]
        changes = []
        resolver = SemanticResolver()
        policy = NotificationPolicy()
        adapters = SemanticAdapters(resolver, policy, lambda: changes.append(True), lambda: now[0])

        adapters.media("Playing", "/track/opaque-id")
        self.assertEqual(resolver.state.scene, Scene.ARCHIVE)
        self.assertEqual(policy.summary(now[0]).category, Category.OTHER)
        adapters.dnd(True)
        self.assertEqual(resolver.state.scene, Scene.FOCUS)
        adapters.dnd(False)
        adapters.pomodoro(True, 50)
        self.assertEqual(resolver.state.scene, Scene.FOCUS)
        self.assertEqual(policy.summary(now[0]).category, Category.CALENDAR)

        policy.clear()
        now[0] += 20
        adapters.network("offline")
        summary = policy.summary(now[0])
        self.assertEqual((summary.category, summary.priority, summary.persistent),
                         (Category.SYSTEM, Priority.CRITICAL, True))
        adapters.network("online", vpn=True)
        self.assertEqual(policy.summary(now[0]).category, Category.SECURITY)

        policy.clear()
        now[0] += 20
        self.assertTrue(adapters.repository(1, True))
        self.assertEqual(policy.summary(now[0]).category, Category.TRANSFER)
        self.assertFalse(adapters.repository(9, True))
        self.assertGreaterEqual(len(changes), 6)

    def test_dbus_hub_routes_live_property_semantics(self) -> None:
        class Variant:
            def __init__(self, value):
                self.value = value

            def unpack(self):
                return self.value

            def get_child_value(self, index):
                value = self.value[index]
                return value if isinstance(value, Variant) else Variant(value)

            def lookup_value(self, key, _type):
                value = self.value.get(key)
                if value is None:
                    return None
                return value if isinstance(value, Variant) else Variant(value)

        class Connection:
            def __init__(self):
                self.callbacks = []

            def signal_subscribe(self, *_args):
                self.callbacks.append(_args[-1])
                return len(self.callbacks)

            def signal_unsubscribe(self, _subscription):
                pass

        class Gio:
            class DBusSignalFlags:
                NONE = 0

        now = [100.0]
        resolver = SemanticResolver()
        policy = NotificationPolicy()
        adapters = SemanticAdapters(resolver, policy, lambda: None, lambda: now[0])
        session = Connection()
        system = Connection()
        hub = DBusAdapterHub(Gio, session, system, adapters, "focus-timer.timer")

        def properties(callback, sender, path, interface, values):
            callback(
                None,
                sender,
                path,
                None,
                None,
                Variant((interface, Variant(values), ())),
            )

        properties(
            hub._session_properties,
            ":1.20",
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            {
                "PlaybackStatus": "Playing",
                "Metadata": Variant({"mpris:trackid": Variant("/opaque/track")}),
            },
        )
        properties(
            hub._session_properties,
            ":1.21",
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            {"PlaybackStatus": "Paused"},
        )
        self.assertEqual(resolver.state.scene, Scene.ARCHIVE)
        hub._name_owner_changed(
            None, None, None, None, None,
            Variant(("org.mpris.MediaPlayer2.one", ":1.20", "")),
        )
        self.assertEqual(resolver.state.scene, Scene.DUEL)

        properties(
            hub._session_properties,
            ":1.30",
            "/org/freedesktop/systemd1/unit/focus_2dtimer_2etimer",
            "org.freedesktop.systemd1.Unit",
            {"ActiveState": "active"},
        )
        properties(
            hub._session_properties,
            ":1.30",
            "/org/freedesktop/systemd1/unit/focus_2dtimer_2etimer",
            "org.freedesktop.systemd1.Timer",
            {"NextElapseUSecMonotonic": 150_000_000},
        )
        self.assertEqual(resolver.state.scene, Scene.FOCUS)
        self.assertEqual(policy.summary(now[0]).category, Category.CALENDAR)

        policy.clear()
        now[0] = 120.0
        properties(
            hub._system_properties,
            ":1.40",
            "/org/freedesktop/NetworkManager",
            "org.freedesktop.NetworkManager",
            {"Connectivity": 2},
        )
        self.assertEqual(policy.summary(now[0]).category, Category.SYSTEM)
        now[0] = 120.1
        properties(
            hub._system_properties,
            ":1.40",
            "/org/freedesktop/NetworkManager/ActiveConnection/1",
            "org.freedesktop.NetworkManager.Connection.Active",
            {"Vpn": True},
        )
        self.assertEqual(policy.summary(now[0]).category, Category.SECURITY)
        hub.close()
        self.assertEqual(hub.subscriptions, [])


if __name__ == "__main__":
    unittest.main()
