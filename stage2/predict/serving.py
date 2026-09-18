#!/usr/bin/env python3
"""把训练产物变成可直接服务的预测。

大屏服务只读模型 JSON，不读明细表。因此：
  - 未来一天的负荷预测在训练结束时就算好，写进产物的 forecast 段，服务端只查表；
  - 单次分位数与站点画像是纯函数，给定设施类型/时段/小时即可算出，不需要数据库。
这样"服务启动"与"训练"解耦：换模型只需要替换一个 JSON，不重启数据链路。

两个约定值得写清楚：
1. 未来日只用于取特征。它在面板里被追加为一天，目标列填 0 占位——
   预测只用滞后特征，占位值不会被读进任何模型。
2. 当上线的是基线时，产物里的模型仍然是一份用该目标拟合的岭回归，
   其 intercept 恰好等于训练期目标均值，因此 global_mean 基线可以直接由它还原。
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import baselines  # noqa: E402
import features  # noqa: E402
import models  # noqa: E402
import sessions  # noqa: E402
import timeline  # noqa: E402

HOURS = timeline.HOURS


def build_future_rows(panel):
    """在面板末尾追加一天（未来日）并重算滞后特征，返回该日的行与它的天数下标。"""
    last_day = max(row['day_index'] for row in panel['panel'])
    future_day = last_day + 1
    template = {row['hour']: row for row in panel['panel'] if row['day_index'] == last_day}
    weekday = timeline.DAY_NAMES[(timeline.DAY_NAMES.index(template[0]['weekday']) + 1) % 7]
    extended = dict(panel, panel=list(panel['panel']))
    for hour in range(HOURS):
        extended['panel'].append(dict(
            day_index=future_day, hour=hour, weekday=weekday,
            is_weekend=int(weekday in ('Sat', 'Sun')), sessions=0.0, kwh=0.0,
            mask=1))          # mask=1：这一天存在，只是还没有观测；预测行需要它可用
    return features.attach_lags(extended), future_day


def predict_rows(task, model, rows, spec):
    """按任务选定的方法给出逐行预测。"""
    if task['chosen'] in models.METHODS:
        values, _ = models.predict(model, rows, spec)
    else:
        fitted = {'mean': model['intercept']} if task['chosen'] == 'global_mean' else {}
        values, _ = baselines.predict(rows, task['chosen'], fitted, spec)
    return [None if value is None else round(float(value), 4) for value in values]


def next_day_forecast(panel, tasks):
    """算好未来一天 24 小时的负荷预测；返回可直接写进产物的结构。"""
    rows, future_day = build_future_rows(panel)
    future = [row for row in rows if row['day_index'] == future_day]
    forecast = []
    for task in tasks:
        column = features.add_horizon(rows, task['horizon'], task['base'])
        spec = features.task_spec(column)
        for row in future:
            row[column] = 0.0            # 占位目标：预测只用特征
        values = predict_rows(task, task['model'], future, spec)
        forecast.append(dict(
            base=task['base'], horizon=task['horizon'], column=column,
            method=task['chosen'], day_index=future_day,
            weekday=future[0]['weekday'], is_weekend=future[0]['is_weekend'],
            hours=[dict(hour=row['hour'], predicted=value)
                   for row, value in zip(future, values)]))
    return forecast


def session_quantile(section, target, facility, period, hour, platform=None,
                     is_weekend=0, manager_vehicle=0.0, station=None):
    """给定会话发生时的信息，返回该目标三个分位数的预测值。"""
    categories = section['categories']
    statistics = section['deployment'][target]['statistics']
    platform = platform or categories['platform'][0]
    row = dict(facility_label=facility, time_period=period, hour=int(hour),
               platform=platform, is_weekend=int(is_weekend),
               manager_vehicle=float(manager_vehicle), station_id=station or '')
    enriched = sessions.attach_station_features([row], statistics)[0]
    values = {}
    for tau in sessions.QUANTILES:
        entry = section['deployment'][target]['quantiles'][f'{tau:.1f}']
        if entry['chosen'] == 'quantile_ridge':
            model = dict(entry['model'], categories=categories,
                         statistics=statistics)
            values[f'{tau:.1f}'] = round(sessions.predict_quantile(model, [enriched])[0], 4)
        else:
            values[f'{tau:.1f}'] = _baseline_quantile(entry['baseline'], enriched)
    return values


def _baseline_quantile(baseline, row):
    if baseline['method'] == 'global_quantile':
        return baseline['global_value']
    key = '|'.join(str(row[name]) for name in baseline['key'])
    return baseline['table'].get(key, baseline['global_value'])


def station_payload(section, limit=20):
    """站点画像接口的响应体：聚类、分档、Top 站点。"""
    tiers = section['tiers']
    counts = {}
    for entry in tiers['tiers'].values():
        counts[entry['tier']] = counts.get(entry['tier'], 0) + 1
    return dict(
        stations=section['stations'], clustered=section['clustered_stations'],
        sparse=len(section['sparse_stations']),
        cluster_count=section['cluster_count'], silhouette=section['silhouette'],
        clusters=[dict(cluster=item['cluster'], name=item['name'],
                       stations=len(item['stations']), sessions=item['sessions'],
                       profile=item['profile']) for item in section['clusters']],
        views=[dict(k=item['k'], silhouette=item['silhouette'],
                    clusters=[dict(cluster=cluster['cluster'], name=cluster['name'],
                                   stations=len(cluster['stations']),
                                   sessions=cluster['sessions'], profile=cluster['profile'])
                              for cluster in item['clusters']])
               for item in section.get('views', [])],
        tiers=dict(cuts=tiers['cuts'], names=tiers['names'], counts=counts),
        top_stations=section['top_stations'][:limit],
        stability=section['stability'],
        protocol=section['protocol'], note=section['note'])
