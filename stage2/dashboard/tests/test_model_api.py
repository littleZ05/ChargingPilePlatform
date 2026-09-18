"""模型接口单测：参数校验、负荷预测、单次分位数与站点画像的响应体。"""
import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import urlopen

DASHBOARD = Path(__file__).resolve().parents[1]
PREDICT = DASHBOARD.parent / 'predict'
for path in (str(DASHBOARD), str(PREDICT), str(PREDICT / 'tests')):
    if path not in sys.path:
        sys.path.insert(0, path)

import model_api  # noqa: E402
import support  # noqa: E402
import train_v2  # noqa: E402
from server import create_server  # noqa: E402
from test_analytics import (BATTERY_COLUMNS, STATION_COLUMNS,  # noqa: E402
                            session as cleaning_session, write_cleaning)


class ModelApiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        hourly = [[1 + ((offset + hour) % 4) if 6 <= hour <= 22 else 0
                   for hour in range(24)] for offset in range(50)]
        database = support.create_database(cls.root / 'warehouse.db', hourly)
        cls.model = train_v2.train(database, min_train_days=14)
        # HTTP 用例要起真实服务，而服务启动时按契约校验清洗产物，所以这里也要一份合成产物
        cls.cleaning = cls.root / 'cleaning'
        cls.cleaning.mkdir()
        write_cleaning(cls.cleaning, {
            'sessions': [cleaning_session('1', kwh='0.1'),
                         cleaning_session('2', kwh='0.2', station_id='002', start_hour='13')],
            'stations': [dict(STATION_COLUMNS, stationId='001'),
                         dict(STATION_COLUMNS, source_row='2', stationId='002', station_name='乙站')],
            'battery': [dict(BATTERY_COLUMNS)]})

    def test_forecast_returns_24_hours_for_every_combination(self):
        for base in model_api.BASES:
            for horizon in model_api.HORIZONS:
                payload = model_api.forecast(self.model, {'base': base, 'horizon': str(horizon)})
                self.assertEqual(payload['base'], base)
                self.assertEqual(payload['horizon'], horizon)
                self.assertEqual(len(payload['hours']), 24)
                self.assertEqual([point['hour'] for point in payload['hours']], list(range(24)))
                self.assertTrue(payload['method'])
                self.assertIn('相对', payload['note'])
                self.assertTrue(all(point['predicted'] is None or point['predicted'] >= 0
                                    for point in payload['hours']))

    def test_forecast_default_is_sessions_24h(self):
        payload = model_api.forecast(self.model, {})
        self.assertEqual((payload['base'], payload['horizon']), ('sessions', 24))

    def test_forecast_rejects_bad_parameters(self):
        for filters in ({'base': 'cars'}, {'horizon': '12'}, {'horizon': 'x'},
                        {'weekday': 'Mon'}, {'base': 'kwh', 'limit': '1'}):
            with self.assertRaises(ValueError):
                model_api.forecast(self.model, dict(filters))

    def test_session_quantiles_are_ordered_and_explained(self):
        options = model_api.model_options(self.model)
        payload = model_api.session_quantiles(self.model, {
            'target': 'duration_hours', 'facility': options['facilities'][0],
            'period': options['periods'][0], 'hour': '9'})
        values = [payload['quantiles'][key] for key in ('0.1', '0.5', '0.9')]
        self.assertEqual(values, sorted(values))
        self.assertEqual(set(payload['chosen']), {0.1, 0.5, 0.9})
        self.assertIn(payload['chosen'][0.5], ('quantile_ridge', 'global_quantile',
                                               'facility_quantile', 'facility_period_quantile'))
        self.assertTrue(all(0.0 <= payload['coverage'][tau] <= 1.0 for tau in payload['coverage']))
        self.assertIn('pinball', payload['protocol']['metric'])

    def test_session_quantiles_validate_enums(self):
        cases = ({'target': 'power'}, {'facility': '不存在的类型'}, {'period': 'noon'},
                 {'hour': '24'}, {'hour': 'x'}, {'platform': 'symbian'},
                 {'weekend': '2'}, {'unknown': '1'})
        for filters in cases:
            with self.assertRaises(ValueError):
                model_api.session_quantiles(self.model, dict(filters))

    def test_station_payload_carries_clusters_tiers_and_top(self):
        payload = model_api.stations(self.model, {'limit': '5'})
        self.assertLessEqual(len(payload['top_stations']), 5)
        self.assertEqual(payload['stations'], payload['clustered'] + payload['sparse'])
        self.assertGreaterEqual(payload['cluster_count'], 2)
        self.assertEqual(sum(payload['tiers']['counts'].values()), payload['stations'])
        self.assertTrue(all('name' in cluster for cluster in payload['clusters']))
        self.assertIn('不代表经营优先级', payload['note'])
        for row in payload['top_stations']:
            self.assertIn('busyness', row)

    def test_model_options_expose_the_full_enum(self):
        payload = model_api.model_options(self.model)
        self.assertTrue(payload['facilities'])
        self.assertTrue(payload['periods'])
        self.assertEqual(set(payload['targets']), {'duration_hours', 'kwh'})
        self.assertEqual([level['tau'] for level in payload['levels']], [0.1, 0.5, 0.9])

    def test_missing_sections_are_reported_not_guessed(self):
        with self.assertRaises(ValueError):
            model_api.session_quantiles({'version': 'x'}, {})
        with self.assertRaises(ValueError):
            model_api.stations({'version': 'x'}, {})
        with self.assertRaises(ValueError):
            model_api.forecast({'version': 'x'}, {})
        with self.assertRaises(ValueError):
            model_api.classification({'version': 'x'}, {'hour': '9'})

    def test_classification_returns_probability_decision_and_context(self):
        options = model_api.model_options(self.model)
        payload = model_api.classification(self.model, {
            'facility': options['facilities'][0], 'period': options['periods'][0], 'hour': '9'})
        lookup = payload['lookup']
        self.assertEqual(lookup['method'], payload['chosen'])
        self.assertGreaterEqual(lookup['probability'], 0.0)
        self.assertLessEqual(lookup['probability'], 1.0)
        self.assertEqual(lookup['decision'], int(lookup['probability'] >= lookup['threshold']))
        self.assertTrue(lookup['decision_label'])
        self.assertTrue(lookup['leaf']['conditions_text'] or lookup['leaf']['samples'] > 0)
        self.assertEqual(len(payload['curve']), 24)
        self.assertEqual([row['hour'] for row in payload['curve']], list(range(24)))
        self.assertEqual(len(payload['methods']), 4)
        self.assertTrue(payload['segments'])
        self.assertIn('测试块', payload['note'])
        self.assertEqual(payload['version'], self.model['version'])
        self.assertIn(lookup['rule_label'], [row['label'] for row in payload['methods']])

    def test_classification_accepts_station_and_weekend_filters(self):
        options = model_api.model_options(self.model)
        payload = model_api.classification(self.model, {
            'facility': options['facilities'][0], 'period': options['periods'][0], 'hour': '18',
            'weekend': '1', 'station': 'S1'})
        self.assertEqual(payload['lookup']['is_weekend'], 1)
        self.assertEqual(payload['lookup']['station'], 'S1')
        self.assertGreater(payload['lookup']['station_count'], 0)

    def test_classification_validates_enums_and_hour(self):
        options = model_api.model_options(self.model)
        cases = ({'facility': '不存在的类型', 'hour': '9'}, {'period': 'noon', 'hour': '9'},
                 {'hour': '24'}, {'hour': 'x'}, {'hour': None}, {'weekend': '2', 'hour': '9'},
                 {'unknown': '1', 'hour': '9'})
        for filters in cases:
            with self.assertRaises(ValueError):
                model_api.classification(self.model, dict(filters))
        self.assertTrue(options['facilities'])

    def test_classification_endpoint_serves_the_model_over_http(self):
        model_path = self.root / 'model_v2.json'
        model_path.write_text(json.dumps(self.model, ensure_ascii=False), encoding='utf-8')
        server = create_server(str(self.cleaning), 0, None, str(model_path))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f'http://127.0.0.1:{server.server_port}'
        try:
            urlopen(base + '/api/health', timeout=2).close()
        except OSError as error:
            server.shutdown()
            server.server_close()
            thread.join()
            self.skipTest(f'当前环境不允许 loopback 连接（{error}）')
        try:
            with urlopen(base + '/api/classification?hour=9') as response:
                payload = json.load(response)
            self.assertEqual(payload['code'], 0)
            self.assertTrue(payload['data']['lookup']['decision_label'])
            with urlopen(base + '/api/health') as response:
                health = json.load(response)['data']
            self.assertTrue(health['classification_ready'])
            self.assertTrue(health['forecast_ready'])
            for suffix in ['/api/classification?hour=24', '/api/classification?unknown=1',
                           '/api/classification?hour=9&period=noon']:
                with self.assertRaises(HTTPError) as error:
                    urlopen(base + suffix)
                self.assertEqual(error.exception.code, 400)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == '__main__':
    unittest.main()
