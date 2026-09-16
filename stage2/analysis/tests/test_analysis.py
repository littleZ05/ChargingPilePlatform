"""分析层单测：统计量、相关系数、回归、聚类、异常检测与端到端分析。"""
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import analysis


class AnalysisTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_describe_known_values(self):
        stats = analysis.describe([1, 2, 3, 4])
        self.assertEqual(stats['count'], 4)
        self.assertEqual(stats['sum'], 10)
        self.assertEqual(stats['mean'], 2.5)
        self.assertEqual(stats['median'], 2.5)
        self.assertEqual(stats['p25'], 1.75)
        self.assertEqual(stats['p75'], 3.25)

    def test_pearson_and_regression_exact(self):
        pairs = [(x, 2 * x + 1) for x in range(10)]
        self.assertEqual(analysis.pearson(pairs), 1.0)
        model = analysis.linear_regression(pairs)
        self.assertAlmostEqual(model['slope'], 2.0, places=6)
        self.assertAlmostEqual(model['intercept'], 1.0, places=6)
        self.assertAlmostEqual(model['r_squared'], 1.0, places=6)

    def test_pearson_returns_none_for_constant_series(self):
        self.assertIsNone(analysis.pearson([(1, 5), (1, 6)]))

    def test_kmeans_separates_two_groups(self):
        points = [[0.0, 0.0], [0.1, 0.1], [10.0, 10.0], [10.1, 10.1]]
        labels, centers = analysis.kmeans(points, k=2)
        self.assertEqual(len(set(labels)), 2)
        self.assertEqual(labels[0], labels[1])
        self.assertEqual(labels[2], labels[3])
        self.assertNotEqual(labels[0], labels[2])
        self.assertLess(min(centers[0][0], centers[1][0]), 1.0)
        self.assertGreater(max(centers[0][0], centers[1][0]), 9.0)

    def test_outliers_detect_extreme_value(self):
        values = [1.0] * 20 + [100.0]
        stats = analysis.outliers(values)
        self.assertGreaterEqual(stats['iqr_outliers'], 1)
        self.assertEqual(stats['count'], 21)

    def build_database(self):
        database = self.root / 'warehouse.db'
        connection = sqlite3.connect(database)
        connection.execute('create table dwd_sessions (station_id text, facility_type text,'
                           ' platform text, weekday text, start_hour integer, kwh text,'
                           ' duration_hours text, quality_flags text, time_of_day_usable text,'
                           ' weekday_usable text)')
        rows = []
        for index in range(30):
            rows.append((f'00{index % 3}', '4', 'ios' if index % 2 else 'android',
                         analysis.__dict__ and ['Mon', 'Tue', 'Wed'][index % 3], 8 + index % 4,
                         str(2.0 + index % 5), str(1.0 + index % 3), '', '1', '1'))
        connection.executemany('insert into dwd_sessions values (?,?,?,?,?,?,?,?,?,?)', rows)
        connection.execute('create table ingest_business_orders (id integer)')
        connection.execute('insert into ingest_business_orders values (1),(2)')
        connection.commit()
        connection.close()
        return database

    def test_analyse_end_to_end_and_report(self):
        database = self.build_database()
        result = analysis.analyse(database, clusters=2)
        self.assertEqual(result['source']['sessions'], 30)
        self.assertEqual(result['source']['stations'], 3)
        self.assertEqual(result['source']['ingestion']['ingest_business_orders'], 2)
        self.assertEqual(result['descriptives']['kwh']['count'], 30)
        self.assertIn('Mon', result['seasonality']['weekday'])
        self.assertEqual(len(result['clusters']['profiles']), 3)
        report = self.root / 'report.md'
        analysis.build_report(result, report)
        text = report.read_text(encoding='utf-8')
        self.assertIn('第二阶段数据分析结果', text)
        self.assertIn('站点聚类', text)
        json.dumps(result, ensure_ascii=False)


if __name__ == '__main__':
    unittest.main()
