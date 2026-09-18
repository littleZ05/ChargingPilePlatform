"""时间轴单测：面板形状、掩码语义、缺测排除与自洽性守卫。"""
import tempfile
import unittest
from pathlib import Path

import support
import timeline


class TimelineTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_panel_shape_and_missing_day_mask(self):
        hourly = support.flat_days(3) + [None] + support.flat_days(2)
        panel = timeline.build(support.create_database(self.root / 'w.db', hourly))
        self.assertEqual(panel['span_days'], 6)
        self.assertEqual(panel['observed_days'], 5)
        self.assertEqual(panel['missing_days'], 1)
        self.assertEqual(panel['events_total'], 5 * 24)
        self.assertEqual(len(panel['panel']), 6 * 24)

        missing = [row for row in panel['panel'] if row['day_index'] == 3]
        self.assertEqual(len(missing), 24)
        self.assertTrue(all(row['mask'] == 0 for row in missing))
        self.assertTrue(all(row['sessions'] == 0 for row in missing))

    def test_missing_day_excluded_from_totals(self):
        hourly = support.flat_days(3) + [None] + support.flat_days(2)
        panel = timeline.build(support.create_database(self.root / 'w.db', hourly))
        self.assertEqual(sum(row['sessions'] for row in panel['panel']), 120)
        self.assertEqual(sum(row['mask'] for row in panel['panel']), 5 * 24)

    def test_hour_without_sessions_is_a_real_zero(self):
        """有采集但该小时没人充电，是真实的 0，不能当成缺测。"""
        hourly = support.flat_days(2, sessions_per_hour=0)
        hourly[0][9] = 3
        panel = timeline.build(support.create_database(self.root / 'w.db', hourly))
        day0 = [row for row in panel['panel'] if row['day_index'] == 0]
        self.assertTrue(all(row['mask'] == 1 for row in day0))
        self.assertEqual(day0[9]['sessions'], 3)
        self.assertEqual(day0[10]['sessions'], 0)

    def test_weekday_consistency_guard_rejects_mismatch(self):
        hourly = support.flat_days(2)
        panel = timeline.build_panel(
            support.create_database(self.root / 'w.db', hourly, corrupt_weekday_rows=4))
        self.assertEqual(panel['consistency']['mismatched'], 4)
        with self.assertRaises(timeline.TimelineError):
            timeline.verify_consistency(panel)

    def test_unusable_rows_are_dropped(self):
        hourly = support.flat_days(2)
        panel = timeline.build(
            support.create_database(self.root / 'w.db', hourly, unusable_rows=6))
        self.assertEqual(panel['events_total'], 2 * 24 - 6)

    def test_unparsable_timestamp_is_counted(self):
        panel = timeline.build(support.create_database(self.root / 'w.db', support.flat_days(1)))
        self.assertEqual(panel['events_unparsed'], 0)
        self.assertIsNone(timeline.parse_created('not-a-time'))
        self.assertIsNone(timeline.parse_created('0014-13-45 10:00:00'))


if __name__ == '__main__':
    unittest.main()
