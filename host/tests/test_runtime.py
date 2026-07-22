from __future__ import annotations

import unittest

from arcane_host.adapters import SemanticAdapters
from arcane_host.focus import FocusArbiter
from arcane_host.policy import NotificationPolicy
from arcane_host.runtime import DaemonRuntime
from arcane_host.semantic import SemanticResolver


class FakeGLib:
    next_id = 1
    added = []
    removed = []

    @classmethod
    def reset(cls):
        cls.next_id = 1
        cls.added = []
        cls.removed = []

    @classmethod
    def _add(cls, kind, delay, callback):
        source_id = cls.next_id
        cls.next_id += 1
        cls.added.append((kind, delay, callback, source_id))
        return source_id

    @classmethod
    def idle_add(cls, callback):
        return cls._add("idle", 0, callback)

    @classmethod
    def timeout_add(cls, delay, callback):
        return cls._add("timeout", delay, callback)

    @classmethod
    def source_remove(cls, source_id):
        cls.removed.append(source_id)


class FakeGio:
    unowned = []

    @classmethod
    def bus_unown_name(cls, owner_id):
        cls.unowned.append(owner_id)


class FakeLoop:
    def __init__(self):
        self.quit_calls = 0

    def quit(self):
        self.quit_calls += 1


class FakeHeartbeat:
    def __init__(self, sent=False):
        self.sent = sent
        self.notify_requests = 0
        self.closed = False

    def tick(self, _now):
        return self.sent

    def request_notify(self):
        self.notify_requests += 1

    def next_deadline(self, now):
        return now + 0.5

    def close(self):
        self.closed = True


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        FakeGLib.reset()
        FakeGio.unowned = []

    def runtime(self, *, sent=False, once=False):
        now = [10.0]
        heartbeat = FakeHeartbeat(sent)
        resolver = SemanticResolver()
        policy = NotificationPolicy()
        arbiter = FocusArbiter(settle_seconds=0)
        loop = FakeLoop()
        runtime = DaemonRuntime(
            FakeGio,
            FakeGLib,
            loop,
            heartbeat,
            resolver,
            policy,
            arbiter,
            once=once,
            clock=lambda: now[0],
        )
        adapters = SemanticAdapters(resolver, policy, runtime.wake, lambda: now[0])
        runtime.bind_adapters(adapters)
        return runtime, heartbeat, resolver, arbiter, loop

    def test_semantic_revision_requests_notify_and_schedules_deadline(self):
        runtime, heartbeat, resolver, arbiter, _loop = self.runtime()
        arbiter.report("firefox", "", 10.0)
        runtime.tick()
        self.assertEqual(resolver.state.scene.name, "ARCHIVE")
        self.assertEqual(heartbeat.notify_requests, 1)
        self.assertEqual(FakeGLib.added[-1][0], "timeout")

    def test_wake_coalesces_inside_tick(self):
        runtime, _heartbeat, _resolver, _arbiter, _loop = self.runtime()
        runtime.in_tick = True
        runtime.wake()
        runtime.wake()
        self.assertTrue(runtime.wake_pending)
        self.assertEqual(FakeGLib.added, [])

    def test_once_quits_without_rescheduling(self):
        runtime, _heartbeat, _resolver, _arbiter, loop = self.runtime(sent=True, once=True)
        runtime.tick()
        self.assertEqual(loop.quit_calls, 1)
        self.assertEqual(FakeGLib.added, [])

    def test_close_removes_sources_unsubscribes_and_unowns(self):
        runtime, heartbeat, _resolver, _arbiter, _loop = self.runtime()
        closed = []

        class Resource:
            def close(self):
                closed.append(True)

        runtime.own(Resource())
        runtime.set_bus_owner(7)
        runtime.wake()
        source_id = runtime.source_id
        runtime.close()
        runtime.close()
        self.assertEqual(FakeGLib.removed, [source_id])
        self.assertEqual(closed, [True])
        self.assertEqual(FakeGio.unowned, [7])
        self.assertTrue(heartbeat.closed)


if __name__ == "__main__":
    unittest.main()
