import importlib.util
from pathlib import Path
import sqlite3
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py')
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value

data=module('acceptance_data');demo=module('run_demo')

class AcceptanceDataTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'demo.db';demo.prepare_database(self.path)

    def test_snapshot_is_read_only(self):
        before=self.path.read_bytes();result=data.snapshot(self.path)
        self.assertIn('今日营收（元）',result);self.assertEqual(self.path.read_bytes(),before)

    def test_zero_fixture_is_scoped_and_rejects_active_order(self):
        with sqlite3.connect(self.path) as db:
            db.execute("INSERT INTO users(phone,balance) VALUES('13800138088',100)")
            user=db.execute("SELECT id FROM users WHERE phone='13800138088'").fetchone()[0]
            other=db.execute("SELECT SUM(balance) FROM users WHERE id<>?",(user,)).fetchone()[0]
            db.execute("INSERT INTO piles(id,station_id,code,power_kw) VALUES(999,1,'TEST-P999',60)")
            db.execute('INSERT INTO orders(user_id,pile_id,station_id,state) VALUES(?,999,1,0)',(user,))
        with self.assertRaises(RuntimeError):data.prepare('zero',self.path)
        with sqlite3.connect(self.path) as db:
            self.assertEqual(db.execute('SELECT balance FROM users WHERE id=?',(user,)).fetchone()[0],100)
            db.execute('UPDATE orders SET state=1 WHERE user_id=?',(user,))
        data.prepare('zero',self.path)
        with sqlite3.connect(self.path) as db:
            self.assertEqual(db.execute('SELECT balance FROM users WHERE id=?',(user,)).fetchone()[0],0)
            self.assertEqual(db.execute('SELECT SUM(balance) FROM users WHERE id<>?',(user,)).fetchone()[0],other)

    def test_discount_keeps_order_samples_and_no_verdict(self):
        with sqlite3.connect(self.path) as db:
            originals=db.execute('SELECT id,real_power FROM pile_power_logs WHERE order_id IS NOT NULL').fetchall()
            orders=db.execute('SELECT COUNT(*),SUM(amount) FROM orders').fetchone()
            db.execute("INSERT INTO piles(id,station_id,code,power_kw) VALUES(999,1,'TEST-P999',60)")
        data.prepare('discount',self.path)
        with sqlite3.connect(self.path) as db:
            self.assertEqual(db.execute('SELECT id,real_power FROM pile_power_logs WHERE order_id IS NOT NULL').fetchall(),originals)
            self.assertEqual(db.execute('SELECT COUNT(*),SUM(amount) FROM orders').fetchone(),orders)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM pile_power_logs WHERE pile_id=999 AND order_id IS NULL').fetchone()[0],12)
            self.assertEqual(db.execute('PRAGMA foreign_key_check').fetchall(),[])

    def test_invalid_scenario_does_not_modify(self):
        before=self.path.read_bytes()
        with self.assertRaises(ValueError):data.prepare('unknown',self.path)
        self.assertEqual(self.path.read_bytes(),before)

if __name__=='__main__':unittest.main()
