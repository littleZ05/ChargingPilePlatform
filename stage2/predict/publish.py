#!/usr/bin/env python3
"""把预测结果落到数仓：dws_day_hour / dws_station_features / ads_station_busyness /
ads_forecast / ads_session_quantile。

为什么要在预测之后单独落一次库：数仓装载（warehouse/load.py）发生在训练之前，
那时候还没有模型；而课程的"数据存储"环节要求预测结果也要能按层查到，
不能只躺在 JSON 文件里。本脚本只读模型产物，不重新训练，
因此重跑代价是秒级，可以安全地跟在 train_v2.py 后面。

写入策略：这几张表由本脚本独占，每次重跑先 drop 再 create，保证结果与模型版本一致；
其他层（ODS/DWD/DWS/ADS）的表一概不碰。
"""
import argparse
import json
import sqlite3
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import serving  # noqa: E402
import sessions  # noqa: E402
import timeline  # noqa: E402

TABLES = ('dws_day_hour', 'dws_station_features', 'ads_station_busyness',
          'ads_forecast', 'ads_session_quantile')

SCHEMA = {
    'dws_day_hour': ('day_index integer, hour integer, weekday text, is_weekend integer, '
                     'session_count integer, total_kwh real, mask integer'),
    'dws_station_features': ('station_id text, session_count integer, active_days integer, '
                             'active_hours integer, sessions_per_active_day real, '
                             'kwh_per_session real, duration_mean real, peak_share real, '
                             'weekend_share real, active_hours_per_day real, dc_share real, '
                             'cluster_name text, busyness_tier text'),
    'ads_station_busyness': ('station_id text, sessions_per_active_day real, tier text, '
                             'tier_index integer, is_sparse integer'),
    'ads_forecast': ('day_index integer, weekday text, hour integer, base text, horizon integer, '
                     'method text, predicted real, model_version text, generated_at text'),
    'ads_session_quantile': ('target text, facility_label text, time_period text, hour integer, '
                             'tau real, predicted real, chosen text, coverage real, '
                             'calibration_gap real, model_version text'),
}


def publish(database, model, dictionary=None):
    connection = sqlite3.connect(database)
    counts = {}
    try:
        for table in TABLES:
            connection.execute(f'drop table if exists {table}')
            connection.execute(f'create table {table} ({SCHEMA[table]})')

        panel = timeline.build(database)
        connection.executemany(
            'insert into dws_day_hour values (?,?,?,?,?,?,?)',
            [(row['day_index'], row['hour'], row['weekday'], row['is_weekend'],
              int(row['sessions']), round(float(row['kwh']), 4), int(row['mask']))
             for row in panel['panel']])
        counts['dws_day_hour'] = len(panel['panel'])

        stations = model.get('stations')
        if stations:
            table = stations['station_table']
            connection.executemany(
                'insert into dws_station_features values (?,?,?,?,?,?,?,?,?,?,?,?,?)',
                [(station, stats['sessions'], stats['active_days'], stats['active_hours'],
                  round(stats['sessions_per_active_day'], 4), round(stats['kwh_per_session'], 4),
                  round(stats['duration_mean'], 4), round(stats['peak_share'], 4),
                  round(stats['weekend_share'], 4), round(stats['active_hours_per_day'], 4),
                  round(stats['dc_share'], 4), stats['cluster'], stats['busyness'])
                 for station, stats in sorted(table.items())])
            connection.executemany(
                'insert into ads_station_busyness values (?,?,?,?,?)',
                [(station, entry['sessions_per_active_day'], entry['tier'], entry['index'],
                  int(entry['tier'] == '样本不足'))
                 for station, entry in sorted(stations['tiers']['tiers'].items())])
            counts['dws_station_features'] = len(table)
            counts['ads_station_busyness'] = len(stations['tiers']['tiers'])

        version, generated_at = model.get('version'), model.get('generated_at')
        rows = []
        for item in model.get('forecast', []):
            for point in item['hours']:
                rows.append((item['day_index'], item['weekday'], point['hour'], item['base'],
                             item['horizon'], item['method'], point['predicted'],
                             version, generated_at))
        connection.executemany('insert into ads_forecast values (?,?,?,?,?,?,?,?,?)', rows)
        counts['ads_forecast'] = len(rows)

        section = model.get('sessions')
        quantile_rows = []
        if section:
            categories = section['categories']
            for target in section['deployment']:
                for facility in categories['facility_label']:
                    for period in categories['time_period']:
                        for hour in range(timeline.HOURS):
                            values = serving.session_quantile(section, target, facility,
                                                              period, hour)
                            for tau in sessions.QUANTILES:
                                key = f'{tau:.1f}'
                                task = next(task for task in section['tasks']
                                            if task['target'] == target and task['tau'] == tau)
                                quantile_rows.append((target, facility, period, hour, tau,
                                                      values[key], task['chosen'],
                                                      task['coverage'],
                                                      task['calibration_gap'], version))
        connection.executemany(
            'insert into ads_session_quantile values (?,?,?,?,?,?,?,?,?,?)', quantile_rows)
        counts['ads_session_quantile'] = len(quantile_rows)
        connection.commit()
    finally:
        connection.close()

    if dictionary:
        path = Path(dictionary)
        lines = ['', '## 预测层落库表（由 predict/publish.py 写入）', '',
                 f'模型版本 `{version}`，生成时间 {generated_at}。', '',
                 '| 层 | 表 | 行数 | 说明 |', '|---|---|---:|---|',
                 f"| DWS | `dws_day_hour` | {counts['dws_day_hour']} | "
                 '相对时间轴（day_index）× 小时面板：会话数、电量与缺测掩码 |',
                 f"| DWS | `dws_station_features` | {counts.get('dws_station_features', 0)} | "
                 '站点画像特征、所属簇与繁忙度档位 |',
                 f"| ADS | `ads_station_busyness` | {counts.get('ads_station_busyness', 0)} | "
                 '站点繁忙度分档（稀疏站点标记为样本不足） |',
                 f"| ADS | `ads_forecast` | {counts['ads_forecast']} | "
                 '未来一天 24 小时的负荷预测（会话数/电量 × 未来 1/6/24 小时 × 上线方法） |',
                 f"| ADS | `ads_session_quantile` | {counts['ads_session_quantile']} | "
                 '单次充电时长/电量的 P10/P50/P90（按设施类型 × 峰谷时段 × 小时） |']
        with path.open('a', encoding='utf-8') as stream:
            stream.write('\n'.join(lines) + '\n')
    return dict(database=str(database), model_version=version, tables=counts)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', required=True, help='数仓 SQLite 路径')
    parser.add_argument('--model', required=True, help='model_v2.json 路径')
    parser.add_argument('--dictionary', help='数据字典 markdown（追加预测层表清单）')
    args = parser.parse_args(argv)

    model = json.loads(Path(args.model).read_text(encoding='utf-8'))
    summary = publish(args.db, model, args.dictionary)
    print(json.dumps(summary, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
