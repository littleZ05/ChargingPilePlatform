"""会话分类单测：指标、阈值、滚动评估、采纳规则与报告。"""
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

import support

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import classification  # noqa: E402
import sessions  # noqa: E402


def add_signal(path, sql):
    connection = sqlite3.connect(path)
    connection.executescript(sql)
    connection.commit()
    connection.close()
    return path


class MetricTest(unittest.TestCase):
    def test_ranking_metrics_have_expected_values(self):
        self.assertEqual(classification.roc_auc([0, 0, 1, 1], [0.1, 0.2, 0.3, 0.4]), 1.0)
        self.assertEqual(classification.roc_auc([0, 0, 1, 1], [0.4, 0.3, 0.2, 0.1]), 0.0)
        self.assertAlmostEqual(classification.roc_auc([0, 1], [0.5, 0.5]), 0.5)
        self.assertIsNone(classification.roc_auc([1, 1], [0.5, 0.6]))
        self.assertEqual(classification.average_precision([0, 0, 1, 1], [0.1, 0.2, 0.9, 0.8]), 1.0)
        self.assertIsNone(classification.average_precision([0, 0], [0.1, 0.2]))
        self.assertAlmostEqual(classification.brier([0, 1], [0.0, 1.0]), 0.0)

    def test_metrics_carry_confusion_and_threshold(self):
        stats = classification.metrics([0, 0, 1, 1], [0.1, 0.6, 0.7, 0.2], 0.5)
        self.assertEqual(stats['confusion'], {'tp': 1, 'fp': 1, 'tn': 1, 'fn': 1})
        self.assertEqual(stats['precision'], 0.5)
        self.assertEqual(stats['recall'], 0.5)
        self.assertEqual(stats['f1'], 0.5)

    def test_best_threshold_separates_the_classes(self):
        threshold = classification.best_threshold([0, 0, 1, 1], [0.1, 0.2, 0.8, 0.9])
        self.assertTrue(0.2 < threshold <= 0.8)

    def test_pooled_metrics_use_each_blocks_threshold(self):
        stats = classification.pooled_metrics([0, 1, 0, 1], [0.4, 0.6, 0.4, 0.6], [0.5, 0.5, 0.4, 0.4])
        # 第三条用了自己那一折的阈值 0.4，所以被判成正类：这正是"逐折选阈值"与"全局 0.5"的区别
        self.assertEqual(stats['confusion'], {'tp': 2, 'fp': 1, 'tn': 1, 'fn': 0})
        self.assertGreater(stats['recall'], stats['precision'])
        self.assertEqual(stats['samples'], 4)


class ClassificationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        # 场景一：长时长只跟开始时段有关，设施与时段查表里没有这个信息 → 树应该被采纳
        cls.hour_database = support.create_database(root / 'hour.db', support.flat_days(70))
        add_signal(cls.hour_database,
                   "update dwd_sessions set duration_hours='2.0' ; "
                   "update dwd_sessions set duration_hours='6.0' where start_hour between 8 and 15")
        # 场景二：长时长完全由设施类型决定，经验查表已经能完美预测 → 树达不到 10% 门槛
        cls.facility_database = support.create_database(root / 'facility.db', support.flat_days(70))
        add_signal(cls.facility_database,
                   "update dwd_sessions set duration_hours='2.0', facility_label='交流' ; "
                   "update dwd_sessions set duration_hours='6.0', facility_label='直流' "
                   "where start_hour % 2 = 0")
        cls.hour_section = classification.train(cls.hour_database)
        cls.facility_section = classification.train(cls.facility_database)

    def test_tree_is_adopted_when_the_signal_is_not_in_the_lookup_table(self):
        section = self.hour_section
        self.assertTrue(section['model_adopted'])
        self.assertEqual(section['chosen'], 'cart')
        self.assertGreater(section['f1_advantage'], classification.ADOPT_MARGIN)
        self.assertTrue(section['ranking_not_worse'])

    def test_lookup_table_wins_when_it_already_covers_the_signal(self):
        section = self.facility_section
        self.assertFalse(section['model_adopted'])
        self.assertEqual(section['chosen'], section['baseline_choice'])

    def test_rolling_evaluation_covers_every_method_and_block(self):
        section = self.hour_section
        self.assertTrue(section['blocks'])
        self.assertEqual(len(section['fold_thresholds']), len(section['blocks']))
        self.assertEqual(section['test_sessions'],
                         sum(block['sessions'] for block in section['blocks']))
        for method in classification.METHODS:
            stats = section['metrics'][method]
            self.assertEqual(stats['samples'], section['test_sessions'])
            self.assertIsNotNone(stats['average_precision'])
            self.assertGreaterEqual(stats['roc_auc'], 0.0)
            self.assertLessEqual(stats['roc_auc'], 1.0)
        self.assertEqual(section['block_wins'] <= section['block_total'], True)

    def test_deployment_tree_carries_rules_calibration_and_limits(self):
        deployment = self.hour_section['deployment']
        self.assertGreater(deployment['leaves'], 0)
        self.assertTrue(deployment['features'])
        self.assertTrue(deployment['rules'])
        for rule in deployment['rules']:
            self.assertTrue(rule['conditions_text'])
            self.assertTrue(rule['translation'])
            self.assertGreater(rule['samples'], 0)
        self.assertTrue(deployment['calibration'])
        for row in deployment['calibration']:
            self.assertGreater(row['samples'], 0)
            self.assertGreaterEqual(row['observed_rate'], 0.0)
            self.assertLessEqual(row['observed_rate'], 1.0)
        self.assertTrue(deployment['importance'])
        self.assertTrue(self.hour_section['limits'])

    def test_deployment_carries_the_lookup_tables_the_service_needs(self):
        """服务端不回查明细表，所以类别、站点统计与分段比例必须随产物一起存下来。"""
        section = self.facility_section
        deployment = section['deployment']
        self.assertEqual(deployment['categories'], section['categories'])
        self.assertEqual(deployment['statistics'], section['statistics'])
        baseline = deployment['baseline']
        self.assertEqual(baseline['method'], section['baseline_choice'])
        self.assertIn(tuple(baseline['keys']),
                      (('facility_label',), ('facility_label', 'time_period')))
        self.assertTrue(baseline['table'])
        # 键必须是 JSON 能存的字符串：元组键在 json.dumps 里会变成数组，服务端查不到
        for key, value in baseline['table'].items():
            self.assertIsInstance(key, str)
            self.assertEqual(len(key.split('|')), len(baseline['keys']))
            self.assertGreaterEqual(value, 0.0)
            self.assertLessEqual(value, 1.0)
        self.assertEqual(set(baseline['table']), set(baseline['counts']))
        self.assertEqual(sum(baseline['counts'].values()), deployment['train_sessions'])

    def test_label_threshold_comes_from_the_requested_quantile(self):
        section = self.hour_section
        self.assertAlmostEqual(section['threshold_hours'], 6.0)
        self.assertEqual(section['quantile'], classification.DEFAULT_QUANTILE)
        self.assertIn('P75', section['threshold_source'])

    def test_report_is_written_with_metrics_and_rules(self):
        path = Path(self.temp.name) / 'predict-report.md'
        text = classification.build_report(self.hour_section, path)
        self.assertEqual(path.read_text(encoding='utf-8'), text)
        for fragment in ('会话分类树', '平均精度', 'CART 分类树', '概率校准', '使用方式与限制'):
            self.assertIn(fragment, text)
        self.assertNotIn('口径', text)

    def test_small_dataset_is_rejected(self):
        path = support.create_database(Path(self.temp.name) / 'tiny.db', support.flat_days(1))
        with self.assertRaises(ValueError):
            classification.train(path)

    def test_design_matrix_carries_the_occupancy_columns(self):
        rows, _ = sessions.load_sessions(self.hour_database)
        categories = sessions.categories_from(rows)
        names, matrix, statistics = classification.design(rows, categories)
        for name in classification.OCCUPANCY_FEATURES:
            self.assertIn(name, names)
        self.assertTrue(all(len(vector) == len(names) for vector in matrix))
        self.assertEqual(statistics['target'], 'duration_hours')

    def test_deployment_carries_the_occupancy_lookup_with_fallbacks(self):
        lookup = self.hour_section['deployment']['occupancy']
        self.assertTrue(lookup['by_station_hour'])
        self.assertTrue(lookup['by_hour'])
        self.assertGreater(lookup['overall']['samples'], 0)
        for entry in list(lookup['by_station_hour'].values())[:5]:
            self.assertGreater(entry['samples'], 0)
            for key in ('recent', 'active', 'day'):
                self.assertGreaterEqual(entry[key], 0.0)
        self.assertIn('没有实时会话流', lookup['source'])
        # 站点×小时 的样本数合计等于训练会话数：每条会话都落在自己的桶里
        self.assertEqual(sum(e['samples'] for e in lookup['by_station_hour'].values()),
                         self.hour_section['deployment']['train_sessions'])


def reference(**overrides):
    """构造一条只带占用特征所需字段的会话（时长估计直接给，不经过统计）。"""
    row = dict(day_index=0, hour=9, station_id='S1', station_median=3.0)
    row.update(overrides)
    return row


class OccupancyTest(unittest.TestCase):
    def test_counts_only_look_backwards(self):
        rows = [reference(hour=8), reference(hour=9), reference(hour=10)]
        counts = classification.occupancy_stream(rows)
        self.assertEqual([c['station_recent'] for c in counts], [0.0, 1.0, 2.0])
        # 估计时长 3 小时，前两单在 10 点都还算在充
        self.assertEqual([c['station_active'] for c in counts], [0.0, 1.0, 2.0])
        self.assertEqual([c['station_day'] for c in counts], [0.0, 1.0, 2.0])

    def test_later_sessions_cannot_change_earlier_features(self):
        rows = [reference(hour=8), reference(hour=9), reference(hour=10)]
        first_two = classification.occupancy_stream(rows[:2])
        all_three = classification.occupancy_stream(rows)
        self.assertEqual(first_two, all_three[:2])
        # 顺序打乱也不能改变结果：特征只取决于"更早开始的会话"
        shuffled = classification.occupancy_stream([rows[2], rows[0], rows[1]])
        self.assertEqual([c['station_recent'] for c in shuffled], [2.0, 0.0, 1.0])

    def test_other_stations_and_distant_past_do_not_count(self):
        rows = [reference(hour=8), reference(hour=9, station_id='S2'),
                reference(day_index=5, hour=8)]
        counts = classification.occupancy_stream(rows)
        self.assertEqual(counts[1]['station_recent'], 0.0)
        self.assertEqual(counts[1]['station_active'], 0.0)
        self.assertEqual(counts[2]['station_recent'], 0.0)     # 5 天以外
        self.assertEqual(counts[2]['station_active'], 0.0)
        self.assertEqual(counts[2]['station_day'], 0.0)

    def test_prefix_lets_a_test_block_see_the_training_window(self):
        rows = [reference(hour=8), reference(hour=9)]
        prefix = classification.occupancy_history([dict(rows[0])])
        counts = classification.occupancy_stream(rows[1:], prefix)
        self.assertEqual(counts[0]['station_recent'], 1.0)
        self.assertEqual(counts[0]['station_active'], 1.0)



if __name__ == '__main__':
    unittest.main()
