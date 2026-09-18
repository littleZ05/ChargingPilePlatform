#!/usr/bin/env python3
"""相对时间轴：把 created_raw 的月日还原成可用的时间序列。

源数据的年份字段不可信（0014/0015 不是真实年份），既有实现因此丢掉了时间结构，
把整段数据压成 星期×小时 的剖面。但 created_raw 的月日与 weekday 列逐行自洽
（本模块会校验），说明日期骨架本身是真实的：可以构造相对时间轴 day_index，
用它做滞后特征与滚动评估，而不声称任何真实日历年份。

对外一律使用 day_index 与相对描述（第 0 天、前一天、上周同日），不输出真实日期。
"""
import datetime
import sqlite3
from collections import defaultdict

DAY_NAMES = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']
HOURS = 24


class TimelineError(ValueError):
    """时间轴无法构造或不自洽。"""


def parse_created(raw):
    """解析 created_raw 的 'YYYY-MM-DD HH:MM:SS'，返回 date；无法解析返回 None。"""
    if not raw or len(raw) < 10:
        return None
    try:
        return datetime.date(int(raw[0:4]), int(raw[5:7]), int(raw[8:10]))
    except (ValueError, TypeError):
        return None


def weekday_name(day):
    """按 ISO 星期取英文缩写，避开 strftime 的本地化差异。"""
    return DAY_NAMES[day.weekday()]


def load_events(database, table='dwd_sessions'):
    """读取明细事件（只取可用性校验通过的记录）。返回 (events, unparsed)。"""
    connection = sqlite3.connect(database)
    try:
        rows = connection.execute(
            f'select created_raw, weekday, start_hour, kwh, duration_hours, station_id, '
            f'facility_label, time_period from {table} '
            "where time_of_day_usable='1' and weekday_usable='1'").fetchall()
    finally:
        connection.close()

    events, unparsed = [], 0
    for raw, weekday, hour, kwh, duration, station, facility, period in rows:
        day = parse_created(raw)
        if day is None or hour is None:
            unparsed += 1
            continue
        events.append(dict(date=day, weekday=weekday, hour=int(hour),
                           kwh=float(kwh or 0.0),
                           duration_hours=float(duration or 0.0),
                           station_id=str(station) if station is not None else '',
                           facility_label=facility or '', time_period=period or ''))
    return events, unparsed


def build_panel(database, table='dwd_sessions'):
    """构造 日×小时 面板。

    返回 dict：
      origin/source_date   首日与它的源字符串（仅用于追溯，不当作真实年份）
      span_days            首末日期之间的日历天数
      observed_days        真有采集的日期数
      missing_days         日历上存在但完全没有采集的日期数（掩码为 0）
      consistency          月日推出的星期与 weekday 列的一致性统计
      days                 每天一行：day_index/source_date/weekday/observed
      panel                span_days × 24 行：sessions/kwh 与掩码
      events               明细事件（供后续站点级与聚类使用）
    """
    events, unparsed = load_events(database, table)
    if not events:
        raise TimelineError('没有可用事件：请检查 dwd_sessions 是否为空或可用性标记全为 0')

    origin = min(event['date'] for event in events)
    last = max(event['date'] for event in events)
    span_days = (last - origin).days + 1

    checked = matched = 0
    mismatched = []
    observed = set()
    cells = defaultdict(lambda: [0, 0.0])
    for event in events:
        day_index = (event['date'] - origin).days
        expected = weekday_name(event['date'])
        checked += 1
        if expected == event['weekday']:
            matched += 1
        else:
            mismatched.append(dict(source_date=event['date'].isoformat(),
                                   declared=event['weekday'], derived=expected))
        observed.add(day_index)
        cell = cells[(day_index, event['hour'])]
        cell[0] += 1
        cell[1] += event['kwh']

    days = []
    for day_index in range(span_days):
        day = origin + datetime.timedelta(days=day_index)
        days.append(dict(day_index=day_index, source_date=day.isoformat(),
                         weekday=weekday_name(day), is_weekend=int(day.weekday() >= 5),
                         observed=int(day_index in observed)))

    panel = []
    for day in days:
        for hour in range(HOURS):
            cell = cells.get((day['day_index'], hour), [0, 0.0])
            panel.append(dict(day_index=day['day_index'], hour=hour,
                              weekday=day['weekday'], is_weekend=day['is_weekend'],
                              mask=day['observed'],
                              sessions=cell[0], kwh=round(cell[1], 4)))

    return dict(origin=origin.isoformat(), source_date_span=[origin.isoformat(), last.isoformat()],
                span_days=span_days, observed_days=len(observed),
                missing_days=span_days - len(observed),
                events_total=len(events), events_unparsed=unparsed,
                consistency=dict(checked=checked, matched=matched,
                                 mismatched=len(mismatched), samples=mismatched[:5]),
                days=days, panel=panel, events=events)


def verify_consistency(panel, max_mismatch_ratio=0.0):
    """月日推出的星期必须与 weekday 列一致；否则时间轴不可信，直接报错。"""
    stats = panel['consistency']
    if not stats['checked']:
        raise TimelineError('没有任何事件参与一致性校验')
    ratio = stats['mismatched'] / stats['checked']
    if ratio > max_mismatch_ratio:
        raise TimelineError(
            f"月日与 weekday 不自洽：{stats['mismatched']}/{stats['checked']} 行不一致"
            f"（示例 {stats['samples']}），时间轴不可信")
    return True


def summary(panel):
    """一行摘要，供日志与报告引用。"""
    masked = sum(1 for row in panel['panel'] if row['mask'])
    return dict(span_days=panel['span_days'], observed_days=panel['observed_days'],
                missing_days=panel['missing_days'], events=panel['events_total'],
                unparsed=panel['events_unparsed'],
                usable_cells=masked, cells=len(panel['panel']),
                consistency=panel['consistency'])


def build(database, table='dwd_sessions'):
    """构造 + 校验，供各层直接调用。"""
    panel = build_panel(database, table)
    verify_consistency(panel)
    return panel
