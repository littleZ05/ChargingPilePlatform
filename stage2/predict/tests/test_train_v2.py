"""训练入口单测：任务矩阵、采纳门槛、报告与产物可序列化。"""
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import support

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import baselines  # noqa: E402
import evaluate  # noqa: E402
import features  # noqa: E402
import models  # noqa: E402
import timeline  # noqa: E402
import train_v2  # noqa: E402


class TrainV2Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        hourly = [[1 + ((offset + hour) % 5) if 6 <= hour <= 22 else 0
                   for hour in range(24)] for offset in range(56)]
        hourly[35] = None                      # 一天空白：掩码应当把它排除
        cls.database = support.create_database(cls.root / 'warehouse.db', hourly)
        cls.model = train_v2.train(cls.database, min_train_days=14)
        cls.tasks = {task['column']: task for task in cls.model['tasks']}

    def test_task_matrix_covers_every_base_and_horizon(self):
        expected = {base if hour == 1 else f'{base}_h{hour}'
                    for base in train_v2.BASES for hour in train_v2.HORIZONS}
        self.assertEqual(set(self.tasks), expected)
        self.assertEqual(len(self.model['tasks']), 6)
        for task in self.model['tasks']:
            self.assertEqual(task['base'], task['column'].split('_h')[0])

    def test_selection_uses_early_days_and_report_uses_late_days(self):
        """选择集与报告集按时间切开：选择集全部早于切分点，报告集不早于它。"""
        task = self.tasks['sessions']
        cut = task['selection']['cut_day']
        self.assertGreater(cut, 0)
        self.assertGreater(task['selection']['days'], 0)
        self.assertGreater(task['report']['days'], 0)
        self.assertEqual(task['selection']['days'] + task['report']['days'],
                         task['training']['folds'])
        # 选择集样本量应当明显大于报告集（前 70% / 后 30%）
        selection = task['selection']['metrics'][task['chosen']]
        self.assertGreater(selection['samples'], task['report']['chosen']['samples'])
        # 报告集里同时留了选定方法与最佳基线，便于对照
        self.assertEqual(task['report']['chosen'],
                         task['report']['metrics'][task['chosen']])
        self.assertEqual(task['report']['best_baseline'],
                         task['report']['metrics'][task['baseline_choice']])

    def test_adoption_rule_can_reject_a_weak_model(self):
        """模型只领先 5% 时应当退回基线，领先 20% 时才上线。"""
        weak = self._patched_task(model_gain=0.05)
        strong = self._patched_task(model_gain=0.20)
        self.assertFalse(weak['model_adopted'])
        self.assertIn(weak['chosen'], baselines.METHODS)
        self.assertTrue(strong['model_adopted'])
        self.assertIn(strong['chosen'], models.METHODS)
        self.assertGreaterEqual(strong['selection_advantage'], train_v2.ADOPT_MARGIN)

    def test_chosen_method_matches_adoption_flag(self):
        for task in self.model['tasks']:
            if task['model_adopted']:
                self.assertIn(task['chosen'], models.METHODS)
                self.assertGreaterEqual(task['selection_advantage'],
                                        train_v2.ADOPT_MARGIN - 1e-9)
            else:
                self.assertIn(task['chosen'], baselines.METHODS)
                self.assertIsNone(task['selection_advantage'])
            self.assertIn(task['baseline_choice'], baselines.METHODS)

    def test_every_task_carries_metrics_for_all_candidates(self):
        candidates = set(baselines.METHODS + models.METHODS)
        for task in self.model['tasks']:
            self.assertEqual(set(task['all_metrics']), candidates)
            self.assertEqual(set(task['labels']), candidates)
            self.assertGreater(task['training']['rows'], 0)
            self.assertGreater(task['training']['folds'], 0)
            self.assertTrue(task['top_weights'])

    def test_model_payload_is_json_serializable(self):
        payload = json.dumps(self.model, ensure_ascii=False)
        self.assertIn('tasks', json.loads(payload))

    def test_sessions_section_can_be_included_or_skipped(self):
        self.assertIn('sessions', self.model)
        section = self.model['sessions']
        self.assertEqual(len(section['tasks']), 6)          # 2 目标 × 3 分位数
        self.assertEqual(set(section['deployment']), {'duration_hours', 'kwh'})
        without = train_v2.train(self.database, min_train_days=14, with_sessions=False)
        self.assertNotIn('sessions', without)

    def test_stations_section_is_included_and_can_be_skipped(self):
        section = self.model.get('stations')
        self.assertIsNotNone(section)
        self.assertEqual(section['stations'], section['clustered_stations']
                         + len(section['sparse_stations']))
        self.assertGreaterEqual(section['cluster_count'], 2)
        without = train_v2.train(self.database, min_train_days=14, with_stations=False)
        self.assertNotIn('stations', without)

    def test_forecast_is_baked_for_every_task(self):
        forecast = self.model.get('forecast')
        self.assertTrue(forecast, self.model.get('forecast_error'))
        combos = {(item['base'], item['horizon']) for item in forecast}
        self.assertEqual(combos, {(base, hour) for base in train_v2.BASES
                                  for hour in train_v2.HORIZONS})
        for item in forecast:
            self.assertEqual(len(item['hours']), 24)
            self.assertTrue(item['method'])
            self.assertTrue(all(point['predicted'] is None or point['predicted'] >= 0
                                for point in item['hours']))

    def test_report_is_written_without_informal_wording(self):
        path = self.root / '预测评估_v2.md'
        train_v2.build_report(self.model, path)
        text = path.read_text(encoding='utf-8')
        for section in ('# 第二阶段预测评估（v2）', '选定方法在报告集上的表现',
                        '全部候选方法', '模型可解释性', '单次充电分位数',
                        '站点画像聚类与繁忙度分档', '限制'):
            self.assertIn(section, text)
        self.assertIn(f'{int(train_v2.ADOPT_MARGIN * 100)}%', text)   # 采纳门槛写明
        self.assertNotIn('口径', text)
        self.assertNotIn('老师', text)

    def _patched_task(self, model_gain):
        """用假预测替掉滚动评估：基线 MAE=2.0，模型 MAE=2.0*(1-gain)。"""
        rows = features.attach_lags(timeline.build(self.database))

        def fake_walk_forward(rows, method, column='sessions', min_train_days=28,
                              start_day=None, end_day=None, alpha=models.DEFAULT_ALPHA):
            gap = 2.0 * (1 - model_gain) if method in models.METHODS else 2.0
            records = [dict(day_index=day, hour=hour, actual=10.0,
                            predicted=10.0 + gap)
                       for day in range(20) for hour in range(24)]
            return dict(method=method, label=evaluate.label_of(method), target=column,
                        base='sessions', horizon=1, folds=20, fallbacks=0,
                        metrics=evaluate.metrics([r['actual'] for r in records],
                                                 [r['predicted'] for r in records]),
                        by_hour={}, by_day_type={}, predictions=records)

        with mock.patch.object(train_v2.evaluate, 'walk_forward', fake_walk_forward):
            return train_v2.train_task(rows, 'sessions', 1, min_train_days=5)


if __name__ == '__main__':
    unittest.main()
