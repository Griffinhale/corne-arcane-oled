from __future__ import annotations

import unittest

from arcane_host.adapters import DBusAdapterHub, SemanticAdapters
from arcane_host.policy import NotificationPolicy
from arcane_host.profiles import canonical_identifier, resolve_profile
from arcane_host.protocol import Category, EMPTY_SUMMARY, Priority, Scene
from arcane_host.semantic import SemanticResolver


class SemanticTests(unittest.TestCase):
    def test_scene_precedence_and_revision(self) -> None:
        resolver = SemanticResolver()
        self.assertFalse(resolver.update(summary=EMPTY_SUMMARY))
        self.assertTrue(resolver.update(focus_scene=Scene.ARCHIVE))
        self.assertEqual(resolver.state.scene, Scene.ARCHIVE)
        revision = resolver.state.revision
        self.assertFalse(resolver.update(media_playing=True))
        self.assertEqual(resolver.state.scene, Scene.ARCHIVE)
        self.assertEqual(resolver.state.revision, revision)
        self.assertFalse(resolver.update(focus_scene=Scene.DUEL))
        self.assertEqual(resolver.state.revision, revision)
        self.assertTrue(resolver.update(media_playing=False))
        self.assertEqual(resolver.state.scene, Scene.DUEL)
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
