"""服务层单测：分类查表/走树、概率曲线、分段表，以及"只读模型 JSON"的约定。

分类结果落库与大屏接口都建立在这些纯函数上，因此这里重点验证两件事：
上线方法的概率必须与产物里写明的那一支一致；产物经过 JSON 往返（元组键变成字符串）后
服务端算出来的概率一个也不许变。
"""
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

import support

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import classification  # noqa: E402
import serving  # noqa: E402


def add_signal(path, sql):
    connection = sqlite3.connect(path)
    connection.executescript(sql)
    connection.commit()
    connection.close()
    return path


class ClassificationServingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        # 树被采纳的场景：长时长只跟开始时段有关，经验查表看不到这个信息
        database = support.create_database(root / 'hour.db', support.flat_days(70))
        add_signal(database,
                   "update dwd_sessions set duration_hours='2.0' ; "
                   "update dwd_sessions set duration_hours='6.0' where start_hour between 8 and 15")
        cls.tree_section = classification.train(database)
        # 经验查表已经覆盖信号的场景：树达不到门槛，上线查表
        database = support.create_database(root / 'facility.db', support.flat_days(70))
        add_signal(database,
                   "update dwd_sessions set duration_hours='2.0', facility_label='交流' ; "
                   "update dwd_sessions set duration_hours='6.0', facility_label='直流' "
                   "where start_hour % 2 = 0")
        cls.rule_section = classification.train(database)

    def first(self, section):
        categories = section['categories']
        return categories['facility_label'][0], categories['time_period'][0]

    def test_lookup_follows_the_method_that_is_actually_online(self):
        facility, period = self.first(self.tree_section)
        detail = serving.classification_detail(self.tree_section, facility, period, 9)
        self.assertEqual(detail['method'], 'cart')
        self.assertEqual(detail['probability'], detail['tree_rate'])
        self.assertNotEqual(detail['rule_rate'], detail['tree_rate'])
        facility, period = self.first(self.rule_section)
        detail = serving.classification_detail(self.rule_section, facility, period, 9)
        self.assertNotEqual(detail['method'], 'cart')
        self.assertEqual(detail['probability'], detail['rule_rate'])

    def test_decision_uses_the_stored_threshold(self):
        facility, period = self.first(self.rule_section)
        threshold = self.rule_section['deployment']['decision_threshold']
        for hour in (0, 9, 23):
            detail = serving.classification_detail(self.rule_section, facility, period, hour)
            self.assertEqual(detail['threshold'], threshold)
            self.assertEqual(detail['decision'], int(detail['probability'] >= threshold))
            self.assertTrue(detail['decision_label'])

    def test_curve_covers_24_hours_and_keeps_both_arms(self):
        facility, period = self.first(self.tree_section)
        curve = serving.classification_curve(self.tree_section, facility, period)
        self.assertEqual([row['hour'] for row in curve], list(range(24)))
        tree_rates = {row['tree_rate'] for row in curve}
        rule_rates = {row['rule_rate'] for row in curve}
        self.assertGreater(len(tree_rates), 1)          # 树按时段给出分辨率
        self.assertEqual(len(rule_rates), 1)            # 查表在同一设施类型与时段内是常数
        facility, period = self.first(self.rule_section)
        flat = serving.classification_curve(self.rule_section, facility, period)
        self.assertEqual(len({row['rule_rate'] for row in flat}), 1)

    def test_segments_carry_samples_from_the_trained_window(self):
        segments = serving.classification_segments(self.rule_section)
        self.assertTrue(segments)
        self.assertEqual(sum(row['sessions'] for row in segments),
                         self.rule_section['deployment']['train_sessions'])
        for row in segments:
            self.assertGreater(row['sessions'], 0)
            self.assertGreaterEqual(row['rate'], 0.0)
            self.assertLessEqual(row['rate'], 1.0)

    def test_leaf_reports_the_path_conditions_when_the_tree_splits(self):
        facility, period = self.first(self.tree_section)
        detail = serving.classification_detail(self.tree_section, facility, period, 9)
        leaf = detail['leaf']
        self.assertEqual(leaf['probability'], detail['tree_rate'])
        self.assertGreater(leaf['samples'], 0)
        self.assertLessEqual(leaf['positives'], leaf['samples'])
        self.assertTrue(leaf['conditions_text'])
        early = serving.classification_detail(self.tree_section, facility, period, 9)['leaf']
        late = serving.classification_detail(self.tree_section, facility, period, 23)['leaf']
        self.assertNotEqual(early['conditions_text'], late['conditions_text'])

    def test_calibration_bucket_contains_the_probability(self):
        facility, period = self.first(self.tree_section)
        detail = serving.classification_detail(self.tree_section, facility, period, 9)
        bucket = detail['calibration_bucket']
        self.assertIsNotNone(bucket)
        self.assertGreater(bucket['samples'], 0)
        self.assertTrue(bucket['covers'])

    def test_station_features_come_from_the_stored_statistics(self):
        statistics = self.rule_section['statistics']
        station = next(iter(statistics['counts']))
        facility, period = self.first(self.rule_section)
        detail = serving.classification_detail(self.rule_section, facility, period, 9,
                                               station=station)
        self.assertGreater(detail['station_count'], 0)
        self.assertGreater(detail['station_median'], 0)

    def test_payload_survives_a_json_round_trip(self):
        """服务端只拿到 JSON：元组键变成字符串后，概率必须一个都不变。"""
        facility, period = self.first(self.tree_section)
        before = serving.classification_detail(self.tree_section, facility, period, 9)
        restored = json.loads(json.dumps(self.tree_section, ensure_ascii=False))
        after = serving.classification_detail(restored, facility, period, 9)
        self.assertEqual(before, after)

    def test_payload_is_self_contained_and_json_serializable(self):
        facility, period = self.first(self.rule_section)
        payload = serving.classification_payload(self.rule_section, facility, period, 9)
        for key in ('label', 'threshold_hours', 'chosen', 'methods', 'blocks', 'rules',
                    'calibration', 'importance', 'limits', 'lookup', 'curve', 'segments', 'note'):
            self.assertIn(key, payload)
        self.assertEqual(len(payload['methods']), len(classification.METHODS))
        self.assertIn(payload['lookup']['decision_label'], ('提前提示', '不需要额外动作'))
        self.assertIn('测试块', payload['note'])
        self.assertEqual(json.loads(json.dumps(payload, ensure_ascii=False))['segments'],
                         payload['segments'])
        self.assertNotIn('口径', json.dumps(payload, ensure_ascii=False))

    def test_unknown_station_falls_back_to_the_global_statistics(self):
        """没见过的站点（station_count=0）必须退让到全局值，而不是报错或造数。"""
        facility, period = self.first(self.rule_section)
        detail = serving.classification_detail(self.rule_section, facility, period, 9,
                                               station='S-不存在')
        self.assertEqual(detail['station_count'], 0)
        self.assertIsNotNone(detail['probability'])

    def test_occupancy_lookup_falls_back_and_reports_its_source(self):
        """服务端没有实时会话流：占用只能查"站点×小时 → 该小时 → 整体"的历史平均。"""
        lookup = self.rule_section['deployment']['occupancy']
        facility, period = self.first(self.rule_section)
        station, hour = next(iter(lookup['by_station_hour'])).split('|')
        known = serving.classification_detail(self.rule_section, facility, period, int(hour),
                                              station=station)
        self.assertGreater(known['occupancy']['samples'], 0)
        self.assertIn('平均占用', known['occupancy']['source'])
        hourly = lookup['by_hour'][str(int(hour))]
        unknown = serving.classification_detail(self.rule_section, facility, period, int(hour),
                                                station='S-不存在')
        self.assertEqual(unknown['occupancy']['samples'], hourly['samples'])
        self.assertEqual(unknown['occupancy']['day'], hourly['day'])


if __name__ == '__main__':
    unittest.main()
