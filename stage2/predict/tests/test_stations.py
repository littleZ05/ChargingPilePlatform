"""站点画像单测：特征汇总、轮廓系数定 k、繁忙度分档与稀疏站点处理。"""
import datetime
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import stations  # noqa: E402

SCHEMA = ('create table dwd_sessions (created_raw text, weekday text, start_hour integer, '
          'kwh text, duration_hours text, station_id text, facility_label text, '
          'time_period text, platform text, manager_vehicle text, '
          'time_of_day_usable text, weekday_usable text)')
ORIGIN = datetime.date(15, 6, 1)
WEEKDAYS = ('Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun')


def build_database(path, specs, days=40):
    """按站点规格造数据：per_day 每天几单、facility 设施类型、stride 每隔几天有单。"""
    connection = sqlite3.connect(path)
    connection.execute(SCHEMA)
    rows = []
    for offset in range(days):
        day = ORIGIN + datetime.timedelta(days=offset)
        for spec in specs:
            if offset % spec.get('stride', 1):
                continue
            for index in range(spec['per_day']):
                hour = spec['hours'][index % len(spec['hours'])]
                rows.append((f'{day.isoformat()} {hour:02d}:15:00', WEEKDAYS[day.weekday()],
                             hour, f"{spec['kwh']:.2f}", f"{spec['duration']:.2f}",
                             spec['station'], spec['facility'],
                             'peak' if 8 <= hour < 12 else 'normal', 'ios', '0', '1', '1'))
    connection.executemany('insert into dwd_sessions values (?,?,?,?,?,?,?,?,?,?,?,?)', rows)
    connection.commit()
    connection.close()
    return path


def two_group_specs():
    specs = []
    for index in range(3):                       # 高频直流站：短时长、大电量
        specs.append(dict(station=f'DC{index}', per_day=3, facility='直流',
                          duration=1.5, kwh=8.0, hours=[8, 12, 18]))
    for index in range(3):                       # 低频交流站：长时长、小电量
        specs.append(dict(station=f'AC{index}', per_day=1, facility='交流',
                          duration=5.0, kwh=3.0, hours=[20]))
    for index in range(2):                       # 稀疏站：样本不足，不参与聚类
        specs.append(dict(station=f'SP{index}', per_day=1, facility='交流',
                          duration=4.0, kwh=3.0, hours=[9], stride=5))
    return specs


class StationsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def rows(self):
        return [
            dict(station_id='A', day_index=0, hour=9, kwh=6.0, duration_hours=2.0,
                 time_period='peak', is_weekend=0, facility_label='直流'),
            dict(station_id='A', day_index=0, hour=9, kwh=6.0, duration_hours=4.0,
                 time_period='normal', is_weekend=0, facility_label='交流'),
            dict(station_id='A', day_index=1, hour=10, kwh=8.0, duration_hours=3.0,
                 time_period='peak', is_weekend=1, facility_label='直流'),
            dict(station_id='B', day_index=0, hour=20, kwh=2.0, duration_hours=1.0,
                 time_period='normal', is_weekend=0, facility_label='交流'),
        ]

    def test_station_features_summarize_behavior(self):
        table = stations.station_features(self.rows())
        self.assertEqual(set(table), {'A', 'B'})
        a = table['A']
        self.assertEqual(a['sessions'], 3)
        self.assertEqual(a['active_days'], 2)
        self.assertEqual(a['active_hours'], 2)
        self.assertAlmostEqual(a['sessions_per_active_day'], 1.5)
        self.assertAlmostEqual(a['kwh_per_session'], 20 / 3)
        self.assertAlmostEqual(a['duration_mean'], 3.0)
        self.assertAlmostEqual(a['peak_share'], 2 / 3)
        self.assertAlmostEqual(a['weekend_share'], 1 / 3)
        self.assertAlmostEqual(a['active_hours_per_day'], 1.0)
        self.assertAlmostEqual(a['dc_share'], 2 / 3)
        self.assertEqual(table['B']['sessions'], 1)

    def test_standardize_centers_and_scales_columns(self):
        scaled, scaling = stations.standardize([[1.0, 5.0], [2.0, 5.0], [3.0, 5.0]])
        self.assertAlmostEqual(sum(row[0] for row in scaled) / 3, 0.0)
        self.assertAlmostEqual(scaled[0][0], -1.2247, places=3)
        self.assertTrue(all(row[1] == 0.0 for row in scaled))    # 常量列退化为 0
        self.assertEqual(scaling[1]['std'], 1.0)

    def test_silhouette_rewards_separation(self):
        separated = stations.silhouette([[0.0, 0.0], [0.1, 0.0], [10.0, 0.0], [10.1, 0.0]],
                                        [0, 0, 1, 1])
        mixed = stations.silhouette([[0.0, 0.0], [0.1, 0.0], [10.0, 0.0], [10.1, 0.0]],
                                    [0, 1, 0, 1])
        self.assertGreater(separated, 0.9)
        self.assertLess(mixed, separated)
        self.assertIsNone(stations.silhouette([[0.0], [1.0]], [0, 0]))   # 只有一个簇

    def test_choose_clusters_picks_the_separated_two_groups(self):
        points = [[0.0, 0.0], [0.1, 0.2], [0.2, 0.1], [9.0, 9.0], [9.2, 8.8], [8.9, 9.1]]
        k, labels, centers, scores = stations.choose_clusters(points, k_range=(2, 3), seed=7)
        self.assertEqual(k, 2)
        self.assertEqual(set(labels[:3]), {labels[0]})
        self.assertNotEqual(labels[0], labels[3])
        self.assertTrue(any(item['k'] == 2 for item in scores))
        self.assertGreater(scores[0]['silhouette'], 0.9)

    def test_cluster_names_come_from_the_strongest_deviations(self):
        names = stations.name_clusters([[2.0, -1.5, 0.0, 0.0, 0.0, 0.0, 0.0],
                                        [-0.1, 0.0, 0.0, 0.0, 0.0, 1.9, 0.0]])
        self.assertEqual(names[0], '偏繁忙·小电量')
        self.assertEqual(names[1], '全时段·偏冷清')

    def test_busyness_tiers_split_by_quantile_and_flag_sparse(self):
        table = {f'S{index}': dict(sessions_per_active_day=float(index), sessions=index * 5)
                 for index in range(8)}
        clustered = {f'S{index}' for index in range(1, 7)}
        result = stations.busyness_tiers(table, clustered=clustered)
        self.assertEqual(result['cuts'], [1.75, 3.5, 5.25])
        self.assertEqual(result['tiers']['S0']['tier'], '样本不足')
        self.assertEqual(result['tiers']['S7']['tier'], '样本不足')
        ordered = [result['tiers'][f'S{index}']['index'] for index in range(1, 7)]
        self.assertEqual(ordered, sorted(ordered))         # 越忙档位越高
        self.assertEqual(result['names'], stations.TIER_NAMES)

    def test_sparse_stations_are_excluded_from_clustering(self):
        database = build_database(self.root / 'sparse.db', two_group_specs())
        section = stations.train(database, min_sessions=20)
        self.assertEqual(section['clustered_stations'], 6)
        self.assertEqual(section['sparse_stations'], ['SP0', 'SP1'])
        self.assertEqual(section['stations'], 8)
        self.assertEqual(section['tiers']['tiers']['SP0']['tier'], '样本不足')
        self.assertEqual(section['clustered_sessions'],
                         section['sessions'] - 2 * 8)      # 稀疏站每站 8 单

    def test_two_behavior_groups_land_in_different_clusters(self):
        database = build_database(self.root / 'groups.db', two_group_specs())
        section = stations.train(database, min_sessions=20)
        grouped = {}
        for station in section['top_stations']:
            if station['cluster']:
                grouped.setdefault(station['cluster'], set()).add(station['station'])
        self.assertEqual(len(grouped), 2)
        for members in grouped.values():
            self.assertTrue(all(name.startswith('DC') for name in members)
                            or all(name.startswith('AC') for name in members))

    def test_train_is_deterministic_and_stable_across_halves(self):
        database = build_database(self.root / 'repeat.db', two_group_specs())
        first = stations.train(database, min_sessions=20)
        second = stations.train(database, min_sessions=20)
        self.assertEqual(first['cluster_count'], second['cluster_count'])
        self.assertEqual([cluster['name'] for cluster in first['clusters']],
                         [cluster['name'] for cluster in second['clusters']])
        self.assertEqual(first['tiers']['tiers'], second['tiers']['tiers'])
        self.assertEqual(first['stability'], second['stability'])
        self.assertIsNotNone(first['stability']['agreement'])
        self.assertEqual(len(first['top_stations']), 8)       # 站点不足 20 个时全列
        payload = json.dumps(first, ensure_ascii=False)
        self.assertIn('clusters', json.loads(payload))

    def test_report_marks_sparse_and_lists_views(self):
        database = build_database(self.root / 'report.db', two_group_specs())
        section = stations.train(database, min_sessions=20)
        path = self.root / '站点画像.md'
        stations.build_report(section, path)
        text = path.read_text(encoding='utf-8')
        for fragment in ('## 五、站点画像聚类与繁忙度分档', '轮廓系数', '样本不足',
                         '会话量 Top', '稳健性检查', '更细的参考视图'):
            self.assertIn(fragment, text)
        self.assertNotIn('口径', text)


if __name__ == '__main__':
    unittest.main()
