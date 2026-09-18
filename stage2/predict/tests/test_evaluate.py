"""评估单测：指标定义、滚动前进不偷看未来、报告可生成。"""
import sys
import tempfile
import unittest
from pathlib import Path

import support

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import evaluate  # noqa: E402
import features  # noqa: E402
import timeline  # noqa: E402


class EvaluateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_metrics_skip_missing_predictions(self):
        stats = evaluate.metrics([2.0, 4.0, 6.0], [1.0, None, 9.0])
        self.assertEqual(stats['samples'], 2)
        self.assertAlmostEqual(stats['mae'], 2.0)      # 只算 (2,1) 与 (6,9) 两条
        self.assertEqual(stats['mean_actual'], 4.0)

    def test_mape_ignores_zero_actuals(self):
        self.assertIsNone(evaluate.mape([0.0, 0.0], [1.0, 2.0]))
        self.assertAlmostEqual(evaluate.mape([2.0], [1.0]), 0.5)

    def test_skill_score_direction(self):
        self.assertAlmostEqual(evaluate.skill_score(5.0, 10.0), 0.5)
        self.assertAlmostEqual(evaluate.skill_score(10.0, 10.0), 0.0)
        self.assertLess(evaluate.skill_score(15.0, 10.0), 0.0)
        self.assertIsNone(evaluate.skill_score(None, 10.0))

    def build_rows(self, days=20, sessions=2, missing_day=None):
        hourly = support.flat_days(days, sessions_per_hour=sessions)
        if missing_day is not None:
            hourly[missing_day] = None
        node = support.create_database(self.root / f'w{days}-{missing_day}.db', hourly)
        return features.attach_lags(timeline.build(node))

    def test_walk_forward_never_uses_future_rows(self):
        rows = self.build_rows()
        before = evaluate.walk_forward(rows, 'global_mean', min_train_days=5)
        pivot = 12
        mutated = [dict(row) for row in rows]
        for row in mutated:
            if row['day_index'] >= pivot:
                row['sessions'] = row['sessions'] * 5 + 100
        after = evaluate.walk_forward(mutated, 'global_mean', min_train_days=5)

        past_before = [r for r in before['predictions'] if r['day_index'] < pivot]
        past_after = [r for r in after['predictions'] if r['day_index'] < pivot]
        self.assertEqual(past_before, past_after)
        self.assertNotEqual(before['metrics']['mae'], after['metrics']['mae'])

    def test_walk_forward_skips_missing_days(self):
        rows = self.build_rows(days=12, missing_day=8)
        result = evaluate.walk_forward(rows, 'moving_average', min_train_days=5)
        tested = {record['day_index'] for record in result['predictions']}
        self.assertNotIn(8, tested)
        self.assertEqual(result['folds'], len(tested))

    def test_compare_marks_best_baseline(self):
        rows = self.build_rows()
        evaluation = evaluate.compare(rows, min_train_days=5)
        self.assertIn(evaluation['best_baseline'], evaluate.baselines.METHODS)
        self.assertEqual(len(evaluation['results']), 3)
        best = min(result['metrics']['mae'] for result in evaluation['results'])
        self.assertAlmostEqual(evaluation['best_baseline_mae'], best)

    def test_report_is_written(self):
        evaluation = evaluate.compare(self.build_rows(), min_train_days=5)
        path = self.root / 'report.md'
        evaluate.build_report(evaluation, path, note='测试说明')
        text = path.read_text(encoding='utf-8')
        self.assertIn('扩展窗口滚动前进', text)
        self.assertIn('按小时分层', text)
        self.assertIn('测试说明', text)


if __name__ == '__main__':
    unittest.main()
