#!/usr/bin/env python3
"""第二阶段分层存储：把 ODS/DWD/ADS 与采集产物装载进本地数仓，并建模 DWS 汇总层。

分层设计（对齐课程要求）：
  ODS 原始层 -> 清洗前的原样数据（含采集产物）
  DWD 明细层 -> 清洗后的标准化明细
  DWS 汇总层 -> 本脚本从 DWD 建模得到（站点×时段、设施×平台、星期×时段、用户行为）
  ADS 应用层 -> 直接支撑大屏的应用聚合
本机以 SQLite 充当数仓（等价 Hive/ClickHouse 的表与 SQL 查询），分层与字段定义保持不变。
"""
import argparse
import csv
import hashlib
import json
import sqlite3
import sys
from collections import Counter, defaultdict
from decimal import Decimal
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'common'))
from contract import load_cleaning  # noqa: E402

LAYER_TABLES = {
    'ods': [('ods/sessions_source.csv', 'ods_sessions_source'),
            ('ods/dsv13r2.csv', 'ods_dsv13r2'),
            ('ods/nvv2t.csv', 'ods_nvv2t'),
            ('ods/nvv2t_md_end.csv', 'ods_nvv2t_md_end')],
    'dwd': [('dwd/sessions.csv', 'dwd_sessions'),
            ('dwd/stations.csv', 'dwd_stations'),
            ('dwd/battery.csv', 'dwd_battery')],
    'ads': [('ads/by_station_id.csv', 'ads_by_station_id'),
            ('ads/by_facility_type.csv', 'ads_by_facility_type'),
            ('ads/by_platform.csv', 'ads_by_platform'),
            ('ads/by_start_hour.csv', 'ads_by_start_hour'),
            ('ads/by_weekday.csv', 'ads_by_weekday'),
            ('ads/by_time_period.csv', 'ads_by_time_period')],
    'audit': [('audit/row_actions.csv', 'audit_row_actions'),
              ('audit/rule_counts.csv', 'audit_rule_counts')],
    'quarantine': [('quarantine/sessions.csv', 'quarantine_sessions'),
                   ('quarantine/stations.csv', 'quarantine_stations'),
                   ('quarantine/battery.csv', 'quarantine_battery')],
}
INGEST_TABLES = {
    'business': ('ods/business', 'ingest_business_'),
    'logs': ('ods/logs', 'ingest_logs_'),
    'stream': ('ods/stream', 'ingest_stream_'),
    'files': ('ods/files', 'ingest_files_'),
}


def infer_type(values):
    """按样本推断列类型：整数/实数/文本，避免把 ID 当数字丢失前导零。"""
    sample = [v for v in values if v not in ('', None)][:200]
    if not sample:
        return 'TEXT'
    if all(v.lstrip('-').isdigit() and not (len(v) > 1 and v.startswith('0')) for v in sample):
        return 'INTEGER'
    try:
        for v in sample:
            Decimal(v)
        return 'REAL'
    except (ArithmeticError, ValueError):
        return 'TEXT'


def quote(name):
    return '"' + name.replace('"', '""') + '"'


def load_csv(connection, path, table):
    path = Path(path)
    if not path.exists():
        return None
    with path.open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.reader(stream)
        try:
            columns = next(reader)
        except StopIteration:
            return None
        rows = [row for row in reader]
    types = [infer_type([row[i] if i < len(row) else '' for row in rows]) for i in range(len(columns))]
    connection.execute(f'drop table if exists {quote(table)}')
    definition = ', '.join(f'{quote(c)} {t}' for c, t in zip(columns, types))
    connection.execute(f'create table {quote(table)} ({definition})')
    placeholders = ', '.join('?' * len(columns))
    connection.executemany(
        f'insert into {quote(table)} values ({placeholders})',
        [[(None if (i >= len(row) or row[i] == '') else row[i]) for i in range(len(columns))] for row in rows])
    return dict(table=table, layer=table.split('_')[0], source=str(path), rows=len(rows),
                columns=columns, sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def load_directory(connection, root, mapping, ledger):
    for relative, table in mapping:
        info = load_csv(connection, Path(root) / relative, table)
        if info:
            ledger.append(info)


def load_ingest(connection, ingest_root, ledger):
    if not ingest_root or not Path(ingest_root).exists():
        return
    for group, (relative, prefix) in INGEST_TABLES.items():
        folder = Path(ingest_root) / relative
        if not folder.is_dir():
            continue
        for path in sorted(folder.glob('*.csv')):
            table = (prefix + path.stem).lower().replace('-', '_').replace(' ', '_')
            info = load_csv(connection, path, table)
            if info:
                info['layer'] = 'ods'
                info['group'] = group
                ledger.append(info)


def build_dws(connection, ledger):
    """从 DWD 明细建模 DWS 汇总层（等价 Hive 宽表聚合）。"""
    statements = {
        'dws_station_hour': """
            create table dws_station_hour as
            select station_id, start_hour,
                   count(*) as session_count,
                   round(sum(cast(kwh as real)), 2) as total_kwh,
                   sum(case when cast(kwh as real) = 0 then 1 else 0 end) as zero_energy_count,
                   round(avg(cast(duration_hours as real)), 4) as avg_duration_hours
            from dwd_sessions
            group by station_id, start_hour""",
        'dws_facility_platform': """
            create table dws_facility_platform as
            select facility_type, platform,
                   count(*) as session_count,
                   round(sum(cast(kwh as real)), 2) as total_kwh,
                   round(avg(cast(kwh as real)), 4) as avg_kwh
            from dwd_sessions
            group by facility_type, platform""",
        'dws_weekday_hour': """
            create table dws_weekday_hour as
            select weekday, start_hour, count(*) as session_count,
                   round(sum(cast(kwh as real)), 2) as total_kwh
            from dwd_sessions
            where weekday_usable = '1' and time_of_day_usable = '1'
            group by weekday, start_hour""",
        'dws_station_quality': """
            create table dws_station_quality as
            select station_id, count(*) as session_count,
                   sum(case when quality_flags is not null and quality_flags <> '' then 1 else 0 end) as flagged_count
            from dwd_sessions
            group by station_id""",
        # 峰谷时段定义见 stage2/common/contract.py：高峰/平时/低谷与对应电价由清洗层派生，本层只聚合。
        'dws_period_profile': """
            create table dws_period_profile as
            select time_period,
                   count(*) as session_count,
                   round(sum(cast(kwh as real)), 2) as total_kwh,
                   round(sum(cast(estimated_fee as real)), 2) as estimated_fee_total,
                   round(avg(cast(kwh as real)), 4) as avg_kwh
            from dwd_sessions
            where time_period is not null and time_period <> ''
            group by time_period""",
        'dws_station_period': """
            create table dws_station_period as
            select station_id, time_period,
                   count(*) as session_count,
                   round(sum(cast(kwh as real)), 2) as total_kwh,
                   round(sum(cast(estimated_fee as real)), 2) as estimated_fee_total
            from dwd_sessions
            where time_period is not null and time_period <> ''
            group by station_id, time_period""",
    }
    for table, sql in statements.items():
        connection.execute(f'drop table if exists {table}')
        connection.execute(sql)
        rows = connection.execute(f'select count(*) from {table}').fetchone()[0]
        ledger.append(dict(table=table, layer='dws', source='dwd_sessions（建模）', rows=rows,
                           columns=[d[0] for d in connection.execute(f'select * from {table} limit 0').description],
                           sha256=''))


def build_user_behavior(connection, ledger):
    """用户行为汇总：user_id 先做 SHA256 截断后再落库，公开产物不保留原标识。"""
    profile = defaultdict(lambda: dict(sessions=0, kwh=Decimal(0), duration=Decimal(0), platforms=set()))
    for user_id, kwh, duration, platform in connection.execute(
            'select user_id, kwh, duration_hours, platform from dwd_sessions'):
        key = hashlib.sha256(str(user_id).encode()).hexdigest()[:16]
        row = profile[key]
        row['sessions'] += 1
        row['kwh'] += Decimal(str(kwh or 0))
        row['duration'] += Decimal(str(duration or 0))
        row['platforms'].add(platform or 'unknown')
    connection.execute('drop table if exists dws_user_behavior')
    connection.execute('create table dws_user_behavior (user_key text, session_count integer, '
                       'total_kwh real, avg_duration_hours real, platform_count integer)')
    connection.executemany('insert into dws_user_behavior values (?,?,?,?,?)',
                           [(key, row['sessions'], float(row['kwh']),
                             float(row['duration'] / row['sessions']) if row['sessions'] else 0.0,
                             len(row['platforms'])) for key, row in sorted(profile.items())])
    ledger.append(dict(table='dws_user_behavior', layer='dws', source='dwd_sessions（user_id 已哈希）',
                       rows=len(profile), columns=['user_key', 'session_count', 'total_kwh',
                                                   'avg_duration_hours', 'platform_count'], sha256=''))


def build_ads_reconciliation(connection, ledger):
    """ADS 汇总层：站点维表关联后的应用表，供大屏直接查询。"""
    connection.execute('drop table if exists ads_station_profile')
    connection.execute("""
        create table ads_station_profile as
        select d.station_id, s.station_name,
               sum(d.session_count) as session_count,
               round(sum(d.total_kwh), 2) as total_kwh,
               round(sum(d.total_kwh) / sum(d.session_count), 4) as avg_kwh
        from dws_station_hour d left join dwd_stations s on s.stationId = d.station_id
        group by d.station_id""")
    connection.execute('drop table if exists ads_kpi_daily')
    connection.execute("""
        create table ads_kpi_daily as
        select count(*) as session_count,
               round(sum(cast(kwh as real)), 2) as total_kwh,
               sum(case when cast(kwh as real) > 0 then 1 else 0 end) as positive_energy_count,
               sum(case when cast(kwh as real) = 0 then 1 else 0 end) as zero_energy_count,
               round(avg(cast(duration_hours as real)), 4) as avg_duration_hours,
               count(distinct station_id) as active_stations
        from dwd_sessions""")
    for table, source in (('ads_station_profile', 'dws_station_hour + dwd_stations'),
                           ('ads_kpi_daily', 'dwd_sessions')):
        rows = connection.execute(f'select count(*) from {table}').fetchone()[0]
        ledger.append(dict(table=table, layer='ads', source=source, rows=rows,
                           columns=[d[0] for d in connection.execute(f'select * from {table} limit 0').description],
                           sha256=''))


def build_data_dictionary(ledger, path, database):
    lines = ['# 数仓数据字典', '',
             f'数据库：`{database}`；表数量：{len(ledger)}。',
             '分层：ODS 原始层 / DWD 明细层 / DWS 汇总层 / ADS 应用层，另含审计与隔离表。', '',
             '| 层 | 表 | 行数 | 来源 | 列 |', '|---|---|---:|---|---|']
    for info in ledger:
        columns = '、'.join(f'`{c}`' for c in info['columns'])
        lines.append(f'| {info["layer"].upper()} | `{info["table"]}` | {info["rows"]} '
                     f'| {info["source"]} | {columns} |')
    lines += ['', '## 数据说明', '',
              '- `dwd_sessions.kwh` 为原始合法电量；`user_id` 只在 DWD 保留，DWS 用户行为表使用哈希键。',
             '- 原字段费用语义与币种未核实，不进入 DWS 营收类指标。',
             '- `estimated_fee` 为峰谷按项目设定电价估算的派生列（高峰1.5/平时1.0/低谷0.7 元×kWh），'
             '仅作参考估算对比，不代表真实收费或营收。',
             '- `time_period` 取值为 `peak`/`normal`/`off_peak`，仅在开始小时通过校验的行上有值。',
              '- 年份不可信，任何按真实日期的分组都不在本数仓提供。',
              '- 采集产物以 `ingest_` 前缀进入 ODS，与课程源文件并存，便于跨源核对。', '']
    Path(path).write_text('\n'.join(lines) + '\n', encoding='utf-8')


def run(data, ingest=None, database='warehouse.db', dictionary=None, manifest=None):
    # 消费方契约校验：缺列/版本不符/对账不一致都在这里拦下，不带着坏产物建仓。
    load_cleaning(data)
    database = Path(database)
    if database.exists():
        raise SystemExit(f'数仓已存在，拒绝覆盖已有证据：{database}')
    connection = sqlite3.connect(database)
    ledger = []
    try:
        load_directory(connection, data, LAYER_TABLES['ods'], ledger)
        load_directory(connection, data, LAYER_TABLES['dwd'], ledger)
        load_directory(connection, data, LAYER_TABLES['ads'], ledger)
        load_directory(connection, data, LAYER_TABLES['audit'], ledger)
        load_directory(connection, data, LAYER_TABLES['quarantine'], ledger)
        load_ingest(connection, ingest, ledger)
        if not connection.execute("select name from sqlite_master where name='dwd_sessions'").fetchone():
            raise SystemExit(f'未找到 dwd/sessions.csv，请检查清洗结果目录：{data}')
        build_dws(connection, ledger)
        build_user_behavior(connection, ledger)
        build_ads_reconciliation(connection, ledger)
        connection.commit()
    finally:
        connection.close()
    if dictionary:
        build_data_dictionary(ledger, dictionary, database)
    summary = dict(database=str(database), tables=len(ledger),
                   layers={layer: sum(1 for i in ledger if i['layer'] == layer)
                           for layer in sorted({i['layer'] for i in ledger})},
                   rows=sum(i['rows'] for i in ledger), tables_detail=ledger)
    if manifest:
        Path(manifest).write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return summary


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', required=True, help='清洗结果目录（含 ods/dwd/ads/audit/quarantine）')
    parser.add_argument('--ingest', help='采集产物目录（可选，作为 ODS 扩展）')
    parser.add_argument('--db', default='warehouse.db', help='输出的 SQLite 数仓路径')
    parser.add_argument('--dictionary', help='数据字典输出路径（.md）')
    parser.add_argument('--manifest', help='装载清单输出路径（.json）')
    args = parser.parse_args(argv)
    summary = run(args.data, args.ingest, args.db, args.dictionary, args.manifest)
    print(json.dumps({k: summary[k] for k in ('database', 'tables', 'layers', 'rows')}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
