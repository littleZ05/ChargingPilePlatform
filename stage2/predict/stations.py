#!/usr/bin/env python3
"""站点画像聚类与繁忙度分档。

预测给出"某天某个时段多少负荷"，但运营还要回答"这个站是什么类型的站、
它在站群里算不算忙"。两个问题都不能靠单一指标回答：只按会话量排名，
会把"少而慢"的长时交流站和"多而快"的直流站混在一起。P3 用站点画像聚类
把站点按多维行为分组，再用繁忙度分档给出一个可解释、可直接排序的标签。

聚类特征（都按站点汇总，7 维）：
  sessions_per_active_day 活跃日日均会话数   —— 周转强度
  kwh_per_session         单次平均电量
  duration_mean           平均单次时长
  peak_share              高峰时段会话占比
  weekend_share           周末会话占比
  active_hours_per_day    活跃日的活跃小时数 —— 时间分散程度
  dc_share                直流设施会话占比

k 不拍脑袋：在 2..6 之间按轮廓系数挑。样本量小的站点不参与聚类——
3 次会话算出来的"画像"是噪声，这类站点一律标注为稀疏，不强行归类。

分档与聚类是两件事，刻意分开：
  聚类 = 多维行为分组，回答"这是什么类型的站"；
  分档 = 单一可解释指标（活跃日日均会话数）按分位数切 4 档，回答"它有多忙"。
把分档也做成聚类会让标签失去可比性：运营需要一个能直接排序的繁忙度。
"""
import argparse
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
if str(HERE.parent / 'analysis') not in sys.path:
    sys.path.insert(0, str(HERE.parent / 'analysis'))

import analysis  # noqa: E402  复用环节 4 已验证的 k-means++ 实现
import sessions  # noqa: E402

MIN_SESSIONS = 20           # 少于此数的站点不参与聚类，标注为稀疏
K_RANGE = (2, 3, 4, 5, 6)
TIER_NAMES = ['低繁忙', '较低繁忙', '较高繁忙', '高繁忙']
TOP_STATIONS = 20           # 报告只列会话量 Top 20，避免把稀疏站点当结论
FEATURES = ['sessions_per_active_day', 'kwh_per_session', 'duration_mean',
            'peak_share', 'weekend_share', 'active_hours_per_day', 'dc_share']
FEATURE_LABELS = {
    'sessions_per_active_day': '周转强度', 'kwh_per_session': '单次电量',
    'duration_mean': '单次时长', 'peak_share': '高峰集中度',
    'weekend_share': '周末活跃度', 'active_hours_per_day': '时段分散度',
    'dc_share': '直流占比'}
DIRECTION_LABELS = {
    'sessions_per_active_day': ('偏冷清', '偏繁忙'),
    'kwh_per_session': ('小电量', '大电量'),
    'duration_mean': ('短时长', '长时长'),
    'peak_share': ('平谷为主', '高峰集中'),
    'weekend_share': ('工作日为主', '周末活跃'),
    'active_hours_per_day': ('时段集中', '全时段'),
    'dc_share': ('交流为主', '直流为主')}


def station_features(rows):
    """把明细会话汇总成站点特征；返回 {站点: {特征: 值}}。"""
    grouped = defaultdict(list)
    for row in rows:
        grouped[row['station_id']].append(row)
    table = {}
    for station, members in grouped.items():
        days = {row['day_index'] for row in members}
        hours = {(row['day_index'], row['hour']) for row in members}
        energy = sum(float(row['kwh']) for row in members)
        table[station] = dict(
            sessions=len(members),
            active_days=len(days),
            active_hours=len(hours),
            sessions_per_active_day=len(members) / len(days),
            kwh_per_session=energy / len(members),
            duration_mean=sum(float(row['duration_hours']) for row in members) / len(members),
            peak_share=sum(1 for row in members if row['time_period'] == 'peak') / len(members),
            weekend_share=sum(1 for row in members if row['is_weekend']) / len(members),
            active_hours_per_day=len(hours) / len(days),
            dc_share=sum(1 for row in members if row['facility_label'] == '直流') / len(members))
    return table


def standardize(matrix):
    """按列 z 分数标准化；标准差为 0 的列退化为 1，避免除零。"""
    if not matrix:
        return [], []
    width = len(matrix[0])
    means = [sum(row[i] for row in matrix) / len(matrix) for i in range(width)]
    stds = []
    for i in range(width):
        variance = sum((row[i] - means[i]) ** 2 for row in matrix) / len(matrix)
        stds.append(math.sqrt(variance) or 1.0)
    scaled = [[(row[i] - means[i]) / stds[i] for i in range(width)] for row in matrix]
    return scaled, [dict(feature=FEATURES[i], mean=round(means[i], 4),
                         std=round(stds[i], 4)) for i in range(width)]


def distance(left, right):
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)))


def silhouette(points, labels):
    """轮廓系数：a 为同簇平均距离，b 为最近邻簇平均距离；越大说明簇分得越开。"""
    groups = defaultdict(list)
    for index, label in enumerate(labels):
        groups[label].append(index)
    if len(groups) < 2:
        return None
    scores = []
    for index, point in enumerate(points):
        own = groups[labels[index]]
        if len(own) < 2:
            continue
        a = sum(distance(point, points[other]) for other in own if other != index) / (len(own) - 1)
        b = min(sum(distance(point, points[other]) for other in members) / len(members)
                for label, members in groups.items() if label != labels[index])
        scores.append((b - a) / max(a, b) if max(a, b) else 0.0)
    return round(sum(scores) / len(scores), 4) if scores else None


def choose_clusters(points, k_range=K_RANGE, seed=42):
    """在 k 的候选区间里按轮廓系数挑；返回 (k, labels, centers, 各 k 得分)。"""
    if len(points) < min(k_range) + 1:
        raise ValueError('站点样本不足，无法聚类')
    best, scores = None, []
    for k in k_range:
        if len(points) <= k:
            continue
        labels, centers = analysis.kmeans(points, k=k, seed=seed)
        score = silhouette(points, labels)
        scores.append(dict(k=k, silhouette=score))
        if score is not None and (best is None or score > best[0]):
            best = (score, k, labels, centers)
    if best is None:
        raise ValueError('没有可用的聚类结果：站点数不足以形成多个簇')
    return best[1], best[2], best[3], scores


def name_clusters(centers):
    """用簇心偏离最大的两个特征给簇命名，例如"偏繁忙·长时长"。"""
    names = []
    for center in centers:
        ranked = sorted(range(len(center)), key=lambda i: -abs(center[i]))
        parts = []
        for index in ranked[:2]:
            low, high = DIRECTION_LABELS[FEATURES[index]]
            parts.append(high if center[index] > 0 else low)
        names.append('·'.join(parts))
    return names


def partition(matrix, sample, table, k, seed=42):
    """对标准化后的站点矩阵做一次 k 聚类，返回带画像命名的结果。"""
    labels, centers = analysis.kmeans(matrix, k=k, seed=seed)
    names = name_clusters(centers)
    clusters = []
    for cluster in range(k):
        members = [station for station, label in zip(sample, labels) if label == cluster]
        if not members:            # Lloyd 迭代可能留下空簇（k 超过实际分组数时）
            continue
        clusters.append(dict(
            cluster=cluster, name=names[cluster], stations=members,
            sessions=sum(table[station]['sessions'] for station in members),
            profile={feature: round(sum(table[station][feature] for station in members)
                                    / len(members), 4) for feature in FEATURES}))
    return dict(k=k, silhouette=silhouette(matrix, labels), labels=labels,
                names=names, clusters=clusters)


def busyness_tiers(stations, clustered=None):
    """按活跃日日均会话数的分位数把站点切成 4 档；样本不足的站点不分档。"""
    values = sorted(stats['sessions_per_active_day'] for stats in stations.values())
    cuts = [analysis.quantile(values, share) for share in (0.25, 0.5, 0.75)] \
        if values else [0.0, 0.0, 0.0]
    tiers = {}
    for station, stats in stations.items():
        value = stats['sessions_per_active_day']
        if clustered is not None and station not in clustered:
            tiers[station] = dict(tier='样本不足', index=None,
                                  sessions_per_active_day=round(value, 4))
            continue
        tiers[station] = dict(tier=TIER_NAMES[sum(1 for cut in cuts if value >= cut)],
                              index=sum(1 for cut in cuts if value >= cut),
                              sessions_per_active_day=round(value, 4))
    return dict(cuts=[round(cut, 4) for cut in cuts], names=list(TIER_NAMES), tiers=tiers)


def stability(rows, k, min_sessions=MIN_SESSIONS, share=0.7, seed=42):
    """分半稳定性：前 70% 天与后 30% 天各自聚类，比较同一批站点的归属是否一致。

    没有人工标签可对的时候，这是能拿到的最直接的稳健性证据：两段各自聚类
    得到的归属如果差异很大，说明画像本身不稳，就不该拿去下结论。
    """
    days = sorted({row['day_index'] for row in rows})
    if not days:
        return dict(stations=0, agreement=None, note='没有可用会话')
    cut = days[min(len(days) - 1, int(len(days) * share))]
    halves = {'early': [row for row in rows if row['day_index'] < cut],
              'late': [row for row in rows if row['day_index'] >= cut]}
    result = {}
    for name, subset in halves.items():
        table = station_features(subset)
        sample = sorted(station for station, stats in table.items()
                        if stats['sessions'] >= max(5, min_sessions // 2))
        if len(sample) <= k:
            return dict(stations=0, agreement=None, note=f'{name} 时段站点样本不足')
        matrix, _ = standardize([[table[station][feature] for feature in FEATURES]
                                 for station in sample])
        labels, _ = analysis.kmeans(matrix, k=k, seed=seed)
        result[name] = dict(zip(sample, labels))
    common = sorted(set(result['early']) & set(result['late']))
    if len(common) < k + 1:
        return dict(stations=len(common), agreement=None, note='两段共有的站点不足')
    # 簇编号本身没有意义：先按最大重合给两段的簇配对，再算归属一致率
    mapping = {}
    for label in set(result['late'].values()):
        counter = defaultdict(int)
        for station in common:
            if result['late'][station] == label:
                counter[result['early'][station]] += 1
        mapping[label] = max(counter, key=counter.get) if counter else None
    matched = sum(1 for station in common
                  if mapping[result['late'][station]] == result['early'][station])
    return dict(stations=len(common), agreement=round(matched / len(common), 4),
                cut_day=cut, note='两段各自聚类后按最大重合配对簇，再统计归属一致率')


def train(database, min_sessions=MIN_SESSIONS, k_range=K_RANGE, seed=42):
    """跑完站点画像：聚类、繁忙度分档、分半稳定性。"""
    rows, _ = sessions.load_sessions(database)
    table = station_features(rows)
    sample = sorted(station for station, stats in table.items()
                    if stats['sessions'] >= min_sessions)
    sparse = sorted(station for station in table if station not in set(sample))
    if len(sample) < min(k_range) + 1:
        raise ValueError(f'会话数不少于 {min_sessions} 的站点只有 {len(sample)} 个，无法聚类')

    matrix, scaling = standardize([[table[station][feature] for feature in FEATURES]
                                   for station in sample])
    k, labels, centers, scores = choose_clusters(matrix, k_range, seed)
    names = name_clusters(centers)
    chosen = partition(matrix, sample, table, k, seed)
    clusters = chosen['clusters']
    # 轮廓系数偏向"把离群站点单独摘出来"的最小 k；再留 2–3 个更细的视图，
    # 供大屏做粒度切换：粗视图看整体差异，细视图能分出可直接命名的站点类型。
    views = [dict(k=item['k'], silhouette=item['silhouette'], clusters=item['clusters'])
             for item in (partition(matrix, sample, table, other, seed)
                          for other in sorted(set(k_range) | {k}) if 3 <= other <= 4)]
    tiers = busyness_tiers(table, clustered=set(sample))
    cluster_of = {station: names[label] for station, label in zip(sample, labels)}
    return dict(
        protocol=dict(min_sessions=min_sessions, k_selection='轮廓系数在 2..6 之间择优',
                      busyness='活跃日日均会话数按分位数切 4 档',
                      stability='前 70% 天与后 30% 天各自聚类后比较归属一致率'),
        stations=len(table), clustered_stations=len(sample), sparse_stations=sparse,
        sessions=len(rows),
        clustered_sessions=sum(table[station]['sessions'] for station in sample),
        cluster_count=k, cluster_scores=scores,
        silhouette=next(item['silhouette'] for item in scores if item['k'] == k),
        clusters=clusters, views=views, tiers=tiers,
        top_stations=[dict(station=station, **table[station],
                           cluster=cluster_of.get(station),
                           busyness=tiers['tiers'][station]['tier'])
                      for station in sorted(table, key=lambda name: -table[name]['sessions'])
                      [:TOP_STATIONS]],
        station_table={station: dict(table[station], cluster=cluster_of.get(station),
                                     busyness=tiers['tiers'][station]['tier'])
                       for station in sorted(table)},
        stability=stability(rows, k, min_sessions, seed=seed),
        scaling=scaling,
        note=('站点画像只描述"这些站的充电行为长什么样"，不代表经营优先级，'
              '也不构成产能或收益结论；会话数不足的站点标注为稀疏，不参与聚类。'))


def build_report(section, path=None):
    """把站点画像写成 markdown 片段。"""
    lines = ['## 五、站点画像聚类与繁忙度分档', '',
             f"共 {section['stations']} 个站点、{section['sessions']} 次会话。会话数不少于 "
             f"{section['protocol']['min_sessions']} 次的 {section['clustered_stations']} "
             f"个站点参与聚类，覆盖 {section['clustered_sessions']} 次会话；其余 "
             f"{len(section['sparse_stations'])} 个站点样本不足，只标注为稀疏，不强行归类。", '',
             f"k 不拍脑袋：{section['protocol']['k_selection']}，最终 k={section['cluster_count']}，"
             '轮廓系数 ' + str(section['silhouette']) + '；候选得分 '
             + '，'.join(f"k={item['k']}: {item['silhouette']}"
                         for item in section['cluster_scores']) + '。', '',
             '繁忙度分档与聚类是两件事：' + section['protocol']['busyness']
             + '，给出一个能直接排序的标签；聚类回答"这是什么类型的站"。', '',
             '| 簇 | 命名 | 站点数 | 会话数 | '
             + ' | '.join(FEATURE_LABELS[feature] for feature in FEATURES) + ' |',
             '|---|---|---:|---:|' + '---:|' * len(FEATURES)]
    for cluster in section['clusters']:
        profile = ' | '.join(str(cluster['profile'][feature]) for feature in FEATURES)
        lines.append(f"| {cluster['cluster']} | {cluster['name']} | {len(cluster['stations'])} "
                     f"| {cluster['sessions']} | {profile} |")
    lines += ['', '更细的参考视图（轮廓系数略低，但簇更容易直接命名，供大屏做粒度切换）：', '',
              '| 视图 | 簇 | 命名 | 站点数 | 会话数 | 日均会话 | 单次电量 | 平均时长 | 直流占比 |',
              '|---|---:|---|---:|---:|---:|---:|---:|---:|']
    for view in section['views']:
        for cluster in view['clusters']:
            profile = cluster['profile']
            lines.append(
                f"| k={view['k']} | {cluster['cluster']} | {cluster['name']} "
                f"| {len(cluster['stations'])} | {cluster['sessions']} "
                f"| {profile['sessions_per_active_day']} | {profile['kwh_per_session']} "
                f"| {profile['duration_mean']} | {profile['dc_share']} |")
    tiers = section['tiers']
    counts = defaultdict(int)
    for entry in tiers['tiers'].values():
        counts[entry['tier']] += 1
    lines += ['', '### 繁忙度分档', '',
              '分档边界（活跃日日均会话数）：'
              + '，'.join(f'{name} {value}' for name, value in
                          zip(['P25', 'P50', 'P75'], tiers['cuts'])) + '。', '',
              '| 档位 | 站点数 |', '|---|---:|']
    for name in TIER_NAMES + ['样本不足']:
        if counts[name]:
            lines.append(f"| {name} | {counts[name]} |")
    lines += ['', f"### 会话量 Top {TOP_STATIONS}（其余站点见 JSON 产物）", '',
              '| 站点 | 会话数 | 活跃日 | 日均会话 | 单次电量 | 平均时长 | 直流占比 | 画像 | 繁忙度 |',
              '|---|---:|---:|---:|---:|---:|---:|---|---|']
    for station in section['top_stations']:
        lines.append(
            f"| `{station['station']}` | {station['sessions']} | {station['active_days']} "
            f"| {round(station['sessions_per_active_day'], 2)} "
            f"| {round(station['kwh_per_session'], 2)} "
            f"| {round(station['duration_mean'], 2)} "
            f"| {round(station['dc_share'], 2)} "
            f"| {station['cluster'] or '稀疏'} | {station['busyness']} |")
    stable = section['stability']
    lines += ['', '### 稳健性检查', '',
              section['protocol']['stability'] + '；'
              + (f"两段共有的 {stable['stations']} 个站点里归属一致率 {stable['agreement']}。"
                 if stable.get('agreement') is not None
                 else f"未完成：{stable.get('note')}"), '',
              f"- {section['note']}", '']
    text = '\n'.join(lines)
    if path:
        Path(path).write_text(text + '\n', encoding='utf-8')
    return text


def main(argv=None):
    parser = argparse.ArgumentParser(description='站点画像聚类与繁忙度分档')
    parser.add_argument('--db', required=True, help='数仓 SQLite 路径')
    parser.add_argument('--out', help='JSON 输出路径')
    parser.add_argument('--report', help='Markdown 报告输出路径')
    parser.add_argument('--min-sessions', type=int, default=MIN_SESSIONS)
    args = parser.parse_args(argv)

    section = train(args.db, args.min_sessions)
    if args.out:
        Path(args.out).write_text(json.dumps(section, ensure_ascii=False, indent=2) + '\n',
                                  encoding='utf-8')
    if args.report:
        build_report(section, args.report)
    print(json.dumps({'stations': section['stations'],
                      'clustered': section['clustered_stations'],
                      'k': section['cluster_count'], 'silhouette': section['silhouette'],
                      'clusters': [dict(cluster=item['cluster'], name=item['name'],
                                        stations=len(item['stations']),
                                        sessions=item['sessions'])
                                   for item in section['clusters']],
                      'stability': section['stability'].get('agreement')},
                     ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
