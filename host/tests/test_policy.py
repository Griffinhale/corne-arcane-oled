from __future__ import annotations

import unittest

from arcane_host.policy import NotificationPolicy, age_bucket
from arcane_host.protocol import Category, Priority


class PolicyTests(unittest.TestCase):
    def test_age_buckets(self) -> None:
        samples = (
            (0, 0),
            (14.9, 0),
            (15, 1),
            (30, 2),
            (60, 3),
            (120, 4),
            (300, 5),
            (600, 6),
            (900, 7),
            (9999, 7),
        )
        for seconds, expected in samples:
            self.assertEqual(age_bucket(seconds), expected)

    def test_fixed_batch_repeat_and_cooldown(self) -> None:
        policy = NotificationPolicy()
        self.assertTrue(policy.inject(Category.TERMINAL, Priority.LOW, False, 1.0, key="a"))
        self.assertTrue(policy.inject(Category.TERMINAL, Priority.LOW, False, 5.9, key="a"))
        self.assertEqual(policy.summary(6.9).count, 2)
        self.assertEqual(policy.summary(7.0).count, 0)
        self.assertFalse(policy.inject(Category.SYSTEM, Priority.NORMAL, False, 8.0, key="b"))
        self.assertEqual(policy.suppressed_cooldown, 1)
        self.assertTrue(policy.inject(Category.SYSTEM, Priority.NORMAL, False, 11.0, key="b"))

    def test_count_saturation_and_representative(self) -> None:
        policy = NotificationPolicy()
        for _ in range(20):
            policy.inject(Category.TRANSFER, Priority.NORMAL, False, 1.0, key="repeat")
        policy.inject(Category.CALENDAR, Priority.NORMAL, False, 2.0, key="new")
        summary = policy.summary(2.0)
        self.assertEqual(summary.count, 15)
        self.assertEqual(summary.category, Category.CALENDAR)
        policy.inject(Category.SECURITY, Priority.CRITICAL, True, 2.1, key="critical")
        self.assertEqual(policy.summary(2.1).category, Category.SECURITY)
        self.assertTrue(policy.summary(2.1).persistent)

    def test_critical_close_clear_and_transient_critical(self) -> None:
        policy = NotificationPolicy()
        policy.inject(Category.SECURITY, Priority.CRITICAL, True, 0, key=4)
        self.assertTrue(policy.close(4))
        self.assertEqual(policy.summary(0).count, 0)
        policy.inject(Category.SECURITY, Priority.CRITICAL, False, 10, key=5)
        self.assertFalse(policy.summary(10).persistent)
        self.assertEqual(policy.summary(16).count, 0)
        policy.inject(Category.SECURITY, Priority.CRITICAL, True, 20, key=6)
        policy.clear()
        self.assertEqual(policy.summary(20).count, 0)

    def test_duplicate_does_not_reset_age(self) -> None:
        policy = NotificationPolicy()
        policy.inject(Category.OTHER, Priority.NORMAL, False, 0, key="same")
        policy.inject(Category.OTHER, Priority.NORMAL, False, 4, key="same")
        self.assertEqual(policy.summary(5).age, 0)
        # Persistent entries survive long enough to prove the first timestamp.
        policy.inject(Category.SECURITY, Priority.CRITICAL, True, 10, key="p")
        policy.inject(Category.SECURITY, Priority.CRITICAL, True, 800, key="p")
        self.assertEqual(policy.summary(910).age, 7)

    def test_bounded_storage_preserves_critical_before_normal(self) -> None:
        policy = NotificationPolicy(max_entries=2)
        policy.inject(Category.SECURITY, Priority.CRITICAL, True, 0, key="c1")
        policy.inject(Category.SECURITY, Priority.CRITICAL, True, 1, key="c2")
        self.assertFalse(policy.inject(Category.OTHER, Priority.NORMAL, False, 2, key="n"))
        self.assertEqual(policy.entry_count, 2)
        self.assertEqual(policy.summary(2).count, 2)


if __name__ == "__main__":
    unittest.main()
