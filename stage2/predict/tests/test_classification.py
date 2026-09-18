"""会话分类单测：指标、阈值、滚动评估、采纳规则与报告。"""
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

import support

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import classification  # noqa: E402


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


if __name__ == '__main__':
    unittest.main()
