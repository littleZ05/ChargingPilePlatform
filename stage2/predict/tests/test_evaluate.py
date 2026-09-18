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
        methods = [result['method'] for result in evaluation['results']]
        self.assertEqual(methods[:3], evaluate.baselines.METHODS)   # 基线在前
        self.assertIn('ridge', methods)                             # 模型在后
        best = min(result['metrics']['mae'] for result in evaluation['results']
                   if result['method'] in evaluate.baselines.METHODS)
        self.assertAlmostEqual(evaluation['best_baseline_mae'], best)

    def test_report_is_written(self):
        evaluation = evaluate.compare(self.build_rows(), min_train_days=5)
        path = self.root / 'report.md'
        evaluate.build_report(evaluation, path, note='测试说明')
        text = path.read_text(encoding='utf-8')
        self.assertIn('扩展窗口滚动前进', text)
        self.assertIn('按小时分层', text)
        self.assertIn('测试说明', text)

    def test_selection_split_keeps_report_days_out_of_selection(self):
        rows = self.build_rows(days=20)
        results = [evaluate.walk_forward(rows, method, min_train_days=5)
                   for method in evaluate.baselines.METHODS + evaluate.models.METHODS]
        split = evaluate.selection_split(results, share=0.7)

        self.assertEqual(split['selection_days'], 10)
        self.assertEqual(split['report_days'], 5)
        self.assertEqual(split['selection_days'] + split['report_days'], 15)   # 第 5..19 天受测
        for result in results:
            early = [record for record in result['predictions']
                     if record['day_index'] < split['cut_day']]
            expected = evaluate.metrics([r['actual'] for r in early],
                                        [r['predicted'] for r in early])
            self.assertEqual(split['selection'][result['method']], expected)
        # 切分点之后的日子只出现在报告集
        late = [record for record in results[0]['predictions']
                if record['day_index'] >= split['cut_day']]
        self.assertTrue(all(record['day_index'] >= split['cut_day'] for record in late))
        self.assertGreater(late[0]['day_index'], split['cut_day'] - 1)

    def test_selection_split_rejects_tiny_test_set(self):
        rows = self.build_rows(days=6)
        results = [evaluate.walk_forward(rows, 'global_mean', min_train_days=5)]
        with self.assertRaises(ValueError):
            evaluate.selection_split(results)

    def test_pick_returns_none_when_nothing_is_scored(self):
        self.assertIsNone(evaluate.pick({}, ['a', 'b']))
        self.assertIsNone(evaluate.pick({'a': {'mae': None}}, ['a']))

    def test_pick_chooses_lowest_metric(self):
        split = {'a': {'mae': 3.0}, 'b': {'mae': 1.5}, 'c': {'mae': None}}
        self.assertEqual(evaluate.pick(split, ['a', 'b', 'c']), 'b')
        self.assertEqual(evaluate.pick(split, ['c', 'a']), 'a')
        self.assertEqual(evaluate.pick(split, ['a', 'b'], key='mae'), 'b')


if __name__ == '__main__':
    unittest.main()
