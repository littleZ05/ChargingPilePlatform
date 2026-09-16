import csv
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analytics import Analytics
from server import create_server


class AnalyticsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'dwd').mkdir()
        session = dict(station_id='001',facility_type='4',platform='ios',weekday='Mon',start_hour='11',
                       kwh='0.1',duration_hours='1',fee_original='0',quality_flags='calendar_year_unverified',
                       time_of_day_usable='1',weekday_usable='1',user_id='must-not-leak')
        data = {'sessions': [session,dict(session,kwh='0',platform='android',duration_hours='30'),
                             dict(session,station_id='002',kwh='0.2',weekday='Tue')],
                'stations': [dict(stationId='001',station_name='甲站'),dict(stationId='002',station_name='乙站')],
                'battery': [dict(soc_percent='50',current_a='-70',max_temperature_c='35',pack_voltage_v='350',
                                 quality_flags='outside_tutorial_current_range_retained',esd_raw='must-not-leak')]}
        hashes = {}
        for name, rows in data.items():
            path=self.root/'dwd'/f'{name}.csv'
            with path.open('w',newline='') as stream:
                writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
            hashes[f'dwd/{name}.csv']=hashlib.sha256(path.read_bytes()).hexdigest()
        (self.root/'manifest.json').write_text(json.dumps(dict(output_sha256=hashes,
            reconciliation={k:{'retained':len(v)} for k,v in data.items()})))
        self.analytics=Analytics(self.root)

    def test_decimal_zero_and_dimension_reconciliation(self):
        result=self.analytics.overview({})
        self.assertEqual(result['summary']['total_kwh'],'0.3')
        self.assertEqual(result['summary']['sessions'],3)
        self.assertEqual(result['summary']['zero_energy'],1)
        for rows in result['dimensions'].values():
            self.assertEqual(sum(r['sessions'] for r in rows),3)
        self.assertEqual(sum(r['count'] for r in result['duration_histogram']),3)
        self.assertNotIn('must-not-leak',json.dumps(result))

    def test_combined_filters_empty_result_and_invalid_values(self):
        result=self.analytics.overview({'station_id':'001','platform':'ios','energy':'positive'})
        self.assertEqual(result['summary']['sessions'],1)
        empty=self.analytics.overview({'station_id':'002','platform':'android'})
        self.assertEqual(empty['summary']['sessions'],0)
        self.assertIsNone(empty['summary']['mean_kwh'])
        with self.assertRaises(ValueError):self.analytics.overview({'station_id':"' OR 1=1"})
        with self.assertRaises(ValueError):self.analytics.overview({'date':'2015'})

    def test_tampered_source_refused_and_battery_independent(self):
        result=self.analytics.battery_summary()
        self.assertEqual(result['records'],1)
        self.assertEqual(result['flagged_current'],1)
        self.assertNotIn('must-not-leak',json.dumps(result))
        (self.root/'dwd/sessions.csv').write_text('tampered')
        with self.assertRaises(ValueError):Analytics(self.root)

    def test_actual_http_filters_and_errors(self):
        server=create_server(self.root,0)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            base=f'http://127.0.0.1:{server.server_port}'
            with urlopen(base+'/api/overview?energy=zero') as response:
                self.assertEqual(json.load(response)['data']['summary']['sessions'],1)
            for suffix in ['/api/overview?energy=wrong','/api/overview?platform=ios&platform=android']:
                with self.assertRaises(HTTPError) as error:urlopen(base+suffix)
                self.assertEqual(error.exception.code,400)
            with self.assertRaises(HTTPError) as error:urlopen(base+'/api/not-found')
            self.assertEqual(error.exception.code,404)
        finally:
            server.shutdown();server.server_close();thread.join()

    def test_report_export_and_forecast_endpoint(self):
        rows, summary, version = self.analytics.report('start_hour', {})
        self.assertEqual(len(rows), 24)
        self.assertIsInstance(version, str)
        self.assertEqual(summary['sessions'], 3)
        with self.assertRaises(ValueError):
            self.analytics.report('user_id', {})
        model = dict(method='seasonal', version='test-model',
                     hour_profile={str(h): (5.0 if h == 9 else 1.0) for h in range(24)},
                     weekday_factor={'Mon': 1.0}, energy_per_session=2.0,
                     evaluation={'seasonal': {'mae': 0.5}}, caveats=['课程数据限制'])
        model_path = self.root / 'model.json'
        model_path.write_text(json.dumps(model), encoding='utf-8')
        server = create_server(self.root, 0, str(model_path))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            base = f'http://127.0.0.1:{server.server_port}'
            with urlopen(base + '/api/report?kind=start_hour&energy=zero') as response:
                text = response.read().decode('utf-8-sig')
                self.assertTrue(response.headers['Content-Type'].startswith('text/csv'))
            self.assertIn('sessions', text.splitlines()[0])
            self.assertEqual(len(text.strip().splitlines()), 25)          # 表头 + 24 个小时
            with urlopen(base + '/api/forecast?weekday=Mon&horizon=24') as response:
                payload = json.load(response)['data']
            self.assertEqual(len(payload['hours']), 24)
            self.assertEqual(payload['version'], 'test-model')
            self.assertEqual(payload['hours'][9]['sessions'], 5.0)
            for suffix in ['/api/report?kind=bogus', '/api/forecast?weekday=Nope']:
                with self.assertRaises(HTTPError) as error:
                    urlopen(base + suffix)
                self.assertEqual(error.exception.code, 400)
        finally:
            server.shutdown();server.server_close();thread.join()


if __name__=='__main__':unittest.main()
