"""落库单测：预测结果写进数仓、重跑不重复、数据字典追加预测层清单。"""
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

import support

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import publish  # noqa: E402
import train_v2  # noqa: E402


class PublishTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        hourly = [[1 + ((offset + hour) % 4) if 6 <= hour <= 22 else 0
                   for hour in range(24)] for offset in range(50)]
        cls.database = support.create_database(cls.root / 'warehouse.db', hourly)
        cls.model = train_v2.train(cls.database, min_train_days=14)

    def rows(self, table):
        connection = sqlite3.connect(self.database)
        try:
            return connection.execute(f'select * from {table}').fetchall()
        finally:
            connection.close()

    def test_publish_creates_every_table_with_expected_shape(self):
        summary = publish.publish(self.database, self.model)
        panel = self.rows('dws_day_hour')
        self.assertEqual(len(panel), 50 * 24)
        self.assertEqual(summary['tables']['dws_day_hour'], 50 * 24)
        # 面板里既有观测日（mask=1）也有缺测日（若存在）；这里全部为观测日
        self.assertTrue(all(row[6] == 1 for row in panel))
        self.assertEqual(summary['tables']['ads_forecast'],
                         len(self.model['forecast']) * 24)
        self.assertEqual(summary['tables']['dws_station_features'],
                         len(self.rows('dws_station_features')))
        self.assertEqual(summary['tables']['ads_session_quantile'],
                         len(self.rows('ads_session_quantile')))
        self.assertEqual(summary['tables']['ads_session_classification'],
                         len(self.rows('ads_session_classification')))
        self.assertEqual(summary['tables']['ads_classification_metric'],
                         len(self.rows('ads_classification_metric')))
        self.assertEqual(summary['model_version'], self.model['version'])

    def test_classification_rows_match_the_model_and_the_stored_threshold(self):
        publish.publish(self.database, self.model)
        section = self.model['classification']
        categories = section['categories']
        rows = self.rows('ads_session_classification')
        self.assertEqual(len(rows), len(categories['facility_label'])
                         * len(categories['time_period']) * 24)
        threshold = section['deployment']['decision_threshold']
        for facility, period, hour, method, probability, decision, rule, tree, cut, *_ in rows:
            self.assertIn(facility, categories['facility_label'])
            self.assertIn(period, categories['time_period'])
            self.assertEqual(method, section['chosen'])
            self.assertEqual(cut, threshold)
            self.assertEqual(decision, int(probability >= threshold))
            if section['chosen'] == 'cart':
                self.assertNotEqual(rule, tree)     # 上线树时两支必须分开记，不能抄成一个数
        self.assertEqual({row[9] for row in rows}, {section['deployment']['train_sessions']})

    def test_classification_metrics_table_marks_the_online_method(self):
        publish.publish(self.database, self.model)
        section = self.model['classification']
        rows = self.rows('ads_classification_metric')
        self.assertEqual([row[0] for row in rows],
                         section['baselines'] + ['cart'])
        chosen = [row for row in rows if row[10] == 1]
        self.assertEqual(len(chosen), 1)
        self.assertEqual(chosen[0][0], section['chosen'])
        for row in rows:
            self.assertTrue(row[1])
            self.assertIsNotNone(row[2])          # 平均精度
            self.assertEqual(row[11], self.model['version'])

    def test_forecast_rows_carry_method_and_version(self):
        publish.publish(self.database, self.model)
        rows = self.rows('ads_forecast')
        bases = {row[3] for row in rows}
        horizons = {row[4] for row in rows}
        self.assertEqual(bases, {'sessions', 'kwh'})
        self.assertEqual(horizons, {1, 6, 24})
        self.assertTrue(all(row[5] for row in rows))          # 每条都写明上线方法
        self.assertTrue(all(row[7] == self.model['version'] for row in rows))

    def test_session_quantiles_are_monotone_and_labelled(self):
        publish.publish(self.database, self.model)
        rows = self.rows('ads_session_quantile')
        grouped = {}
        for target, facility, period, hour, tau, predicted, chosen, *_ in rows:
            grouped.setdefault((target, facility, period, hour), {})[tau] = predicted
        self.assertTrue(grouped)
        for key, values in grouped.items():
            ordered = [values[tau] for tau in sorted(values)]
            self.assertEqual(ordered, sorted(ordered), f'{key} 的分位数不单调')

    def test_republish_replaces_instead_of_appending(self):
        first = publish.publish(self.database, self.model)
        second = publish.publish(self.database, self.model)
        self.assertEqual(first['tables'], second['tables'])
        self.assertEqual(len(self.rows('ads_forecast')), second['tables']['ads_forecast'])

    def test_dictionary_appendix_is_written(self):
        path = self.root / '数据字典.md'
        path.write_text('# 数据字典\n', encoding='utf-8')
        publish.publish(self.database, self.model, dictionary=path)
        text = path.read_text(encoding='utf-8')
        for fragment in ('预测层落库表', 'dws_day_hour', 'ads_forecast',
                         'ads_session_quantile', 'ads_session_classification',
                         'ads_classification_metric', self.model['version']):
            self.assertIn(fragment, text)
        self.assertNotIn('口径', text)

    def test_manifest_is_refreshed_with_published_tables(self):
        """落库后数仓清单必须与库内容一致，不能停在装载阶段的 44 张表。"""
        path = self.root / 'warehouse_manifest.json'
        path.write_text(json.dumps({'database': str(self.database), 'tables': 1, 'rows': 10,
                                    'layers': {'ads': 1},
                                    'tables_detail': [{'table': 'ads_kpi_daily', 'layer': 'ads',
                                                       'rows': 10}]},
                                   ensure_ascii=False), encoding='utf-8')
        summary = publish.publish(self.database, self.model)
        manifest = json.loads(path.read_text(encoding='utf-8'))
        self.assertEqual(manifest['tables'], len(publish.TABLES) + 1)
        self.assertEqual(manifest['layers']['ads'], 1 + sum(
            1 for layer, _ in publish.PUBLISHED_TABLES.values() if layer == 'ads'))
        self.assertEqual(manifest['rows'],
                         10 + sum(summary['tables'][table] for table in publish.TABLES))
        self.assertEqual(manifest['prediction_model_version'], self.model['version'])
        # 重跑不重复登记
        publish.publish(self.database, self.model)
        again = json.loads(path.read_text(encoding='utf-8'))
        self.assertEqual(again['tables'], manifest['tables'])
        self.assertEqual(again['rows'], manifest['rows'])

    def test_cli_entrypoint_publishes_and_prints_summary(self):
        import contextlib
        import io
        model_path = self.root / 'model.json'
        model_path.write_text(json.dumps(self.model, ensure_ascii=False), encoding='utf-8')
        dictionary = self.root / 'dict-cli.md'
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = publish.main(['--db', str(self.database), '--model', str(model_path),
                                 '--dictionary', str(dictionary)])
        self.assertEqual(code, 0)
        printed = json.loads(buffer.getvalue())
        self.assertEqual(printed['model_version'], self.model['version'])
        self.assertIn('ads_forecast', printed['tables'])
        self.assertIn('预测层落库表', dictionary.read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
