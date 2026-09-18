#!/usr/bin/env python3
"""把训练产物变成可直接服务的预测。

大屏服务只读模型 JSON，不读明细表。因此：
  - 未来一天的负荷预测在训练结束时就算好，写进产物的 forecast 段，服务端只查表；
  - 单次分位数与站点画像是纯函数，给定设施类型/时段/小时即可算出，不需要数据库。
  - 会话分类只在训练期拟合一次树、算一张分段比例表，服务端只做"走树/查表"。
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
import tree  # noqa: E402

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


def classification_row(section, facility, period, hour, station=None, platform=None,
                       is_weekend=0, manager_vehicle=0.0):
    """把"扫码那一刻已知的信息"展开成设计矩阵的一行，列顺序与训练时一致。"""
    categories = section['categories']
    platform = platform or categories['platform'][0]
    row = dict(facility_label=facility, time_period=period, hour=int(hour),
               platform=platform, is_weekend=int(is_weekend),
               manager_vehicle=float(manager_vehicle), station_id=station or '')
    enriched = sessions.attach_station_features([row], section['statistics'])[0]
    _, values = sessions.design_row(enriched, categories, with_station=True)
    return enriched, values


def rounded(value, digits=4):
    return None if value is None else round(float(value), digits)


def classification_rule_rate(baseline, row):
    """上线规则那一支的概率：全局比例，或按设施类型（× 峰谷时段）查表。"""
    if baseline['method'] == 'global_rate':
        return baseline['base_rate']
    key = '|'.join(str(row[name]) for name in baseline['keys'])
    return baseline['table'].get(key, baseline['base_rate'])


def classification_tree_rate(deployment, values):
    """部署树给出的概率：沿树走到叶子，取叶子上的历史正类比例。"""
    model = {'tree': deployment['tree'], 'features': deployment['features']}
    return tree.predict_proba(model, values)


def classification_leaf(deployment, values):
    """这条会话落到哪片叶子：条件、样本量、其中长时长占比。"""
    node = deployment['tree']
    columns = {name: position for position, name in enumerate(deployment['features'])}
    conditions = []
    while not node['leaf']:
        value = values[columns[node['feature']]]
        if value <= node['threshold']:
            conditions.append(f"{node['feature']} <= {node['threshold']:g}")
            node = node['left']
        else:
            conditions.append(f"{node['feature']} > {node['threshold']:g}")
            node = node['right']
    compressed = tree.compress(conditions)
    return dict(conditions=compressed, conditions_text='、'.join(compressed),
                samples=node['samples'], positives=node['positives'],
                probability=round(node['value'], 4))


def classification_calibration_bucket(calibration, value):
    """这条概率落在哪个校准区间里，附上该区间的实际正类比例。"""
    for index, entry in enumerate(calibration):
        low, high = (float(part) for part in entry['range'].strip('()[]').split(','))
        if low < value <= high or (index == 0 and value <= low):
            return dict(entry, covers=True)
    return None


def classification_detail(section, facility, period, hour, station=None, platform=None,
                          is_weekend=0, manager_vehicle=0.0):
    """这一次会话的长时长占用概率：上线那一支 + 另一支对照 + 判定结果。"""
    deployment = section['deployment']
    enriched, values = classification_row(section, facility, period, hour, station,
                                          platform, is_weekend, manager_vehicle)
    rule_rate = classification_rule_rate(deployment['baseline'], enriched)
    tree_rate = classification_tree_rate(deployment, values)
    online = tree_rate if section['chosen'] == 'cart' else rule_rate
    threshold = deployment['decision_threshold']
    return dict(
        facility=facility, period=period, hour=int(hour), station=station or None,
        platform=enriched['platform'], is_weekend=int(is_weekend),
        method=section['chosen'], method_label=section['chosen_label'],
        probability=round(online, 4),
        decision=1 if online >= threshold else 0,
        decision_label='提前提示' if online >= threshold else '不需要额外动作',
        threshold=threshold,
        rule_method=deployment['baseline']['method'],
        rule_label=section['labels'][deployment['baseline']['method']],
        rule_rate=round(rule_rate, 4),
        tree_rate=round(tree_rate, 4),
        station_median=rounded(enriched['station_median']),
        station_count=int(enriched['station_count']),
        leaf=classification_leaf(deployment, values),
        calibration_bucket=classification_calibration_bucket(deployment['calibration'], online))


def classification_curve(section, facility, period, station=None, platform=None,
                         is_weekend=0, manager_vehicle=0.0):
    """同一个设施类型与峰谷时段下，0–23 点的概率曲线；上线方法一支、树一支。"""
    entries = []
    for hour in range(HOURS):
        detail = classification_detail(section, facility, period, hour, station,
                                       platform, is_weekend, manager_vehicle)
        entries.append(dict(hour=hour, probability=detail['probability'],
                            rule_rate=detail['rule_rate'], tree_rate=detail['tree_rate'],
                            decision=detail['decision']))
    return entries


def classification_segments(section):
    """设施类型 × 峰谷时段的历史正类比例与样本量（部署规则用的那张查找表）。"""
    baseline = section['deployment']['baseline']
    keys = baseline['keys']
    segments = []
    for key, rate in sorted(baseline['table'].items()):
        parts = key.split('|')
        segments.append(dict(facility=parts[0],
                             period=parts[1] if len(keys) > 1 else None,
                             sessions=baseline['counts'].get(key, 0), rate=round(rate, 4)))
    return segments


def classification_payload(section, facility, period, hour, station=None, platform=None,
                           is_weekend=0, manager_vehicle=0.0):
    """分类接口的响应体：单次预警 + 方法对照 + 概率曲线 + 分段表 + 规则与限制。"""
    deployment = section['deployment']
    return dict(
        label=section['label'], positive=section['positive'], negative=section['negative'],
        threshold_hours=section['threshold_hours'], threshold_source=section['threshold_source'],
        chosen=section['chosen'], chosen_label=section['chosen_label'],
        baseline_choice=section['baseline_choice'], baseline_label=section['baseline_label'],
        model_adopted=section['model_adopted'], beats_on_decisions=section['beats_on_decisions'],
        ranking_not_worse=section['ranking_not_worse'],
        f1_advantage=section['f1_advantage'], skill=section['skill'],
        block_wins=section['block_wins'], block_total=section['block_total'],
        test_sessions=section['test_sessions'], positive_rate=section['fold_positive_rate'],
        decision_threshold=deployment['decision_threshold'],
        methods=[dict(method=method, label=section['labels'][method], metrics=section['metrics'][method])
                 for method in section['baselines'] + ['cart']],
        blocks=section['blocks'],
        deployment=dict(params=deployment['params'], leaves=deployment['leaves'],
                        features=deployment['features'], train_sessions=deployment['train_sessions'],
                        train_positive_rate=deployment['train_positive_rate'],
                        in_sample_metrics=deployment['in_sample_metrics']),
        rules=deployment['rules'], calibration=deployment['calibration'],
        importance=deployment['importance'], limits=section['limits'],
        lookup=classification_detail(section, facility, period, hour, station, platform,
                                     is_weekend, manager_vehicle),
        curve=classification_curve(section, facility, period, station, platform,
                                   is_weekend, manager_vehicle),
        segments=classification_segments(section),
        note=(f"标签：训练窗口内单次时长 ≥ {section['threshold_hours']} 小时的会话记为"
              f"「{section['positive']}」（{section['threshold_source']}）。"
              f"评估在 {len(section['blocks'])} 个测试块上滚动重训，"
              f"概率阈值只在各折训练窗口内选，测试块不参与。"))
