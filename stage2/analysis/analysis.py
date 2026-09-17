#!/usr/bin/env python3
"""第二阶段数据分析：描述统计、相关性、聚类、回归、季节性与异常检测。

全部使用 Python 标准库实现（本机无 numpy/sklearn/Spark），算法口径与 Spark MLlib 一致：
  - 描述统计与分位数
  - Pearson 相关系数
  - K-Means 聚类（k-means++ 初始化，固定随机种子保证可复现）
  - 最小二乘一元线性回归（斜率、截距、R²）
  - 小时/星期季节性指数
  - IQR 与 3σ 异常检测
输出 JSON 结果与中文分析报告；不发布真实日期趋势（源年份未证实）。
"""
import argparse
import json
import math
import random
import sqlite3
import sys
from collections import defaultdict
from decimal import Decimal
from pathlib import Path

FACILITY_LABELS = {'1': '直流', '2': '交流', '3': '直交流一体'}


def facility_name(code):
    code = str(code).strip()
    return FACILITY_LABELS.get(code, '未知编码' + code)


def quantile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    low, high = math.floor(position), math.ceil(position)
    if low == high:
        return ordered[int(position)]
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def describe(values):
    values = [float(v) for v in values]
    if not values:
        return dict(count=0)
    mean = sum(values) / len(values)
    variance = sum((v - mean) ** 2 for v in values) / (len(values) - 1) if len(values) > 1 else 0.0
    return dict(count=len(values), sum=round(sum(values), 4), mean=round(mean, 4),
                median=round(quantile(values, .5), 4), p25=round(quantile(values, .25), 4),
                p75=round(quantile(values, .75), 4), stdev=round(math.sqrt(variance), 4),
                min=round(min(values), 4), max=round(max(values), 4))


def pearson(pairs):
    points = [(float(x), float(y)) for x, y in pairs]
    if len(points) < 2:
        return None
    n = len(points)
    mean_x = sum(p[0] for p in points) / n
    mean_y = sum(p[1] for p in points) / n
    cov = sum((x - mean_x) * (y - mean_y) for x, y in points)
    var_x = sum((x - mean_x) ** 2 for x, _ in points)
    var_y = sum((y - mean_y) ** 2 for _, y in points)
    if var_x == 0 or var_y == 0:
        return None
    return round(cov / math.sqrt(var_x * var_y), 6)


def linear_regression(pairs):
    points = [(float(x), float(y)) for x, y in pairs]
    if len(points) < 2:
        return None
    n = len(points)
    mean_x = sum(p[0] for p in points) / n
    mean_y = sum(p[1] for p in points) / n
    denominator = sum((x - mean_x) ** 2 for x, _ in points)
    if denominator == 0:
        return None
    slope = sum((x - mean_x) * (y - mean_y) for x, y in points) / denominator
    intercept = mean_y - slope * mean_x
    total = sum((y - mean_y) ** 2 for _, y in points)
    residual = sum((y - (slope * x + intercept)) ** 2 for x, y in points)
    return dict(slope=round(slope, 6), intercept=round(intercept, 6),
                r_squared=round(1 - residual / total, 6) if total else None, samples=n)


def kmeans(points, k=3, iterations=100, seed=42):
    """k-means++ 初始化 + Lloyd 迭代；固定种子使结果可复现。"""
    if len(points) <= k:
        return list(range(len(points))), [list(map(float, p)) for p in points]
    rng = random.Random(seed)
    centers = [list(map(float, points[rng.randrange(len(points))]))]
    while len(centers) < k:
        distances = []
        for point in points:
            nearest = min(sum((float(a) - b) ** 2 for a, b in zip(point, center)) for center in centers)
            distances.append(nearest)
        threshold = rng.random() * sum(distances)
        cumulative = 0.0
        for index, distance in enumerate(distances):
            cumulative += distance
            if cumulative >= threshold:
                centers.append(list(map(float, points[index])))
                break
        else:
            centers.append(list(map(float, points[-1])))
    labels = [0] * len(points)
    for _ in range(iterations):
        changed = False
        for index, point in enumerate(points):
            distances = [sum((float(a) - b) ** 2 for a, b in zip(point, center)) for center in centers]
            label = distances.index(min(distances))
            if label != labels[index]:
                labels[index] = label
                changed = True
        for label in range(k):
            members = [points[i] for i in range(len(points)) if labels[i] == label]
            if members:
                centers[label] = [sum(float(m[d]) for m in members) / len(members)
                                  for d in range(len(members[0]))]
        if not changed:
            break
    return labels, centers


def seasonality(rows, key):
    totals = defaultdict(int)
    for row in rows:
        totals[str(row[key])] += 1
    overall = sum(totals.values()) / len(totals) if totals else 0
    return {k: dict(sessions=v, index=round(v / overall, 4) if overall else None)
            for k, v in sorted(totals.items(), key=lambda item: str(item[0]))}


def outliers(values):
    numbers = sorted(float(v) for v in values)
    result = dict(count=len(numbers))
    if len(numbers) >= 4:
        q1, q3 = quantile(numbers, .25), quantile(numbers, .75)
        spread = q3 - q1
        low, high = q1 - 1.5 * spread, q3 + 1.5 * spread
        result.update(iqr_low=round(low, 4), iqr_high=round(high, 4),
                      iqr_outliers=sum(1 for v in numbers if v < low or v > high))
    if len(numbers) >= 2:
        mean = sum(numbers) / len(numbers)
        stdev = math.sqrt(sum((v - mean) ** 2 for v in numbers) / (len(numbers) - 1))
        result.update(sigma3_outliers=sum(1 for v in numbers if abs(v - mean) > 3 * stdev) if stdev else 0,
                      mean=round(mean, 4), stdev=round(stdev, 4))
    return result


def load_sessions(connection):
    connection.row_factory = sqlite3.Row
    return [dict(row) for row in connection.execute(
        'select station_id, facility_type, facility_label, platform, weekday, start_hour, kwh, duration_hours,'
        ' weekend, time_period, estimated_fee, quality_flags, time_of_day_usable, weekday_usable from dwd_sessions')]


def usable(value):
    """可用性标记容错：CSV 里的 '1' 经 SQLite 类型推断可能变成整数 1。"""
    return str(value).strip() in ('1', 'True', 'true')


def station_features(rows):
    grouped = defaultdict(list)
    for row in rows:
        grouped[row['station_id']].append(row)
    features, keys = [], []
    for station_id, members in sorted(grouped.items()):
        energy = [float(m['kwh'] or 0) for m in members]
        durations = [float(m['duration_hours'] or 0) for m in members]
        features.append([len(members), sum(energy), sum(durations) / len(members),
                         sum(1 for e in energy if e == 0) / len(members)])
        keys.append(station_id)
    return keys, features


def zscore_scale(features):
    columns = list(zip(*features))
    scaled = []
    for column in columns:
        mean = sum(column) / len(column)
        stdev = math.sqrt(sum((v - mean) ** 2 for v in column) / len(column)) or 1.0
        scaled.append([(v - mean) / stdev for v in column])
    return [list(row) for row in zip(*scaled)]


def day_type_comparison(rows):
    """工作日 vs 周末对比（老师硬性要求的对比性分析）。

    工作日 5 天、周末 2 天，因此除总量外给出日均口径，避免直接用总量误导。
    """
    groups = {'weekday': [], 'weekend': []}
    for row in rows:
        key = 'weekend' if usable(row.get('weekend')) else 'weekday'
        groups[key].append(row)
    out = {}
    for key, members in groups.items():
        kwh = [float(m['kwh'] or 0) for m in members]
        duration = [float(m['duration_hours'] or 0) for m in members]
        days = 5 if key == 'weekday' else 2
        out[key] = dict(sessions=len(members), days=days,
                        total_kwh=round(sum(kwh), 2),
                        per_day_sessions=round(len(members) / days, 2),
                        per_day_kwh=round(sum(kwh) / days, 2),
                        avg_kwh=round(sum(kwh) / len(members), 4) if members else None,
                        avg_duration_hours=round(sum(duration) / len(members), 4) if members else None)
    weekday = out.get('weekday', {}); weekend = out.get('weekend', {})
    weekday_avg = weekday.get('avg_kwh'); weekend_avg = weekend.get('avg_kwh')
    out['contrast'] = dict(
        weekend_per_day_sessions_vs_weekday=round(
            weekend.get('per_day_sessions', 0) / weekday.get('per_day_sessions', 1), 4)
        if weekday.get('per_day_sessions') else None,
        weekend_avg_kwh_vs_weekday=round(weekend_avg / weekday_avg, 4)
        if (weekday_avg is not None and weekend_avg is not None and weekday_avg) else None)
    return out


def distribution(rows, key, with_fee=False):
    """按某维度统计会话数、总电量、平均电量；with_fee 时附带估算电费合计。"""
    grouped = defaultdict(list)
    for row in rows:
        value = str(row.get(key) or '').strip()
        if not value:
            continue
        grouped[value].append(row)
    out = {}
    for value, members in sorted(grouped.items()):
        kwh = [float(m['kwh'] or 0) for m in members]
        item = dict(sessions=len(members), total_kwh=round(sum(kwh), 2),
                    avg_kwh=round(sum(kwh) / len(members), 4) if members else None)
        if with_fee:
            fee = [float(m['estimated_fee'] or 0) for m in members if str(m.get('estimated_fee') or '').strip()]
            item['estimated_fee_total'] = round(sum(fee), 2)
        out[value] = item
    return out


def analyse(database, clusters=3):
    connection = sqlite3.connect(database)
    try:
        rows = load_sessions(connection)
        ingestion = {}
        for table in ('ingest_business_orders', 'ingest_logs_pcserver', 'ingest_stream_windowed_power'):
            try:
                ingestion[table] = connection.execute(f'select count(*) from "{table}"').fetchone()[0]
            except sqlite3.OperationalError:
                continue
    finally:
        connection.close()
    energy = [float(r['kwh'] or 0) for r in rows]
    durations = [float(r['duration_hours'] or 0) for r in rows]
    keys, features = station_features(rows)
    labels, centers = kmeans(zscore_scale(features), clusters)
    profiles = []
    for index, key in enumerate(keys):
        profiles.append(dict(station_id=key, cluster=labels[index],
                             sessions=features[index][0], total_kwh=round(features[index][1], 2),
                             mean_duration=round(features[index][2], 4),
                             zero_energy_ratio=round(features[index][3], 4)))
    cluster_summary = []
    for label in range(clusters):
        members = [p for p in profiles if p['cluster'] == label]
        if not members:
            continue
        cluster_summary.append(dict(cluster=label, stations=len(members),
                                    sessions=sum(m['sessions'] for m in members),
                                    total_kwh=round(sum(m['total_kwh'] for m in members), 2),
                                    mean_sessions=round(sum(m['sessions'] for m in members) / len(members), 2)))
    day_type = day_type_comparison(rows)
    dimensions = dict(facility_type=distribution(rows, 'facility_type'),
                      time_period=distribution(rows, 'time_period', with_fee=True),
                      platform=distribution(rows, 'platform'))
    return dict(
        source=dict(sessions=len(rows), stations=len(keys), ingestion=ingestion),
        descriptives=dict(kwh=describe(energy), duration_hours=describe(durations)),
        correlation=dict(kwh_vs_duration=pearson(zip(durations, energy))),
        regression=dict(duration_to_kwh=linear_regression(zip(durations, energy))),
        seasonality=dict(hour=seasonality([r for r in rows if usable(r['time_of_day_usable'])], 'start_hour'),
                         weekday=seasonality([r for r in rows if usable(r['weekday_usable'])], 'weekday')),
        comparison=dict(weekday_vs_weekend=day_type),
        dimensions=dimensions,
        clusters=dict(k=clusters, summary=cluster_summary, profiles=profiles),
        anomalies=dict(kwh=outliers(energy), duration_hours=outliers(durations)),
        caveats=['聚类用于描述站点画像，不代表经营优先级或因果关系。',
                 '相关性不等于因果，样本年份未证实，不做真实日期趋势。',
                 '原字段费用语义与币种未核实，不参与本层分析。',
                 '估算电费为峰谷模型口径（高峰1.5/平时1.0/低谷0.7元×kWh），非真实营收。'])


def build_report(result, path):
    hour = result['seasonality']['hour']
    top_hours = sorted(hour.items(), key=lambda item: -item[1]['sessions'])[:3]
    reg = result['regression']['duration_to_kwh']
    lines = ['# 第二阶段数据分析结果', '',
             f"样本：{result['source']['sessions']} 次会话、{result['source']['stations']} 个站点；"
             f"采集产物行数：{result['source']['ingestion']}。", '',
             '## 一、描述统计', '',
             '| 指标 | 样本 | 合计 | 均值 | 中位数 | P25 | P75 | 标准差 | 最小 | 最大 |',
             '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for name, key in (('电量 kWh', 'kwh'), ('时长 小时', 'duration_hours')):
        stats = result['descriptives'][key]
        lines.append(f"| {name} | {stats['count']} | {stats['sum']} | {stats['mean']} | {stats['median']} "
                     f"| {stats['p25']} | {stats['p75']} | {stats['stdev']} | {stats['min']} | {stats['max']} |")
    lines += ['', '## 二、关联与回归', '',
              f"- 时长与电量的 Pearson 相关系数：`{result['correlation']['kwh_vs_duration']}`；相关不等于因果。",
              f"- 最小二乘回归：`电量 ≈ {reg['slope']} × 时长 + {reg['intercept']}`，"
              f"R² = `{reg['r_squared']}`，样本 {reg['samples']}。", '',
              '## 三、季节性（来源小时/星期，非真实日期）', '']
    for key, label in (('hour', '开始小时'), ('weekday', '星期')):
        top = sorted(result['seasonality'][key].items(), key=lambda item: -item[1]['sessions'])[:3]
        detail = '，'.join(f"{k} 共 {v['sessions']} 次" for k, v in top)
        lines.append(f'- {label}前三：{detail}')
    lines += ['', '## 四、站点聚类', '',
              f"k = {result['clusters']['k']}，k-means++ 初始化，固定随机种子。", '',
              '| 簇 | 站点数 | 会话数 | 累计电量 kWh | 平均站点会话数 |', '|---:|---:|---:|---:|---:|']
    for cluster in result['clusters']['summary']:
        lines.append(f"| {cluster['cluster']} | {cluster['stations']} | {cluster['sessions']} "
                     f"| {cluster['total_kwh']} | {cluster['mean_sessions']} |")
    lines += ['', '## 五、异常检测', '']
    for name, key in (('电量', 'kwh'), ('时长', 'duration_hours')):
        stats = result['anomalies'][key]
        lines.append(f"- {name}：IQR 区间 [{stats.get('iqr_low')}, {stats.get('iqr_high')}]，"
                     f"IQR 异常 {stats.get('iqr_outliers')} 条，3σ 异常 {stats.get('sigma3_outliers')} 条。"
                     '异常保留并标记，不直接删除。')
    period_labels = {'peak': '高峰', 'normal': '平时', 'off_peak': '低谷'}
    wd = result['comparison']['weekday_vs_weekend']
    lines += ['', '## 六、工作日 vs 周末对比（对比性分析）', '']
    for key, label in (('weekday', '工作日'), ('weekend', '周末')):
        g = wd[key]
        lines.append(f"- {label}：{g['sessions']} 次会话（{g['days']} 天），总电量 {g['total_kwh']} kWh；"
                     f"日均 {g['per_day_sessions']} 次 / {g['per_day_kwh']} kWh，"
                     f"平均 {g['avg_kwh']} kWh/次、{g['avg_duration_hours']} 小时/次。")
    contrast = wd['contrast']
    lines.append(f"- 周末日均会话相对工作日：`{contrast['weekend_per_day_sessions_vs_weekday']}`；"
                 f"周末平均电量相对工作日：`{contrast['weekend_avg_kwh_vs_weekday']}`。")
    lines += ['', '## 七、维度分布（会话数 / 总电量 / 平均电量）', '']
    for key, label in (('facility_type', '设施类型'), ('time_period', '峰谷时段'), ('platform', '平台')):
        lines.append(f"### {label}")
        for value, item in result['dimensions'][key].items():
            shown = facility_name(value) if key == 'facility_type' else period_labels.get(value, value)
            fee = f"，估算电费 {item['estimated_fee_total']} 元" if 'estimated_fee_total' in item else ''
            lines.append(f"- {shown}：{item['sessions']} 次，{item['total_kwh']} kWh，平均 {item['avg_kwh']} kWh/次{fee}")
        lines.append('')
    lines += ['', '## 八、限制', ''] + [f'- {c}' for c in result['caveats']]
    Path(path).write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', required=True, help='数仓 SQLite 路径')
    parser.add_argument('--out', required=True, help='分析结果 JSON 输出路径')
    parser.add_argument('--report', help='分析报告 Markdown 输出路径')
    parser.add_argument('--clusters', type=int, default=3, help='K-Means 簇数')
    args = parser.parse_args(argv)
    result = analyse(args.db, args.clusters)
    Path(args.out).write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if args.report:
        build_report(result, args.report)
    print(json.dumps({'sessions': result['source']['sessions'], 'stations': result['source']['stations'],
                      'correlation': result['correlation'], 'r_squared':
                      result['regression']['duration_to_kwh']['r_squared']}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
