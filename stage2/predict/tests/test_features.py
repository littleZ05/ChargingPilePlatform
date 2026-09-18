"""特征单测：滞后取值、缺测不补零、目标编码只用历史、未来篡改不改变过去特征。"""
import sys
import tempfile
import unittest
from pathlib import Path

import support

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import features  # noqa: E402
import timeline  # noqa: E402


class FeaturesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def panel(self, hourly):
        return timeline.build(support.create_database(self.root / 'w.db', hourly))

    def test_lag_values_come_from_earlier_days(self):
        # 第 d 天每小时会话数 = d+1
        hourly = [[offset + 1] * 24 for offset in range(10)]
        rows = features.attach_lags(self.panel(hourly))
        row = next(r for r in rows if r['day_index'] == 5 and r['hour'] == 3)
        self.assertEqual(row['sessions'], 6)
        self.assertEqual(row['lag_1d'], 5)
        self.assertIsNone(row['lag_7d'])          # 第 -2 天不存在
        self.assertEqual(row['obs_28d'], 5)
        self.assertEqual(row['prev_day_total'], 5 * 24)
        self.assertEqual(row['roll_7d_total'], sum(range(1, 6)) / 5 * 24)

    def test_missing_day_is_not_treated_as_zero(self):
        hourly = [[3] * 24, [3] * 24, [3] * 24, [3] * 24, None, [9] * 24]
        rows = features.attach_lags(self.panel(hourly))
        row = next(r for r in rows if r['day_index'] == 5 and r['hour'] == 0)
        self.assertIsNone(row['lag_1d'])          # 前一天缺测 → 没有昨日可用
        self.assertEqual(row['obs_28d'], 4)       # 只有 4 天有采集
        self.assertIsNone(row['prev_day_total'])
        self.assertEqual(row['lag_7d_ma'], 3.0)   # 均值只统计有采集的天

    def test_backward_only_guard_passes(self):
        hourly = [[offset + 1] * 24 for offset in range(12)]
        panel = self.panel(hourly)
        self.assertEqual(features.verify_backward_only(panel), [])

    def test_target_encode_uses_strictly_earlier_rows(self):
        rows = [dict(day_index=day, hour=0, facility_label='直流', sessions=value, mask=1)
                for day, value in enumerate([1, 2, 3, 4])]
        encoded = features.expanding_target_encode(rows, 'facility_label', 'sessions',
                                                   prior_weight=0.0)
        self.assertEqual(encoded, [0.0, 1.0, 1.5, 2.0])

    def test_target_encode_shrinks_to_prior(self):
        # 第 0 天是另一组且为 0，第 1、2 天同组且为 20：
        # 第 2 天的编码应落在全局先验 10 与该组均值 20 之间，体现向先验收缩。
        rows = [dict(day_index=0, hour=0, facility_label='交流', sessions=0.0, mask=1),
                dict(day_index=1, hour=0, facility_label='直流', sessions=20.0, mask=1),
                dict(day_index=2, hour=0, facility_label='直流', sessions=20.0, mask=1)]
        encoded = features.expanding_target_encode(rows, 'facility_label', 'sessions',
                                                   prior_weight=5.0)
        self.assertEqual(encoded[0], 0.0)              # 第一条没有历史，也没有先验
        self.assertGreater(encoded[2], 10.0)           # 高于全局先验
        self.assertLess(encoded[2], 20.0)              # 低于本组均值 → 已收缩

    def test_usable_rows_require_all_columns(self):
        hourly = [[2] * 24 for _ in range(3)] + [None] + [[2] * 24 for _ in range(8)]
        rows = features.attach_lags(self.panel(hourly))
        usable = features.usable_rows(rows)
        self.assertTrue(all(row['mask'] == 1 for row in usable))
        # 第 7/8/9/11 天同时具备昨日与上周同日；第 10 天因为 d-7 恰好缺测被排除
        self.assertEqual(sorted({row['day_index'] for row in usable}), [7, 8, 9, 11])


if __name__ == '__main__':
    unittest.main()
