"""预测层单测：季节画像、线性拟合、评估指标、模型产物与预测接口。"""
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import train
from forecast import baseline_predict, forecast, linear_predict


class PredictTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_linear_predict_respects_coefficients(self):
        self.assertEqual(linear_predict([5.0, 1.0, 0.0, 0.0], 'Mon', 3), 8.0)
        self.assertEqual(linear_predict([5.0, 1.0, 2.0, 0.0], 'Sat', 3), 10.0)
        self.assertEqual(linear_predict([0.0, -1.0, 0.0, 0.0], 'Mon', 3), 0.0)  # 负值截断为 0

    def test_seasonal_predict_recovers_profile(self):
        model = dict(hour_profile={str(h): (10.0 if h == 8 else 1.0) for h in range(24)},
                     weekday_factor={'Mon': 1.0})
        self.assertAlmostEqual(baseline_predict(model, 'Mon', 8), 10.0)
        self.assertAlmostEqual(baseline_predict(model, 'Mon', 0), 1.0)

    def test_metrics_known_errors(self):
        stats = train.metrics([2, 2, 2], [3, 1, 2])
        self.assertEqual(stats['samples'], 3)
        self.assertAlmostEqual(stats['mae'], 2 / 3, places=4)
        self.assertAlmostEqual(stats['mean_actual'], 2.0, places=4)

    def test_fit_linear_uses_least_squares(self):
        observations = [dict(weekday='Mon', hour=h, sessions=2 * h + 3, kwh=0.0) for h in range(24)]
        coefficients = train.fit_linear(observations)
        self.assertAlmostEqual(coefficients[0], 3.0, places=4)
        self.assertAlmostEqual(coefficients[1], 2.0, places=4)

    def build_database(self):
        database = self.root / 'warehouse.db'
        connection = sqlite3.connect(database)
        connection.execute('create table dwd_sessions (weekday text, start_hour integer, kwh text,'
                           ' weekday_usable text, time_of_day_usable text)')
        rows = []
        for weekday in train.DAYS:
            for hour in range(24):
                sessions = 12 if hour == 9 else (6 if weekday in ('Sat', 'Sun') else 2)
                for _ in range(sessions):
                    rows.append((weekday, hour, '4.5', '1', '1'))
        connection.executemany('insert into dwd_sessions values (?,?,?,?,?)', rows)
        connection.commit()
        connection.close()
        return database

    def test_train_model_artifacts_and_forecast_window(self):
        model = train.train(self.build_database())
        self.assertIn(model['method'], {'seasonal', 'linear'})
        self.assertEqual(len(model['hour_profile']), 24)
        self.assertEqual(model['source']['cells'], 168)
        expected_sessions = 7 * 12 + 5 * 23 * 2 + 2 * 23 * 6      # 每天9点12次，工作日其它时段2次，周末6次
        self.assertEqual(model['source']['sessions'], expected_sessions)
        self.assertIn('mae', model['evaluation']['seasonal'])
        stored = json.loads(json.dumps(model, ensure_ascii=False))
        self.assertEqual(stored['version'], model['version'])
        result = forecast(model, 'Mon', 24)
        self.assertEqual(len(result['hours']), 24)
        self.assertEqual(len(result['quiet_hours']), 5)
        self.assertGreater(result['total_sessions'], 0)
        self.assertEqual(result['version'], model['version'])
        report = self.root / 'evaluate.md'
        train.build_report(model, report)
        self.assertIn('留一星期', report.read_text(encoding='utf-8'))

    def test_forecast_rejects_unknown_weekday(self):
        model = dict(method='seasonal', version='t', hour_profile={str(h): 1.0 for h in range(24)},
                     weekday_factor={'Mon': 1.0}, energy_per_session=1.0)
        with self.assertRaises(ValueError):
            forecast(model, 'Funday', 24)


if __name__ == '__main__':
    unittest.main()
