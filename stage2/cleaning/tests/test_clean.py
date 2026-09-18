import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('clean', Path(__file__).parents[1] / 'clean.py')
clean = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clean)


class CleaningTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.station = dict(stationId='001', locationId='01', facilityType='4', station_name=' 测试站 ',
                            address='地址', device_count='2', open_time='00:00-24:00', update_time='2019/7/26')
        self.session = dict(sessionId='0001', kwhTotal='0', charging_fees='0', created='0015-01-05 10:00:00',
                            ended='0015-01-05 11:00:00', startTime='10', endTime='11', chargeTimeHrs='1',
                            weekday='Mon', platform='IOS', userId='0002', stationId='001', locationId='01',
                            managerVehicle='0', facilityType='4', Mon='1', Tues='0', Wed='0', Thurs='0', Fri='0', Sat='0', Sun='0')
        self.battery = {'esd': '0001', 'record_time': '2.02E+13', 'soc': '50', 'pack_voltage (V)': '350',
                        'charge_current (A)': '-70', 'max_cell_voltage (V)': '4', 'min_cell_voltage (V)': '3.9',
                        'max_temperature (℃)': '35', 'min_temperature (℃)': '30',
                        'available_energy (kw)': '10', 'available_capacity (Ah)': '20'}

    def inputs(self, sessions=None, battery=None):
        for name, rows in [('stations', [self.station]), ('sessions', sessions or [self.session]), ('battery', battery or [self.battery])]:
            clean.save_csv(self.source / clean.FILES[name], rows)

    def read(self, output, name):
        with (output / name).open(encoding='utf-8-sig') as stream:
            return list(csv.DictReader(stream))

    def test_preserves_uncertainty_zero_and_identifiers(self):
        self.inputs()
        output = self.root / 'out'
        clean.run(self.source, output)
        row = self.read(output, 'dwd/sessions.csv')[0]
        self.assertEqual(row['session_id'], '0001')
        self.assertEqual(row['kwh'], '0')
        self.assertEqual(row['fee_original'], '0')
        self.assertEqual(row['calendar_date'], '')
        self.assertEqual(row['facility_type'], '4')
        battery = self.read(output, 'dwd/battery.csv')[0]
        self.assertEqual(battery['current_a'], '-70')
        self.assertEqual(battery['timestamp'], '')
        self.assertIn('outside_tutorial_current_range_retained', battery['quality_flags'])

    def test_duplicates_conflicts_and_negative_energy_are_isolated(self):
        duplicate = dict(self.session)
        negative = dict(self.session, sessionId='bad', kwhTotal='-1')
        conflict1 = dict(self.session, sessionId='conflict', kwhTotal='2')
        conflict2 = dict(conflict1, kwhTotal='3')
        self.inputs([self.session, duplicate, negative, conflict1, conflict2])
        output = self.root / 'out'
        result = clean.run(self.source, output)
        self.assertEqual(result['reconciliation']['sessions'], dict(input=5, retained=1, quarantined=4))
        reasons = [r['reason'] for r in self.read(output, 'quarantine/sessions.csv')]
        self.assertEqual(reasons.count('conflicting_primary_key'), 2)
        self.assertIn('exact_duplicate', reasons)

    def test_foreign_key_and_battery_bounds(self):
        self.inputs([dict(self.session, stationId='missing')], [dict(self.battery, soc='101')])
        result = clean.run(self.source, self.root / 'out')
        self.assertEqual(result['reconciliation']['sessions']['quarantined'], 1)
        self.assertEqual(result['reconciliation']['battery']['quarantined'], 1)

    def test_decimal_aggregates_reproducible_and_overwrite_refused(self):
        self.inputs([dict(self.session, kwhTotal='0.1'), dict(self.session, sessionId='0003', kwhTotal='0.2')])
        first = clean.run(self.source, self.root / 'a')
        second = clean.run(self.source, self.root / 'b')
        self.assertEqual(first, second)
        self.assertEqual(first['total_kwh'], '0.3')
        group = self.read(self.root / 'a', 'ads/by_station_id.csv')[0]
        self.assertEqual(group['session_count'], '2')
        self.assertEqual(group['total_kwh'], '0.3')
        with self.assertRaises(ValueError):
            clean.run(self.source, self.root / 'a')

    def test_peak_period_tariff_and_estimated_fee_follow_contract(self):
        """C5/C6/C7：时段边界、单价与估算电费按共享定义模块计算，不由本文件另定一份。"""
        cases = {'0': ('off_peak', '0.7'), '7': ('off_peak', '0.7'), '8': ('peak', '1.5'),
                 '11': ('peak', '1.5'), '12': ('normal', '1.0'), '17': ('normal', '1.0'),
                 '18': ('peak', '1.5'), '21': ('peak', '1.5'), '22': ('off_peak', '0.7'),
                 '23': ('off_peak', '0.7')}
        sessions = []
        for hour in cases:
            # 23 点跨零点：结束小时回到 0，日期进一位
            end_hour, end_date = (0, '0015-01-06') if hour == '23' else (int(hour) + 1, '0015-01-05')
            sessions.append(dict(self.session, sessionId=f'{hour:0>4}', startTime=hour, endTime=str(end_hour),
                                 kwhTotal='2', created=f'0015-01-05 {int(hour):02d}:00:00',
                                 ended=f'{end_date} {end_hour:02d}:00:00'))
        self.inputs(sessions)
        output = self.root / 'out'
        clean.run(self.source, output)
        self.assertEqual(len(self.read(output, 'dwd/sessions.csv')), len(cases))
        for row in self.read(output, 'dwd/sessions.csv'):
            hour = int(row['start_hour'])
            period, price = cases[str(hour)]
            self.assertEqual(row['time_period'], period, f'{hour} 点时段')
            self.assertEqual(row['unit_price'], price, f'{hour} 点单价')
            self.assertEqual(clean.Decimal(row['estimated_fee']), clean.Decimal('2') * clean.Decimal(price))

    def test_facility_label_table_and_unmapped_code(self):
        """C5：1/2/3 有中文标签，其他编码保留原编码并标记待核，不猜语义。"""
        self.inputs([dict(self.session, facilityType='1')])
        clean.save_csv(self.source / clean.FILES['stations'], [dict(self.station, facilityType='1')])
        output = self.root / 'out'
        clean.run(self.source, output)
        self.assertEqual(self.read(output, 'dwd/stations.csv')[0]['facility_label'], '直流')
        self.assertEqual(self.read(output, 'dwd/sessions.csv')[0]['facility_label'], '直流')
        session = self.read(output, 'dwd/sessions.csv')[0]
        self.assertIn('facility_dictionary_assumed', session['quality_flags'])


if __name__ == '__main__':
    unittest.main()
