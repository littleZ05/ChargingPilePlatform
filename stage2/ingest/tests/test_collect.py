"""采集层单测：多源接入、日志结构化、流式窗口、清单哈希与失败保护。"""
import csv
import hashlib
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import collect


class CollectTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.db = self.root / 'business.db'
        connection = sqlite3.connect(self.db)
        connection.executescript(
            'create table orders (id integer primary key, user_id integer, kwh real, state integer);'
            'create table pile_power_logs (id integer primary key, pile_id integer, real_power real,'
            ' logged_at text);'
            'create table selfheal_events (id integer primary key, pile_id integer, level integer,'
            ' real_power real, created_at text);'
            "insert into orders values (1,1,12.5,1),(2,2,0.0,2);"
            "insert into pile_power_logs values (1,7,100.0,'2026-09-16 10:00:03'),"
            "(2,7,120.0,'2026-09-16 10:00:07'),(3,7,80.0,'2026-09-16 10:00:12');"
            "insert into selfheal_events values (1,7,2,18.0,'2026-09-16 10:00:09');")
        connection.commit()
        connection.close()
        self.csv_dir = self.root / 'batch'
        self.csv_dir.mkdir()
        (self.csv_dir / 'sessions.csv').write_text('sessionId,kwh\n1,3.5\n2,0\n', encoding='utf-8')
        self.log = self.root / 'PcServer.log'
        self.log.write_text('[net] Socket 服务已监听端口 9999\n'
                            '2026-09-16 10:42:27 [自愈检查] S03-P02：连续3次功率低于阈值\n'
                            '裸行没有模块标记\n', encoding='utf-8')

    def run_collect(self, **overrides):
        options = dict(database=self.db, csv_dir=self.csv_dir, logs=[self.log],
                       out=self.root / 'ods-run', window_seconds=10)
        options.update(overrides)
        return collect.run(**options)

    def test_multi_source_rows_and_manifest_hashes(self):
        manifest = self.run_collect()
        self.assertEqual(manifest['layer'], 'ODS')
        sources = {source['type']: source for source in manifest['sources']}
        self.assertEqual(set(sources), {'db', 'file', 'log', 'stream'})
        self.assertEqual(sources['db']['rows'], 2 + 3 + 1)          # orders + power + selfheal
        self.assertEqual(sources['file']['rows'], 2)
        self.assertEqual(sources['log']['rows'], 3)
        self.assertEqual(sources['stream']['rows'], 3 + 1 + 2)      # events + alarm + windows
        self.assertEqual(manifest['total_rows'], sum(s['rows'] for s in manifest['sources']))
        for source in manifest['sources']:
            for output in source['outputs']:
                path = Path(output['output'])
                self.assertTrue(path.exists(), path)
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), output['sha256'])

    def test_batch_files_are_byte_identical(self):
        self.run_collect()
        copied = self.root / 'ods-run' / 'ods' / 'files' / 'sessions.csv'
        self.assertEqual(copied.read_bytes(), (self.csv_dir / 'sessions.csv').read_bytes())

    def test_log_structure_and_report(self):
        self.run_collect()
        with (self.root / 'ods-run' / 'ods' / 'logs' / 'PcServer.csv').open(encoding='utf-8') as stream:
            parsed = list(csv.DictReader(stream))
        self.assertEqual([row['parse_state'] for row in parsed], ['parsed', 'parsed', 'unparsed'])
        self.assertEqual(parsed[0]['module'], 'net')
        self.assertEqual(parsed[1]['event_time'], '2026-09-16 10:42:27')
        report = (self.root / 'ods-run' / '采集清单.md').read_text(encoding='utf-8')
        self.assertIn('Flume/Logstash', report)
        self.assertIn('Flink 滚动窗口', report)

    def test_stream_window_aggregation(self):
        self.run_collect()
        with (self.root / 'ods-run' / 'ods' / 'stream' / 'windowed_power.csv').open(encoding='utf-8') as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(len(rows), 2)                              # 10:00:00 窗口2条 + 10:00:10 窗口1条
        self.assertEqual(rows[0]['window_start'], '2026-09-16 10:00:00')
        self.assertEqual(rows[0]['sample_count'], '2')
        self.assertEqual(rows[0]['avg_power'], '110.0')
        self.assertEqual(rows[0]['max_power'], '120.0')
        self.assertEqual(rows[1]['sample_count'], '1')

    def test_refuses_overwrite_and_missing_input(self):
        (self.root / 'ods-run').mkdir()
        with self.assertRaises(SystemExit):
            self.run_collect()
        with self.assertRaises(SystemExit):
            self.run_collect(csv_dir=self.root / 'not-exist', out=self.root / 'ods-run-2')
        self.assertFalse((self.root / 'ods-run-2').exists())

    def test_manifest_is_json_serialisable_and_written(self):
        manifest = self.run_collect()
        stored = json.loads((self.root / 'ods-run' / 'manifest.json').read_text(encoding='utf-8'))
        self.assertEqual(stored['total_rows'], manifest['total_rows'])
        self.assertEqual(stored['tool'], 'stage2/ingest/collect.py')


if __name__ == '__main__':
    unittest.main()
