"""模型单测：设计矩阵、增量拟合与批量拟合一致、残差模型、无历史时不预测。"""
import sys
import tempfile
import unittest
from pathlib import Path

import support

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import features  # noqa: E402
import models  # noqa: E402
import timeline  # noqa: E402


class ModelsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def rows(self, days=30, base='sessions'):
        hourly = [[(offset % 7) + 1] * 24 for offset in range(days)]
        panel = timeline.build(support.create_database(self.root / f'w{days}.db', hourly))
        return features.attach_lags(panel)

    def test_solve_recovers_known_system(self):
        solution = models.solve([[2.0, 1.0], [1.0, 3.0]], [3.0, 5.0])
        self.assertAlmostEqual(solution[0], 0.8, places=6)
        self.assertAlmostEqual(solution[1], 1.4, places=6)

    def test_design_row_is_deterministic(self):
        spec = features.task_spec('sessions')
        row = dict(self.rows(10)[24 * 3], **{})
        names_a, values_a = models.design_row(row, spec)
        names_b, values_b = models.design_row(row, spec)
        self.assertEqual(names_a, names_b)
        self.assertEqual(values_a, values_b)
        self.assertIn('hour=9', names_a)
        self.assertIn('lag_7d', names_a)
        self.assertEqual(len(names_a), 23 + 6 + 1 + 8)

    def test_incremental_fit_matches_batch_fit(self):
        rows = self.rows(30)
        spec = features.task_spec('sessions')
        batch = models.fit(rows, 'ridge', spec, alpha=1.0)
        accumulator = models.ExpandingRidge(spec, alpha=1.0)
        accumulator.add(rows)
        incremental = accumulator.fit()
        self.assertEqual(batch['training_rows'], incremental['training_rows'])
        for left, right in zip(batch['weights'], incremental['weights']):
            self.assertAlmostEqual(left, right, places=6)

    def test_prediction_is_never_negative(self):
        rows = self.rows(20)
        spec = features.task_spec('sessions')
        model = models.fit(rows, 'ridge', spec, alpha=1.0)
        values, _ = models.predict(model, rows[i] if False else rows, spec)
        self.assertTrue(all(value is None or value >= 0 for value in values))

    def test_rows_without_history_are_not_predicted(self):
        rows = self.rows(10)
        spec = features.task_spec('sessions')
        model = models.fit(rows, 'ridge', spec)
        first_day = [row for row in rows if row['day_index'] == 0]
        values, missing = models.predict(model, first_day, spec)
        self.assertEqual(missing, len(first_day))
        self.assertTrue(all(value is None for value in values))

    def test_residual_model_adds_correction_on_top_of_baseline(self):
        rows = self.rows(30)
        spec = features.task_spec('sessions')
        pure = models.fit(rows, 'ridge', spec, alpha=1.0)
        residual = models.fit(rows, 'ridge_residual', spec, alpha=1.0)
        self.assertTrue(residual['residual'])
        self.assertFalse(pure['residual'])
        target = [row for row in rows if row['day_index'] == 20]
        pure_values, _ = models.predict(pure, target, spec)
        residual_values, _ = models.predict(residual, target, spec)
        self.assertNotEqual(pure_values, residual_values)
        self.assertLess(abs(residual['intercept']), abs(pure['intercept']))

    def test_top_weights_sorted_by_magnitude(self):
        rows = self.rows(30)
        model = models.fit(rows, 'ridge', features.task_spec('sessions'))
        weights = [abs(item['weight']) for item in models.top_weights(model, 5)]
        self.assertEqual(weights, sorted(weights, reverse=True))


if __name__ == '__main__':
    unittest.main()
