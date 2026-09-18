"""基线单测：全局均值、季节 naive 的取值与回退顺序。"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import baselines  # noqa: E402
import features  # noqa: E402

SPEC = features.task_spec('sessions')


def row(mask=1, sessions=2.0, **columns):
    base = dict(mask=mask, sessions=sessions, kwh=sessions * 4,
                lag_1d=None, lag_7d=None, lag_7d_ma=None,
                lag_1d_kwh=None, lag_7d_kwh=None, lag_7d_ma_kwh=None)
    base.update(columns)
    return base


class BaselinesTest(unittest.TestCase):
    def test_global_mean_ignores_unobserved_rows(self):
        rows = [row(sessions=1.0), row(sessions=3.0), row(mask=0, sessions=100.0)]
        fitted = baselines.fit(rows, 'global_mean', SPEC)
        self.assertAlmostEqual(fitted['mean'], 2.0)
        values, fallbacks = baselines.predict(rows, 'global_mean', fitted, SPEC)
        self.assertEqual(values, [2.0, 2.0, 2.0])
        self.assertEqual(fallbacks, 0)

    def test_seasonal_naive_prefers_last_week(self):
        rows = [row(lag_7d=5.0, lag_1d=9.0)]
        values, _ = baselines.predict(rows, 'seasonal_naive', dict(mean=1.0), SPEC)
        self.assertEqual(values, [5.0])

    def test_seasonal_naive_falls_back_step_by_step(self):
        rows = [row(lag_1d=9.0), row(), row(lag_7d=0.0)]
        values, fallbacks = baselines.predict(rows, 'seasonal_naive', dict(mean=1.0), SPEC)
        self.assertEqual(values, [9.0, 1.0, 0.0])   # 有 0 值时不能被当成缺失
        self.assertEqual(fallbacks, 1)

    def test_moving_average_uses_window_mean(self):
        rows = [row(lag_7d_ma=2.5, lag_7d=9.0)]
        values, _ = baselines.predict(rows, 'moving_average', dict(mean=1.0), SPEC)
        self.assertEqual(values, [2.5])

    def test_unknown_method_is_rejected(self):
        with self.assertRaises(ValueError):
            baselines.fit([row()], 'random_forest', SPEC)
        with self.assertRaises(ValueError):
            baselines.predict([row()], 'random_forest', {}, SPEC)


if __name__ == '__main__':
    unittest.main()
