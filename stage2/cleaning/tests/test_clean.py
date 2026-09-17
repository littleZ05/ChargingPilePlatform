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


if __name__ == '__main__':
    unittest.main()
