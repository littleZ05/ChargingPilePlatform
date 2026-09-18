"""预测层测试用的合成数据构造器。"""
import datetime
import sqlite3
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import timeline  # noqa: E402

SCHEMA = ('create table dwd_sessions (created_raw text, weekday text, start_hour integer, '
          'kwh text, duration_hours text, station_id text, facility_label text, '
          'time_period text, platform text, manager_vehicle text, '
          'time_of_day_usable text, weekday_usable text)')

DEFAULT_START = datetime.date(14, 11, 18)     # 源数据首日同款写法：年份字段不可信


def create_database(path, hourly_counts, start=DEFAULT_START, kwh_each=4.0,
                    corrupt_weekday_rows=0, unusable_rows=0):
    """hourly_counts[day_offset][hour] = 该小时会话数；整天为 None 表示该日缺测。"""
    connection = sqlite3.connect(path)
    connection.execute(SCHEMA)
    rows = []
    for offset, counts in enumerate(hourly_counts):
        if counts is None:
            continue
        day = start + datetime.timedelta(days=offset)
        for hour, count in enumerate(counts):
            for _ in range(count):
                rows.append([f'{day.isoformat()} {hour:02d}:15:00', timeline.weekday_name(day),
                             hour, str(kwh_each), '2.0', f'S{hour % 4 + 1}', '直流', 'normal',
                             'ios', '0', '1', '1'])
    for row in rows[-corrupt_weekday_rows:] if corrupt_weekday_rows else []:
        row[1] = 'Mon' if row[1] != 'Mon' else 'Tue'
    for row in rows[-unusable_rows:] if unusable_rows else []:
        row[10] = '0'                      # time_of_day_usable 在新增两列之后
    connection.executemany('insert into dwd_sessions values (?,?,?,?,?,?,?,?,?,?,?,?)', rows)
    connection.commit()
    connection.close()
    return path


def flat_days(days, sessions_per_hour=1):
    """连续 days 天、每小时同样会话数的简单场景。"""
    return [[sessions_per_hour] * 24 for _ in range(days)]
