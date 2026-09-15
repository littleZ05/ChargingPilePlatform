import importlib.util
from pathlib import Path
import sqlite3
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('run_demo', Path(__file__).parents[1] / 'run_demo.py')
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)


class DemoDataTest(unittest.TestCase):
    def test_default_preserves_existing_and_reset_backs_up(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'demo.db'
            schema = 'CREATE TABLE marker(value INTEGER);'
            demo.prepare_database(path, schema=schema, seed='INSERT INTO marker VALUES(42);')
            before = path.read_bytes()
            self.assertIsNone(demo.prepare_database(path, schema='invalid SQL'))
            self.assertEqual(path.read_bytes(), before)
            backup = demo.prepare_database(path, True, schema, 'INSERT INTO marker VALUES(7);')
            with sqlite3.connect(backup) as db:
                self.assertEqual(db.execute('SELECT value FROM marker').fetchone()[0], 42)
            with sqlite3.connect(path) as db:
                self.assertEqual(db.execute('SELECT value FROM marker').fetchone()[0], 7)

    def test_failed_seed_leaves_original_intact(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'demo.db'
            demo.prepare_database(path, schema='CREATE TABLE marker(value);', seed='')
            before = path.read_bytes()
            with self.assertRaises(sqlite3.Error):
                demo.prepare_database(path, True, 'CREATE TABLE marker(value);', 'invalid SQL')
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(list(Path(directory).glob('demo-new-*')), [])

    def test_actual_schema_seed_integrity(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'demo.db'
            demo.prepare_database(path)
            with sqlite3.connect(path) as db:
                self.assertEqual(db.execute('SELECT COUNT(*) FROM stations').fetchone()[0], 5)
                self.assertEqual(db.execute('SELECT COUNT(*) FROM piles').fetchone()[0], 37)
                self.assertEqual(db.execute('PRAGMA foreign_key_check').fetchall(), [])


if __name__ == '__main__':
    unittest.main()
