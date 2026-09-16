#!/usr/bin/env python3
"""第二阶段多源数据采集：业务库 / 日志文件 / 批量数据文件 / 设备与事件流 -> ODS 原始层。

只使用 Python 标准库，不依赖 Spark/Kafka/Hadoop 运行时；每个采集器对应课程要求中的
一类数据源，采集清单记录来源、行数、列名与 SHA256，供后续清洗与审计使用。
"""
import argparse
import csv
import hashlib
import json
import re
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

LOG_PATTERN = re.compile(r'^(?:(?P<ts>\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\s+)?'
                         r'\[(?P<module>[^\]]+)\]\s*(?P<message>.*)$')
STREAM_TABLES = {'power': ('pile_power_logs', 'real_power', 'logged_at'),
                 'alarm': ('selfheal_events', 'real_power', 'created_at')}


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 16), b''):
            digest.update(block)
    return digest.hexdigest()


def write_csv(path, columns, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(columns)
        writer.writerows(rows)
    return dict(output=str(path), rows=len(rows), columns=list(columns), sha256=sha256_file(path))


def collect_database(path, out_root):
    """业务数据库同步：等价于 Sqoop/CDC 的全表抽取，逐表落 ODS。"""
    connection = sqlite3.connect(f'file:{Path(path).as_posix()}?mode=ro', uri=True)
    try:
        tables = [r[0] for r in connection.execute(
            "select name from sqlite_master where type='table' and name not like 'sqlite_%' "
            "and name not like '%_fts%' order by name")]
        outputs = []
        for table in tables:
            cursor = connection.execute(f'select * from "{table}"')  # 表名来自 sqlite_master，非外部输入
            columns = [d[0] for d in cursor.description]
            rows = cursor.fetchall()
            outputs.append(dict(table=table, **write_csv(out_root / 'ods' / 'business' / f'{table}.csv',
                                                         columns, rows)))
        return dict(type='db', name='业务数据库（SQLite）', path=str(Path(path).resolve()),
                    tables=len(tables), rows=sum(o['rows'] for o in outputs), outputs=outputs,
                    mapping='等价 Sqoop 全表同步 / CDC 抽取')
    finally:
        connection.close()


def collect_files(directory, out_root):
    """批量数据文件接入：等价于 HDFS 批量导入，保持原字节与文件名。"""
    directory = Path(directory)
    outputs = []
    for path in sorted(p for p in directory.iterdir() if p.suffix.lower() == '.csv'):
        with path.open(encoding='utf-8-sig', newline='') as stream:
            reader = csv.reader(stream)
            columns = next(reader)
            rows = sum(1 for _ in reader)
        target = out_root / 'ods' / 'files' / path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        outputs.append(dict(table=path.stem, **dict(output=str(target), rows=rows,
                                                    columns=columns, sha256=sha256_file(target))))
    return dict(type='file', name='批量数据文件', path=str(directory.resolve()),
                tables=len(outputs), rows=sum(o['rows'] for o in outputs), outputs=outputs,
                mapping='等价 HDFS 批量导入（ods/files 原字节留存）')


def collect_logs(paths, out_root):
    """日志采集：解析 [模块] 消息 结构，等价 Flume/Logstash 的按行采集与结构化。"""
    outputs = []
    for path in paths:
        path = Path(path)
        parsed, unparsed = [], 0
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            line = line.strip()
            if not line:
                continue
            match = LOG_PATTERN.match(line)
            if match:
                parsed.append([match.group('ts') or '', match.group('module'),
                               match.group('message').strip(), 'parsed'])
            else:
                parsed.append(['', '', line, 'unparsed'])
                unparsed += 1
        outputs.append(dict(table=path.stem, unparsed=unparsed,
                            **write_csv(out_root / 'ods' / 'logs' / f'{path.stem}.csv',
                                        ['event_time', 'module', 'message', 'parse_state'], parsed)))
    return dict(type='log', name='应用日志', path=[str(Path(p).resolve()) for p in paths],
                tables=len(outputs), rows=sum(o['rows'] for o in outputs), outputs=outputs,
                mapping='等价 Flume/Logstash 按行采集与结构化')


def _floor_window(stamp, seconds):
    """把事件时间对齐到固定长度窗口（等价 Flink 滚动窗口）。"""
    text = stamp.replace('T', ' ')
    for fmt in ('%Y-%m-%d %H:%M:%S.%f', '%Y-%m-%d %H:%M:%S'):
        try:
            moment = datetime.strptime(text, fmt)
            break
        except ValueError:
            continue
    else:
        return None
    epoch = int(moment.replace(tzinfo=timezone.utc).timestamp())
    return datetime.fromtimestamp(epoch - epoch % seconds, tz=timezone.utc).strftime('%Y-%m-%d %H:%M:%S')


def collect_stream(database, out_root, window_seconds=10):
    """设备与事件流：按事件时间回放并做滚动窗口聚合，等价 Kafka + Flink 窗口语义。"""
    connection = sqlite3.connect(f'file:{Path(database).as_posix()}?mode=ro', uri=True)
    events, aggregates = [], {}
    try:
        for kind, (table, power_column, time_column) in STREAM_TABLES.items():
            try:
                cursor = connection.execute(
                    f'select pile_id, {power_column}, {time_column} from "{table}" order by {time_column}')
            except sqlite3.OperationalError:
                continue
            for pile_id, power, stamp in cursor:
                window = _floor_window(str(stamp), window_seconds)
                events.append([kind, pile_id, power, stamp, window or ''])
                if kind != 'power' or window is None:
                    continue
                bucket = aggregates.setdefault((window, pile_id), [0, 0.0, 0.0])
                bucket[0] += 1
                bucket[1] += float(power or 0)
                bucket[2] = max(bucket[2], float(power or 0))
    finally:
        connection.close()
    window_rows = [[window, pile_id, count, round(total / count, 4), round(peak, 4)]
                   for (window, pile_id), (count, total, peak) in sorted(aggregates.items())]
    outputs = [dict(table='stream_events',
                    **write_csv(out_root / 'ods' / 'stream' / 'stream_events.csv',
                                ['event_kind', 'pile_id', 'real_power', 'event_time', 'window_start'], events)),
               dict(table='windowed_power',
                    **write_csv(out_root / 'ods' / 'stream' / 'windowed_power.csv',
                                ['window_start', 'pile_id', 'sample_count', 'avg_power', 'max_power'], window_rows))]
    return dict(type='stream', name='设备与事件流（功率序列 + 自愈告警）',
                path=str(Path(database).resolve()), window_seconds=window_seconds,
                tables=len(outputs), rows=sum(o['rows'] for o in outputs), outputs=outputs,
                mapping='等价 Kafka 事件流 + Flink 滚动窗口聚合')


def build_manifest(sources, out_root):
    totals = {}
    for source in sources:
        totals[source['type']] = totals.get(source['type'], 0) + source['rows']
    return dict(collected_at=datetime.now().astimezone().isoformat(timespec='seconds'),
                tool='stage2/ingest/collect.py', layer='ODS',
                window_seconds=next((s['window_seconds'] for s in sources if s['type'] == 'stream'), None),
                totals=totals, total_rows=sum(totals.values()), sources=sources)


def build_report(manifest):
    lines = ['# 采集清单', '',
             f'采集时间：{manifest["collected_at"]}；采集层级：ODS；总行数：{manifest["total_rows"]}。', '',
             '| 数据源 | 类型 | 表/文件数 | 行数 | 等价技术 |', '|---|---|---:|---:|---|']
    for source in manifest['sources']:
        lines.append(f'| {source["name"]} | {source["type"]} | {source["tables"]} '
                     f'| {source["rows"]} | {source["mapping"]} |')
    lines += ['', '## 明细', '']
    for source in manifest['sources']:
        lines.append(f'### {source["name"]}（{source["type"]}）')
        lines.append('')
        lines.append('| 表/文件 | 行数 | 列数 | SHA256 |')
        lines.append('|---|---:|---:|---|')
        for output in source['outputs']:
            lines.append(f'| {output["table"]} | {output["rows"]} | {len(output["columns"])} '
                         f'| {output["sha256"][:16]}… |')
        lines.append('')
    return '\n'.join(lines) + '\n'


def run(database=None, csv_dir=None, logs=(), stream_database=None, out=None, window_seconds=10):
    out_root = Path(out)
    if out_root.exists():
        raise SystemExit(f'输出目录已存在，拒绝覆盖已有采集证据：{out_root}')
    checks = [(database, '业务数据库'), (csv_dir, '批量数据文件目录')] + [(p, '日志文件') for p in logs]
    for path, label in checks:
        if path and not Path(path).exists():
            raise SystemExit(f'{label}不存在：{path}')
    sources = []
    if database:
        sources.append(collect_database(database, out_root))
    if csv_dir:
        sources.append(collect_files(csv_dir, out_root))
    if logs:
        sources.append(collect_logs(logs, out_root))
    stream_source = stream_database or database
    if stream_source and Path(stream_source).exists():
        sources.append(collect_stream(stream_source, out_root, window_seconds))
    if not sources:
        raise SystemExit('没有可采集的数据源，请至少指定 --db / --csv / --log')
    manifest = build_manifest(sources, out_root)
    out_root.mkdir(parents=True, exist_ok=True)
    (out_root / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    (out_root / '采集清单.md').write_text(build_report(manifest))
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', help='业务数据库（SQLite）路径')
    parser.add_argument('--csv', help='批量数据文件目录')
    parser.add_argument('--log', action='append', default=[], help='日志文件，可重复')
    parser.add_argument('--stream-db', help='设备与事件流的来源库，默认同 --db')
    parser.add_argument('--window-seconds', type=int, default=10, help='流式窗口长度，默认 10 秒')
    parser.add_argument('--out', required=True, help='ODS 输出目录，必须不存在')
    args = parser.parse_args(argv)
    manifest = run(args.db, args.csv, args.log, args.stream_db, args.out, args.window_seconds)
    print(json.dumps({'total_rows': manifest['total_rows'], 'totals': manifest['totals'],
                      'out': str(Path(args.out).resolve())}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
