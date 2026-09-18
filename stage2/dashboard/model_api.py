#!/usr/bin/env python3
"""第二版模型（model_v2.json）的接口逻辑：纯函数，便于单测与复用。

与 analytics.py 的分工：analytics 读清洗产物回答"发生过什么"，
model_api 读模型产物回答"接下来会怎样"，两者都不写任何数据。
参数一律严格校验：非法枚举宁可返回 400，也不静默回退到默认值——
大屏上出现一个"看起来有数但其实是别的东西"的图，比报错更糟。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'predict'))
from serving import session_quantile, station_payload  # noqa: E402

BASES = ('sessions', 'kwh')
HORIZONS = (1, 6, 24)
HOURS = 24


def _pop_int(filters, key, default=None, low=None, high=None):
    if key not in filters:
        return default
    try:
        value = int(filters.pop(key))
    except (TypeError, ValueError):
        raise ValueError(f'{key} 必须是整数')
    if (low is not None and value < low) or (high is not None and value > high):
        raise ValueError(f'{key} 取值无效：应在 {low}–{high} 之间')
    return value


def forecast(model, filters):
    """负荷预测：base × horizon，返回训练时算好的未来一天。"""
    filters = dict(filters)
    base = filters.pop('base', 'sessions')
    horizon = _pop_int(filters, 'horizon', 24)
    if filters:
        raise ValueError('预测参数无效：仅支持 base（sessions/kwh）与 horizon（1/6/24）')
    if base not in BASES or horizon not in HORIZONS:
        raise ValueError('预测参数无效：base 取 sessions/kwh，horizon 取 1/6/24')
    series = [item for item in model.get('forecast', [])
              if item['base'] == base and item['horizon'] == horizon]
    if not series:
        raise ValueError('模型产物里没有该组合的预测，请重跑 train_v2.py 生成 model_v2.json')
    chosen = series[0]
    tasks = [task for task in model.get('tasks', [])
             if task['base'] == base and task['horizon'] == horizon]
    task = tasks[0] if tasks else {}
    return dict(
        version=model.get('version'), generated_at=model.get('generated_at'),
        base=base, horizon=horizon, method=chosen['method'], column=chosen['column'],
        day=dict(day_index=chosen['day_index'], weekday=chosen['weekday'],
                 is_weekend=chosen['is_weekend']),
        hours=chosen['hours'],
        metrics=task.get('report', {}).get('chosen'),
        baseline=task.get('baseline_choice'),
        baseline_metrics=task.get('report', {}).get('best_baseline'),
        note=('day_index 是相对天数：源年份字段不可信，对外只用相对描述，'
              '不声称真实日历日期。'))


def session_quantiles(model, filters):
    """单次充电分位数：给定设施类型、峰谷时段与开始小时，给出 P10/P50/P90。"""
    filters = dict(filters)
    section = model.get('sessions')
    if not section:
        raise ValueError('模型产物里没有单次分位数部分，请重跑 train_v2.py')
    target = filters.pop('target', 'duration_hours')
    if target not in section['deployment']:
        raise ValueError(f'target 只支持 {"/".join(section["deployment"])}')
    facility = filters.pop('facility', None)
    period = filters.pop('period', None)
    platform = filters.pop('platform', None)
    station = filters.pop('station', None)
    weekend = _pop_int(filters, 'weekend', 0, 0, 1)
    hour = _pop_int(filters, 'hour', None, 0, HOURS - 1)
    if filters:
        raise ValueError('单次分位数参数无效：仅支持 '
                         'target/facility/period/hour/platform/weekend/station')
    categories = section['categories']
    facility = facility or categories['facility_label'][0]
    period = period or categories['time_period'][0]
    if facility not in categories['facility_label']:
        raise ValueError(f'facility 取值无效：{facility}')
    if period not in categories['time_period']:
        raise ValueError(f'period 取值无效：{period}')
    if platform is not None and platform not in categories['platform']:
        raise ValueError(f'platform 取值无效：{platform}')
    if hour is None:
        raise ValueError('缺少 hour（0–23）')
    quantiles = session_quantile(section, target, facility, period, hour,
                                 platform, weekend, station=station)
    tasks = [task for task in section['tasks'] if task['target'] == target]
    return dict(version=model.get('version'), target=target, facility=facility,
                period=period, hour=hour, platform=platform, weekend=weekend,
                station=station, quantiles=quantiles, levels=section['levels'],
                chosen={task['tau']: task['chosen'] for task in tasks},
                coverage={task['tau']: task['coverage'] for task in tasks},
                calibration_gap={task['tau']: task['calibration_gap'] for task in tasks},
                protocol=section['protocol'])


def stations(model, filters):
    """站点画像：聚类、繁忙度分档与 Top 站点。"""
    filters = dict(filters)
    section = model.get('stations')
    if not section:
        raise ValueError('模型产物里没有站点画像，请重跑 train_v2.py')
    limit = _pop_int(filters, 'limit', 20, 1, 105)
    if filters:
        raise ValueError('站点参数无效：仅支持 limit（1–105）')
    return station_payload(section, limit)


def model_options(model):
    """单次分位数与站点接口的合法取值，供前端下拉框取用，避免界面写死枚举。"""
    section = model.get('sessions')
    if not section:
        raise ValueError('模型产物里没有单次分位数部分，请重跑 train_v2.py')
    return dict(version=model.get('version'),
                facilities=section['categories']['facility_label'],
                periods=section['categories']['time_period'],
                platforms=section['categories']['platform'],
                targets=list(section['deployment']),
                levels=section['levels'], protocol=section['protocol'])
