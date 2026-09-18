"""大屏分析与契约校验测试：用合成清洗产物覆盖 v2.0 列契约与失败路径。"""
import csv
import hashlib
import json
import sys
import tempfile
import threading
import unittest
from decimal import Decimal
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'common'))
from analytics import Analytics  # noqa: E402
from contract import RULE_VERSION, TARIFF, ContractError, period_of  # noqa: E402
from server import create_server  # noqa: E402

STATION_COLUMNS = dict(source_row='1', stationId='001', locationId='01', facilityType='3',
                       station_name='甲站', address='河南省郑州市', device_count='2', open_time='00:00-24:00',
                       update_time='2019-07-26', facility_label='直交流一体',
                       geography_status='provided_metadata_not_verified',
                       quality_flags='facility_dictionary_assumed')
BATTERY_COLUMNS = dict(source_row='1', esd_raw='must-not-leak', record_time_raw='2.02E+13', timestamp='',
                       time_series_usable='0', soc_percent='50', pack_voltage_v='350', current_a='-70',
                       max_cell_voltage_v='4', min_cell_voltage_v='3.9', max_temperature_c='35',
                       min_temperature_c='30', available_energy_source_unit_unverified='10',
                       available_capacity_ah='20', instantaneous_power_magnitude_kw='24.5',
                       quality_flags='outside_tutorial_current_range_retained')


def session(source_row='1', station_id='001', start_hour='11', kwh='0.1', platform='ios', weekday='Mon',
            weekday_index=0, duration_hours='1'):
    """按契约派生 time_period/unit_price/estimated_fee，避免测试自己另写一套定义。"""
    period = period_of(start_hour)
    price = TARIFF[period]
    estimated = (Decimal(kwh) * price).quantize(Decimal('0.01'))
    return dict(source_row=str(source_row), session_id=f's{source_row}', user_id='must-not-leak',
                station_id=station_id, location_id='01', facility_type='3', facility_label='直交流一体',
                station_name='甲站', address='河南省郑州市', kwh=kwh, fee_original='0',
                fee_status='zero_unverified', calendar_date='', start_hour=str(start_hour),
                end_hour=str((int(start_hour) + 1) % 24), weekday=weekday, weekday_index=str(weekday_index),
                weekend='1' if weekday_index >= 5 else '0', duration_hours=duration_hours,
                time_period=period, unit_price=str(price), estimated_fee=str(estimated), platform=platform,
                positive_energy='1' if Decimal(kwh) > 0 else '0', time_of_day_usable='1', weekday_usable='1',
                quality_flags='calendar_year_unverified')


def write_cleaning(root, data, rule_version=RULE_VERSION):
    """把合成数据写成清洗产物形态，并生成与内容一致的 manifest。"""
    (root / 'dwd').mkdir(parents=True, exist_ok=True)
    hashes, retained = {}, {}
    for name, rows in data.items():
        path = root / 'dwd' / f'{name}.csv'
        with path.open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        hashes[f'dwd/{name}.csv'] = hashlib.sha256(path.read_bytes()).hexdigest()
        retained[name] = {'input': len(rows), 'retained': len(rows), 'quarantined': 0}
    sessions = data['sessions']
    manifest = dict(rule_version=rule_version, input_sha256={}, reconciliation=retained,
                    total_kwh=str(sum((Decimal(r['kwh']) for r in sessions), Decimal(0))),
                    fee_total_source_unverified=str(sum((Decimal(r['fee_original']) for r in sessions), Decimal(0))),
                    fee_total_estimated_model=str(sum((Decimal(r.get('estimated_fee') or 0) for r in sessions),
                                                      Decimal(0))),
                    output_sha256=hashes)
    (root / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False), encoding='utf-8')
    return manifest


class AnalyticsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = {'sessions': [session('1', kwh='0.1'),
                                  session('2', kwh='0', platform='android', start_hour='13', duration_hours='30'),
                                  session('3', kwh='0.2', station_id='002', weekday='Tue', weekday_index=1)],
                     'stations': [dict(STATION_COLUMNS, stationId='001'),
                                  dict(STATION_COLUMNS, source_row='2', stationId='002', station_name='乙站')],
                     'battery': [dict(BATTERY_COLUMNS)]}
        write_cleaning(self.root, self.data)
        self.analytics = Analytics(self.root)

    def rewrite(self, **changes):
        """按需改写合成产物（如删掉一列、改回旧版本），用于验证失败路径。"""
        data = {k: [dict(r) for r in v] for k, v in self.data.items()}
        for name, drop in changes.get('drop', {}).items():
            for row in data[name]:
                for column in drop:
                    row.pop(column)
        for name, edit in changes.get('edit', {}).items():
            for row in data[name]:
                row.update(edit)
        write_cleaning(self.root, data, rule_version=changes.get('rule_version', RULE_VERSION))

    def test_decimal_zero_and_dimension_reconciliation(self):
        result = self.analytics.overview({})
        self.assertEqual(result['summary']['total_kwh'], '0.3')
        self.assertEqual(result['summary']['sessions'], 3)
        self.assertEqual(result['summary']['zero_energy'], 1)
        for rows in result['dimensions'].values():
            self.assertEqual(sum(r['sessions'] for r in rows), 3)
        self.assertEqual(sum(r['count'] for r in result['duration_histogram']), 3)
        self.assertNotIn('must-not-leak', json.dumps(result))
        self.assertEqual(result['rule_version'], RULE_VERSION)

    def test_combined_filters_empty_result_and_invalid_values(self):
        result = self.analytics.overview({'station_id': '001', 'platform': 'ios', 'energy': 'positive'})
        self.assertEqual(result['summary']['sessions'], 1)
        empty = self.analytics.overview({'station_id': '002', 'platform': 'android'})
        self.assertEqual(empty['summary']['sessions'], 0)
        self.assertIsNone(empty['summary']['mean_kwh'])
        with self.assertRaises(ValueError):
            self.analytics.overview({'station_id': "' OR 1=1"})
        with self.assertRaises(ValueError):
            self.analytics.overview({'date': '2015'})

    def test_time_period_filter_and_estimated_fee(self):
        """峰谷时段可筛选，估算电费按段单价累计，且与原费用分列。"""
        periods = {b['key']: b for b in self.analytics.overview({})['dimensions']['time_period']}
        self.assertEqual(sorted(periods), ['normal', 'peak'])
        self.assertEqual(periods['peak']['label'], '高峰')
        self.assertEqual(periods['peak']['sessions'], 2)              # 11 点两条
        self.assertEqual(periods['normal']['sessions'], 1)            # 13 点一条
        self.assertEqual(periods['peak']['estimated_fee_model'], '0.45')     # 0.1×1.5 + 0.2×1.5
        peak_only = self.analytics.overview({'time_period': 'peak'})['summary']
        self.assertEqual(peak_only['sessions'], 2)
        self.assertEqual(peak_only['estimated_fee_model'], '0.45')
        self.assertEqual(peak_only['source_fee_unverified'], '0')
        with self.assertRaises(ValueError):
            self.analytics.overview({'time_period': 'off_peak'})      # 样本里没有低谷，不静默返回空

    def test_facility_and_day_type_dimensions(self):
        result = self.analytics.overview({})
        facilities = {b['key']: b['label'] for b in result['dimensions']['facility_type']}
        self.assertEqual(facilities, {'3': '直交流一体'})
        day_type = {b['key']: b['sessions'] for b in result['dimensions']['day_type']}
        self.assertEqual(day_type, {'weekday': 3, 'weekend': 0})

    def test_contract_violations_are_fatal(self):
        """缺列、旧规则版本、派生列走样、行数不符都必须抛 ContractError，不得降级。"""
        self.rewrite(drop={'sessions': ['time_period', 'unit_price', 'estimated_fee']})
        with self.assertRaises(ContractError) as error:
            Analytics(self.root)
        self.assertIn('time_period', str(error.exception))

        write_cleaning(self.root, self.data, rule_version='1.0')
        with self.assertRaises(ContractError):
            Analytics(self.root)

        self.rewrite(edit={'sessions': {'time_period': 'off_peak'}})
        with self.assertRaises(ContractError):
            Analytics(self.root)

        self.rewrite()
        path = self.root / 'dwd/sessions.csv'
        lines = path.read_text(encoding='utf-8').splitlines()
        path.write_text('\n'.join(lines[:-1]) + '\n', encoding='utf-8')
        with self.assertRaises(ContractError):
            Analytics(self.root)

    def test_tampered_source_refused_and_battery_independent(self):
        result = self.analytics.battery_summary()
        self.assertEqual(result['records'], 1)
        self.assertEqual(result['flagged_current'], 1)
        self.assertNotIn('must-not-leak', json.dumps(result))
        (self.root / 'dwd/sessions.csv').write_text('tampered')
        with self.assertRaises(ContractError):
            Analytics(self.root)

    def test_metadata_exposes_single_source_of_truth(self):
        meta = self.analytics.metadata()
        self.assertEqual(meta['rule_version'], RULE_VERSION)
        self.assertEqual(meta['assumptions']['tariff'], {'peak': '1.5', 'normal': '1.0', 'off_peak': '0.7'})
        self.assertEqual(meta['assumptions']['facility_labels'], {'1': '直流', '2': '交流', '3': '直交流一体'})
        self.assertEqual(meta['options']['time_period'], ['peak', 'normal'])
        self.assertEqual(meta['total_kwh'], '0.3')

    def test_station_hour_matrix_is_ranked_and_dense(self):
        payload = self.analytics.station_hour({})
        self.assertEqual(payload['hours'], list(range(24)))
        self.assertEqual([row['station'] for row in payload['stations']], ['001', '002'])
        self.assertEqual(len(payload['data']), 2 * 24)          # 行是站点、列是小时，空小时补 0
        cells = {(hour, index): value for hour, index, value in payload['data']}
        self.assertEqual(cells[(11, 0)], 1)
        self.assertEqual(cells[(13, 0)], 1)
        self.assertEqual(cells[(11, 1)], 1)
        self.assertEqual(cells[(0, 0)], 0)
        self.assertEqual(payload['max_sessions'], 1)
        self.assertEqual(payload['sessions'], 3)
        self.assertTrue(payload['stations'][0]['label'])
        self.assertIn('开始小时可用', payload['notes'][0])

    def test_station_hour_respects_filters_and_validates_limit(self):
        top_one = self.analytics.station_hour({'limit': '1'})
        self.assertEqual([row['station'] for row in top_one['stations']], ['001'])
        self.assertEqual(len(top_one['data']), 24)
        android_only = self.analytics.station_hour({'platform': 'android'})
        self.assertEqual([row['station'] for row in android_only['stations']], ['001'])
        self.assertEqual(android_only['sessions'], 1)
        ios_only = self.analytics.station_hour({'platform': 'ios'})
        self.assertEqual([row['station'] for row in ios_only['stations']], ['001', '002'])
        self.assertEqual(ios_only['sessions'], 2)
        for filters in ({'limit': '0'}, {'limit': '51'}, {'limit': 'x'},
                        {'unknown': '1'}, {'platform': 'symbian'}):
            with self.assertRaises(ValueError):
                self.analytics.station_hour(dict(filters))

    def test_actual_http_filters_and_errors(self):
        server, thread = self.serve()
        try:
            base = f'http://127.0.0.1:{server.server_port}'
            with urlopen(base + '/api/overview?energy=zero') as response:
                self.assertEqual(json.load(response)['data']['summary']['sessions'], 1)
            with urlopen(base + '/api/overview?time_period=peak') as response:
                self.assertEqual(json.load(response)['data']['summary']['sessions'], 2)
            with urlopen(base + '/api/health') as response:
                self.assertEqual(json.load(response)['data']['rule_version'], RULE_VERSION)
            with urlopen(base + '/api/station-hour?limit=5') as response:
                heat = json.load(response)['data']
            self.assertEqual(len(heat['data']), len(heat['stations']) * 24)
            for suffix in ['/api/overview?energy=wrong', '/api/overview?platform=ios&platform=android',
                           '/api/overview?time_period=off_peak',
                           '/api/station-hour?limit=0', '/api/station-hour?unknown=1']:
                with self.assertRaises(HTTPError) as error:
                    urlopen(base + suffix)
                self.assertEqual(error.exception.code, 400)
            with self.assertRaises(HTTPError) as error:
                urlopen(base + '/api/not-found')
            self.assertEqual(error.exception.code, 404)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def serve(self, model=None):
        """起真实 HTTP 服务；本机若禁止 loopback 连接则跳过，而不是伪装通过。"""
        server = create_server(self.root, 0, model)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            urlopen(f'http://127.0.0.1:{server.server_port}/api/health', timeout=2).close()
        except OSError as error:
            server.shutdown()
            server.server_close()
            thread.join()
            self.skipTest(f'当前环境不允许 loopback 连接（{error}）；请在可访问 127.0.0.1 的机器上运行本用例')
        return server, thread

    def test_report_export_and_forecast_endpoint(self):
        rows, summary, version = self.analytics.report('start_hour', {})
        self.assertEqual(len(rows), 24)
        self.assertIsInstance(version, str)
        self.assertEqual(summary['sessions'], 3)
        period_rows, _, _ = self.analytics.report('time_period', {})
        self.assertEqual([row['key'] for row in period_rows], ['peak', 'normal'])
        with self.assertRaises(ValueError):
            self.analytics.report('user_id', {})
        model = dict(method='seasonal', version='test-model',
                     hour_profile={str(h): (5.0 if h == 9 else 1.0) for h in range(24)},
                     weekday_factor={'Mon': 1.0}, energy_per_session=2.0,
                     evaluation={'seasonal': {'mae': 0.5}}, caveats=['课程数据限制'])
        model_path = self.root / 'model.json'
        model_path.write_text(json.dumps(model), encoding='utf-8')
        server, thread = self.serve(str(model_path))
        try:
            base = f'http://127.0.0.1:{server.server_port}'
            with urlopen(base + '/api/report?kind=time_period&energy=zero') as response:
                text = response.read().decode('utf-8-sig')
                self.assertTrue(response.headers['Content-Type'].startswith('text/csv'))
            self.assertIn('estimated_fee_model', text.splitlines()[0])
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
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == '__main__':
    unittest.main()
