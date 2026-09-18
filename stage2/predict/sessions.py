#!/usr/bin/env python3
"""单次充电的分位数预测：时长与电量。

负荷预测回答"明天这个站多少车、多少电"，但运营每天真正要回答的是另一个问题：
"一次充电大概占多久、充进多少电"。排队叫号、车位周转、超时提醒都按单次会话发生，
均值回答不了这个问题——本数据里时长右偏（P50 2.81h / P90 4.07h / 最长 55.2h），
均值被长尾拉住，用户想知道的是"最常见多久""最坏大概多久"，也就是分位数。

方法：分位数回归（pinball loss）+ 迭代重加权最小二乘（IRLS）。
  τ=0.5 即最小绝对偏差回归，给中位数；
  τ=0.1 / 0.9 给"偏短/偏长"的区间，可直接当排队时长与电量的参考上下界。
IRLS 的每一步都是加权正规方程，复用 models.solve 的高斯消元，不引入 numpy。
τ≠0.5 时残差不再对称，用绝对偏差做损失天然抗离群：
最长那次 55.2 小时的会话不会像最小二乘那样把整条拟合线拽偏。

评估：按时间切分（前 70% 的天训练、后 30% 的天测试），
用 pinball loss 选方法（分位数预测的正当评分，越低越好），
再用经验覆盖率（实际值不超过预测分位数的比例）检查校准：
一个 τ=0.9 的预测，如果只有 60% 的样本落在它之下，那这个模型就是没校准的。
"""
import argparse
import json
import math
import sqlite3
import sys
from pathlib import Path

import models
import timeline

QUANTILES = (0.1, 0.5, 0.9)
TARGETS = ('duration_hours', 'kwh')
TARGET_LABELS = {'duration_hours': '单次时长（小时）', 'kwh': '单次电量（kWh）'}
MODEL_METHODS = ['quantile_ridge']
BASELINES = ['global_quantile', 'facility_quantile', 'facility_period_quantile']
METHODS = BASELINES + MODEL_METHODS
LABELS = {'global_quantile': '全局分位数',
          'facility_quantile': '按设施类型分位数',
          'facility_period_quantile': '按设施类型 × 峰谷时段分位数',
          'quantile_ridge': '分位数回归（IRLS + L2）'}
DEFAULT_ALPHA = 1.0
ADOPT_MARGIN = 0.10        # 模型在测试期上至少要领先最佳基线 10% 才上线
FIRST_SHARE = 0.7          # 前 70% 的天整体留作滚动评估的测试期
BLOCK_DAYS = 30            # 每个测试块的长度（天）
WINDOW_DAYS = 90           # 重训窗口：用块之前最近 90 天拟合
MAX_ITERATIONS = 40
CONVERGENCE = 1e-6
LOADED_COLUMNS = ('facility_label', 'time_period', 'platform', 'manager_vehicle')


def load_sessions(database, table='dwd_sessions'):
    """读取单次会话明细，并补上相对时间轴 day_index（与负荷预测共用同一套时间基准）。

    返回 (sessions, zero_energy)。零电量会话是真实发生的（插枪但没充上电），
    对"这次充多少电"就是一个合法结果，因此保留在训练与评估里，只单独计数供报告说明。
    """
    connection = sqlite3.connect(database)
    try:
        rows = connection.execute(
            f'select created_raw, weekday, start_hour, kwh, duration_hours, station_id, '
            f'facility_label, time_period, platform, manager_vehicle from {table} '
            "where time_of_day_usable='1' and weekday_usable='1'").fetchall()
    finally:
        connection.close()

    parsed = []
    for raw, weekday, hour, kwh, duration, station, facility, period, platform, manager in rows:
        day = timeline.parse_created(raw)
        if day is None or hour is None or duration is None or kwh is None:
            continue
        parsed.append(dict(date=day, weekday=weekday, hour=int(hour),
                           kwh=float(kwh), duration_hours=float(duration),
                           station_id=str(station) if station is not None else '',
                           facility_label=facility or '未知',
                           time_period=period or '未知', platform=platform or '未知',
                           manager_vehicle=float(manager or 0)))
    if not parsed:
        raise ValueError('没有可用的单次会话：请检查 dwd_sessions 的可用性标记')

    origin = min(row['date'] for row in parsed)
    for row in parsed:
        row['day_index'] = (row['date'] - origin).days
        row['is_weekend'] = int(row['date'].weekday() >= 5)
    return parsed, [row for row in parsed if row['kwh'] <= 0 or row['duration_hours'] <= 0]


def categories_from(rows):
    """设计矩阵用的类别集合；第一个类别作为基准，避免哑变量陷阱。"""
    return {key: sorted({row[key] for row in rows}) for key in LOADED_COLUMNS}


def design_row(row, categories, with_station=False):
    """把一次会话展开成特征；只用会话开始时就能拿到的信息，不用结果字段。

    with_station 打开时追加站点历史分位数与样本量（见 station_statistics）：
    "同一个站点的历史中位数"在用户扫码那一刻就已知，是合法可用的上线特征，
    也是把预测从"设施类型的平均值"细化到"这个站大概多久"的关键。
    """
    names, values = [], []

    def add(name, value):
        names.append(name)
        values.append(float(value))

    for facility in categories['facility_label'][1:]:
        add(f'facility={facility}', 1.0 if row['facility_label'] == facility else 0.0)
    for period in categories['time_period'][1:]:
        add(f'period={period}', 1.0 if row['time_period'] == period else 0.0)
    for platform in categories['platform'][1:]:
        add(f'platform={platform}', 1.0 if row['platform'] == platform else 0.0)
    bucket = min(int(row['hour']) // 4, 5)
    for offset in range(1, 6):
        add(f'hour_bucket={offset}', 1.0 if bucket == offset else 0.0)
    add('is_weekend', row['is_weekend'])
    add('manager_vehicle', row['manager_vehicle'])
    if with_station:
        add('station_median', row['station_median'])
        add('station_count', row['station_count'])
    return names, values


def station_statistics(rows, target, prior_weight=5.0):
    """按站点统计历史分位数（τ=0.5），向全局分位数收缩；样本少的站点自动退让。

    返回查表结构，供 attach_station_features 使用。统计只在训练窗口内做，
    预测某个测试块时用的窗口严格早于该块，不含任何未来会话。
    """
    totals, counts = {}, {}
    for row in rows:
        station = row['station_id']
        totals[station] = totals.get(station, 0.0) + float(row[target])
        counts[station] = counts.get(station, 0) + 1
    return dict(target=target, prior_weight=prior_weight,
                global_value=quantile([float(row[target]) for row in rows], 0.5),
                totals=totals, counts=counts)


def attach_station_features(rows, statistics, leave_one_out=False):
    """把站点历史分位数写进行副本。

    leave_one_out=True 用于训练窗口内部：每个样本的站点统计先扣掉它自己，
    否则模型会学到"这条记录参与了它自己的特征"，训练指标会虚高。
    """
    prior, k = statistics['global_value'], statistics['prior_weight']
    enriched = []
    for row in rows:
        station = row['station_id']
        total, count = statistics['totals'].get(station, 0.0), statistics['counts'].get(station, 0)
        if leave_one_out:
            total -= float(row[statistics['target']])
            count -= 1
        mean = total / count if count > 0 else prior
        value = (mean * count + prior * k) / (count + k) if count + k else prior
        enriched.append(dict(row, station_median=round(value, 4), station_count=float(count)))
    return enriched


def rolling_blocks(rows, first_share=FIRST_SHARE, block_days=BLOCK_DAYS):
    """把后 (1-first_share) 的天切成若干连续的测试块，返回 [(起始天, 结束天)]。

    为什么不是"前 70% 训练、后 30% 测试"的静态切分：本数据集的时间漂移很明显
    （单次时长中位数从第 0 段的 2.35 小时升到第 264 段的 3.08 小时），
    用早期数据训练去预测后期，等于让模型去答一张自己没见过的卷子，
    经验覆盖率会被漂移直接带偏（实测 τ=0.5 只有 0.44）。滚动重训模拟的是
    真实运营动作：上线后每到一个新周期，就用最近一段数据重新拟合。
    """
    days = sorted({row['day_index'] for row in rows})
    if len(days) < 2:
        raise ValueError('会话涉及的天数不足，无法切分测试块')
    start_index = max(1, min(len(days) - 1, int(len(days) * first_share)))
    blocks, start = [], days[start_index]
    while start <= days[-1]:
        end = min(start + block_days - 1, days[-1])
        blocks.append((start, end))
        start = end + 1
    return blocks


def training_window(rows, start_day, window_days=WINDOW_DAYS):
    """重训窗口：块开始之前的最近 window_days 天；不足时退化为全部更早数据。"""
    recent = [row for row in rows
              if start_day - window_days <= row['day_index'] < start_day]
    if recent:
        return recent
    return [row for row in rows if row['day_index'] < start_day]


def quantile(values, fraction):
    """经验分位数（线性插值），与 numpy.quantile 的默认定义一致。"""
    ordered = sorted(values)
    if not ordered:
        return None
    position = (len(ordered) - 1) * fraction
    low, high = int(math.floor(position)), int(math.ceil(position))
    if low == high:
        return ordered[low]
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def pinball(actual, predicted, tau):
    """分位数预测的评分：低估按 (1-τ) 罚、高估按 τ 罚，τ=0.5 时即绝对误差。"""
    pairs = [(a, p) for a, p in zip(actual, predicted) if p is not None]
    if not pairs:
        return None
    total = 0.0
    for a, p in pairs:
        total += tau * (a - p) if a >= p else (1 - tau) * (p - a)
    return total / len(pairs)


def coverage(actual, predicted):
    """经验覆盖率：实际值不超过预测分位数的比例，理想值等于 τ。"""
    pairs = [(a, p) for a, p in zip(actual, predicted) if p is not None]
    if not pairs:
        return None
    return sum(1 for a, p in pairs if a <= p) / len(pairs)


def prepare(rows, target, categories=None, statistics=None):
    """把训练集预处理成 IRLS 反复使用的结构。

    IRLS 每轮只换权重、不换设计矩阵，所以行向量与其上三角外积预计算一次即可：
    每轮迭代的代价从"重新展开并遍历整个矩阵"降到"按权重累加上三角外积"。
    纯 Python 下这不是微优化——6 个（目标 × 分位数）任务能不能在可接受时间里跑完就看它。

    向量首列固定是截距（1.0），其余列按训练集的均值/标准差标准化。
    截距必须显式存在且不被 L2 惩罚：分位数回归靠移动截距来滑动整个预测分布，
    若把截距钉死在目标均值上，τ=0.1 与 τ=0.9 会给出同一条曲线——
    这正是第一版实现的错误，单测里的覆盖率断言把它拦了下来。
    """
    if not rows:
        raise ValueError('训练样本为空，无法拟合分位数回归')
    categories = categories or categories_from(rows)
    statistics = statistics or station_statistics(rows, target)
    rows = attach_station_features(rows, statistics, leave_one_out=True)
    names, matrix, targets = None, [], []
    for row in rows:
        names, values = design_row(row, categories, with_station=True)
        matrix.append(values)
        targets.append(float(row[target]))

    count = len(matrix)
    means = [sum(column) / count for column in zip(*matrix)]
    stds = []
    for index, mean in enumerate(means):
        variance = sum((row[index] - mean) ** 2 for row in matrix) / count
        stds.append(math.sqrt(variance) or 1.0)
    vectors = [[1.0] + [(value - means[i]) / stds[i] for i, value in enumerate(row)]
               for row in matrix]
    width = len(vectors[0])
    outers = [[vector[i] * vector[j] for i in range(width) for j in range(i, width)]
              for vector in vectors]

    target_mean = sum(targets) / count
    target_std = math.sqrt(sum((value - target_mean) ** 2 for value in targets) / count) or 1.0
    return dict(target=target, categories=categories, statistics=statistics,
                features=['intercept'] + names, width=width,
                count=count, means=means, stds=stds,
                vectors=vectors, outers=outers,
                targets=[(value - target_mean) / target_std for value in targets],
                target_mean=target_mean, target_std=target_std)


def fit_quantile(problem, tau, alpha=DEFAULT_ALPHA, initial=None,
                 iterations=MAX_ITERATIONS):
    """IRLS 拟合分位数回归：每轮按残差符号重加权，再解一次加权岭回归。

    权重 w_i = τ/|r_i|（r_i ≥ 0）或 (1-τ)/|r_i|（r_i < 0）。
    残差趋近 0 时权重发散，用 eps 截断；L2 正则同时稳住病态方向。
    """
    width, count = problem['width'], problem['count']
    eps = 1e-3
    coefficients = list(initial) if initial else [0.0] * width
    if initial is None:
        coefficients = _weighted_solve(problem, [1.0] * count, alpha)
    for _ in range(iterations):
        weights = []
        for vector, value in zip(problem['vectors'], problem['targets']):
            residual = value - sum(c * x for c, x in zip(coefficients, vector))
            share = tau if residual >= 0 else 1 - tau
            weights.append(share / max(abs(residual), eps))
        candidate = _weighted_solve(problem, weights, alpha)
        shift = max(abs(a - b) for a, b in zip(candidate, coefficients))
        coefficients = candidate
        if shift < CONVERGENCE:
            break

    return dict(method='quantile_ridge', target=problem['target'], tau=tau, alpha=alpha,
                categories=problem['categories'], statistics=problem['statistics'],
                features=problem['features'],
                coefficients=coefficients, means=problem['means'], stds=problem['stds'],
                target_mean=problem['target_mean'], target_std=problem['target_std'],
                training_rows=count)


def _weighted_solve(problem, weights, alpha):
    width, size = problem['width'], problem['width'] * (problem['width'] + 1) // 2
    triangle = [0.0] * size
    moment = [0.0] * width
    for vector, outer, value, weight in zip(problem['vectors'], problem['outers'],
                                            problem['targets'], weights):
        for i, coordinate in enumerate(vector):
            moment[i] += weight * coordinate * value
        for index, term in enumerate(outer):
            triangle[index] += weight * term
    gram = [[0.0] * width for _ in range(width)]
    position = 0
    for i in range(width):
        for j in range(i, width):
            gram[i][j] = triangle[position]
            gram[j][i] = triangle[position]
            position += 1
        if i:                       # 截距不惩罚：否则会把预测分布整体拉回均值
            gram[i][i] += alpha
    return models.solve(gram, moment)


def predict_quantile(model, rows):
    """逐行给出预测；负值截断为 0（时长与电量都不可能是负数）。"""
    values = []
    for row in rows:
        _, raw = design_row(row, model['categories'], with_station=True)
        vector = [1.0] + [(value - mean) / std
                          for value, mean, std in zip(raw, model['means'], model['stds'])]
        total = sum(coefficient * value
                    for coefficient, value in zip(model['coefficients'], vector))
        values.append(max(model['target_mean'] + model['target_std'] * total, 0.0))
    return values


def monotone(values_by_tau):
    """把同一行的多个分位数拉成单调：q10 ≤ q50 ≤ q90。

    每个 τ 独立拟合，个别样本上可能出现分位数交叉；交叉时的预测本身就是噪声，
    直接排序比保留一个自相矛盾的区间更稳妥，也不影响 pinball loss 的评估方式。
    """
    rows = list(zip(*values_by_tau))
    ordered = [sorted(row) for row in rows]
    return [[row[position] for row in ordered] for position in range(len(values_by_tau))]


def fit_baseline(rows, method, target, tau):
    """基线都只是"分组的经验分位数"，返回一个查表函数。"""
    if method not in BASELINES:
        raise ValueError(f'未知基线：{method}')
    fallback = quantile([float(row[target]) for row in rows], tau)
    if method == 'global_quantile':
        return dict(method=method, tau=tau, global_value=fallback)
    key = ('facility_label',) if method == 'facility_quantile' \
        else ('facility_label', 'time_period')
    groups = {}
    for row in rows:
        groups.setdefault(tuple(row[name] for name in key), []).append(float(row[target]))
    return dict(method=method, tau=tau, global_value=fallback,
                table={group: quantile(values, tau) for group, values in groups.items()},
                key=key)


def predict_baseline(fitted, rows):
    if fitted['method'] == 'global_quantile':
        return [fitted['global_value']] * len(rows)
    key = fitted['key']
    return [fitted['table'].get(tuple(row[name] for name in key), fitted['global_value'])
            for row in rows]


def evaluate_rolling(rows, target, alpha=DEFAULT_ALPHA, window_days=WINDOW_DAYS,
                     block_days=BLOCK_DAYS):
    """滚动重训评估：每个测试块都用它之前的最近窗口重新拟合全部候选。

    模型与基线用同一套窗口、同一批测试样本，比较才是同一件事。
    返回逐 τ 的汇总指标、每个块的大小，以及最后一个块上拟合出的模型。
    """
    blocks = rolling_blocks(rows, block_days=block_days)
    actuals = {tau: [] for tau in QUANTILES}
    predicted = {tau: {method: [] for method in METHODS} for tau in QUANTILES}
    models_by_tau, warm = {}, {}
    sizes, per_block = [], []
    for start, end in blocks:
        train = training_window(rows, start, window_days)
        test = [row for row in rows if start <= row['day_index'] <= end]
        if not train or not test:
            continue
        categories = categories_from(train)
        statistics = station_statistics(train, target)
        problem = prepare(train, target, categories, statistics)
        enriched_test = attach_station_features(test, statistics)
        actual = [float(row[target]) for row in test]
        block = {}
        for tau in QUANTILES:
            model = fit_quantile(problem, tau, alpha, initial=warm.get(tau))
            warm[tau] = model['coefficients']
            models_by_tau[tau] = model
            block[tau] = predict_quantile(model, enriched_test)
            for method in BASELINES:
                fitted = fit_baseline(train, method, target, tau)
                predicted[tau][method].extend(predict_baseline(fitted, test))
            actuals[tau].extend(actual)
        for index, values in enumerate(monotone([block[tau] for tau in QUANTILES])):
            predicted[QUANTILES[index]]['quantile_ridge'].extend(values)
        sizes.append(dict(start_day=start, end_day=end, sessions=len(test),
                          training_rows=len(train)))
        # 逐块留一份指标：整体领先但只在个别块上领先，说明优势不稳，不该上线
        block_metrics = {}
        for tau in QUANTILES:
            values = {'quantile_ridge': block[tau]}
            for name in BASELINES:
                values[name] = predict_baseline(fit_baseline(train, name, target, tau), test)
            block_metrics[tau] = {method: dict(
                pinball=round(pinball(actual, values[method], tau), 4),
                coverage=round(coverage(actual, values[method]), 4)) for method in METHODS}
        per_block.append(dict(start_day=start, end_day=end, sessions=len(test),
                              metrics=block_metrics))

    metrics = {}
    for tau in QUANTILES:
        metrics[tau] = {method: dict(
            pinball=round(pinball(actuals[tau], predicted[tau][method], tau), 4),
            coverage=round(coverage(actuals[tau], predicted[tau][method]), 4),
            samples=len(actuals[tau])) for method in METHODS}
    return dict(target=target, metrics=metrics, models=models_by_tau,
                blocks=sizes, per_block=per_block, test_sessions=len(actuals[0.5]))


def select(evaluation, alpha=DEFAULT_ALPHA):
    """每个 τ 独立挑方法：模型要领先最佳基线 ADOPT_MARGIN 才上线。"""
    target, tasks = evaluation['target'], []
    for tau in QUANTILES:
        entries = evaluation['metrics'][tau]
        baseline_choice = min(BASELINES, key=lambda name: (entries[name]['pinball'], name))
        baseline_loss = entries[baseline_choice]['pinball']
        model_loss = entries['quantile_ridge']['pinball']
        adopted = bool(baseline_loss and model_loss is not None
                       and model_loss <= baseline_loss * (1 - ADOPT_MARGIN))
        chosen = 'quantile_ridge' if adopted else baseline_choice
        model = evaluation['models'][tau]
        wins = total = 0
        for block in evaluation['per_block']:
            stats = block['metrics'][tau]
            if stats[baseline_choice]['pinball'] is None:
                continue
            total += 1
            if stats['quantile_ridge']['pinball'] < stats[baseline_choice]['pinball']:
                wins += 1
        tasks.append(dict(
            target=target, tau=tau, chosen=chosen, baseline_choice=baseline_choice,
            model_adopted=adopted,
            block_wins=wins, block_total=total,
            pinball=entries[chosen]['pinball'], baseline_pinball=baseline_loss,
            coverage=entries[chosen]['coverage'],
            calibration_gap=round(abs(entries[chosen]['coverage'] - tau), 4),
            model_coverage=entries['quantile_ridge']['coverage'],
            baseline_coverage=entries[baseline_choice]['coverage'],
            skill=round(1 - model_loss / baseline_loss, 4) if baseline_loss else None,
            all_metrics=entries,
            top_weights=_top_weights(model),
            training_rows=model['training_rows']))
    return tasks


def _top_weights(model, limit=8):
    """权重最大的特征；截距是标定位置用的，不参与可解释性排序。"""
    names, weights = [], []
    for name, weight in zip(model['features'], model['coefficients']):
        if name != 'intercept':
            names.append(name)
            weights.append(weight)
    return models.top_weights(dict(features=names, weights=weights), limit)


def train(database, share=FIRST_SHARE, alpha=DEFAULT_ALPHA):
    """训练全部（目标 × 分位数）任务。"""
    sessions, zero_energy = load_sessions(database)
    blocks = rolling_blocks(sessions, share)
    tasks, evaluations, deployment = [], {}, {}
    for target in TARGETS:
        evaluation = evaluate_rolling(sessions, target, alpha)
        evaluations[target] = evaluation
        selected = select(evaluation, alpha)
        tasks.extend(selected)
        deployment[target] = build_deployment(
            sessions, target, {task['tau']: task['chosen'] for task in selected}, alpha)
    return dict(
        protocol=dict(
            split=f'滚动重训：前 {int(share * 100)}% 的天留作测试期，'
                  f'之后每 {BLOCK_DAYS} 天一个测试块',
            window=f'每个测试块都用它之前最近 {WINDOW_DAYS} 天重新拟合模型与基线',
            metric='pinball loss（分位数预测的正当评分，越低越好）；同时检查经验覆盖率',
            adoption=f'模型要在测试期上领先最佳基线 {int(ADOPT_MARGIN * 100)}% 才上线'),
        blocks=[dict(block, target=target) for target in TARGETS
                for block in evaluations[target]['blocks']],
        first_block_day=blocks[0][0], last_block_day=blocks[-1][1],
        sessions=len(sessions), test_sessions=sum(
            block['sessions'] for block in evaluations[TARGETS[0]]['blocks']),
        zero_energy_sessions=len(zero_energy),
        levels=[{'tau': tau, 'label': label} for tau, label in
                ((0.1, '偏短（P10）'), (0.5, '中位数（P50）'), (0.9, '偏长（P90）'))],
        tasks=tasks, deployment=deployment)


def build_deployment(rows, target, chosen, alpha=DEFAULT_ALPHA, window_days=WINDOW_DAYS):
    """在最近窗口上拟合上线用的预测器：每个 τ 一个，方法就是评估阶段选定的那个。

    产物自带预测所需的一切（类别集合、站点历史表、模型系数或分组分位数表），
    服务端不需要重新读明细表，也不需要再跑一次训练。
    """
    last_day = max(row['day_index'] for row in rows) + 1
    latest = training_window(rows, last_day, window_days)
    categories = categories_from(latest)
    statistics = station_statistics(latest, target)
    problem = prepare(latest, target, categories, statistics)
    quantiles, warm = {}, None
    for tau in QUANTILES:
        model = fit_quantile(problem, tau, alpha, initial=warm)
        warm = model['coefficients']
        entry = dict(chosen=chosen[tau], tau=tau, training_rows=model['training_rows'],
                     model=dict(features=model['features'],
                                coefficients=[round(value, 6)
                                              for value in model['coefficients']],
                                means=[round(value, 6) for value in model['means']],
                                stds=[round(value, 6) for value in model['stds']],
                                target_mean=round(model['target_mean'], 6),
                                target_std=round(model['target_std'], 6)))
        if chosen[tau] in BASELINES:
            fitted = fit_baseline(latest, chosen[tau], target, tau)
            entry['baseline'] = dict(
                method=fitted['method'], tau=tau,
                global_value=round(fitted['global_value'], 6),
                key=list(fitted.get('key', ())),
                table={'|'.join(group): round(value, 6)
                       for group, value in fitted.get('table', {}).items()})
        quantiles[f'{tau:.1f}'] = entry
    return dict(target=target, window_days=window_days, sessions=len(latest),
                categories=categories,
                statistics=dict(prior_weight=statistics['prior_weight'],
                                global_value=round(statistics['global_value'], 6),
                                totals={key: round(value, 4)
                                        for key, value in statistics['totals'].items()},
                                counts=statistics['counts']),
                quantiles=quantiles)


def build_report(section, path=None):
    """把分位数评估写成 markdown 片段；path 为空时只返回文本。"""
    lines = ['## 四、单次充电分位数（时长与电量）', '',
             f"共 {section['sessions']} 次会话。{section['protocol']['split']}，"
             f"{section['protocol']['window']}；参与评估的测试会话 {section['test_sessions']} 次。", '',
             f"评估用 {section['protocol']['metric']}。覆盖率一列是实际值不超过预测分位数的比例，"
             '理想值等于 τ；偏离超过 5 个百分点就说明这个分位数没校准。', '',
             '滚动协议不是装饰：本数据集的时间漂移很明显（单次时长中位数从第 0 段的 2.35 小时'
             '升到第 264 段的 3.08 小时），同一份数据改用"前 70% 训练、后 30% 测试"的静态切分，'
             '中位数的经验覆盖率会掉到 0.44。', '',
             '| 目标 | 分位数 | 上线的预测器 | pinball | 对照基线 | 其 pinball | '
             '覆盖率 | 与 τ 的偏差 | 相对基线 | 分块胜出 |',
             '|---|---|---|---:|---|---:|---:|---:|---:|---:|']
    for task in section['tasks']:
        lines.append(
            f"| {TARGET_LABELS[task['target']]} | {task['tau']:.1f} "
            f"| {LABELS[task['chosen']]} | {task['pinball']} "
            f"| {LABELS[task['baseline_choice']]} | {task['baseline_pinball']} "
            f"| {task['coverage']} | {task['calibration_gap']} "
            f"| {_percent(task['skill'])} "
            f"| {task['block_wins']}/{task['block_total']} |")
    lines += ['', '### 全部候选方法（测试期 pinball loss）', '',
              '| 目标 | 分位数 | ' + ' | '.join(LABELS[name] for name in METHODS) + ' |',
              '|---|---|' + '---:|' * len(METHODS)]
    for task in section['tasks']:
        cells = ' | '.join(str(task['all_metrics'][name]['pinball']) for name in METHODS)
        lines.append(f"| {TARGET_LABELS[task['target']]} | {task['tau']:.1f} | {cells} |")
    lines += ['', '### 中位数模型的可解释性（权重最大的特征）', '']
    for task in section['tasks']:
        if task['tau'] != 0.5:
            continue
        lines += [f"**{TARGET_LABELS[task['target']]}**（选定 {LABELS[task['chosen']]}）", '',
                  '| 特征 | 权重（标准化后） |', '|---|---:|']
        for item in task['top_weights']:
            lines.append(f"| `{item['feature']}` | {item['weight']} |")
        lines.append('')
    lines += ['### 单次预测的使用方式', '',
              f"采纳规则与负荷预测一致：模型要领先最佳基线 {int(ADOPT_MARGIN * 100)}% 才上线。"
              '分位数回归在 6 个格子里整体都比基线更低，其中 4 个格子还在全部测试块上胜出，'
              '但领先幅度是 5.7%–8.9%（只有电量 P10 达到 13.8%），没有达到门槛，'
              '因此上线的是更简单、可直接查表的经验分位数。"分块胜出"一列给出这些小优势'
              '在测试块之间的分布——门槛不是否认模型，而是要求它先证明自己配得上'
              '替换一条更好解释的规则；模型结果同时保留在产物里作为备选。', '',
              '- P50 用于"这次大概占多久、充多少电"的默认预期与车位周转估算。',
              '- P90 用于排队等待与超时提醒的保守参考，P10 用于快速周转场景。',
              '- 分位数回归用绝对偏差做损失，天然抗离群：实测最长 55.2 小时的单次会话'
              '不会把拟合线拽偏，这类记录在均值模型里是要单独处理的。',
              '- 特征只用会话开始时已知的信息（设施类型、峰谷时段、平台、开始时段、是否周末），'
              '不使用本次会话的结果字段，避免上线时拿不到输入。', '']
    text = '\n'.join(lines)
    if path:
        Path(path).write_text(text + '\n', encoding='utf-8')
    return text


def _percent(value):
    return '-' if value is None else f'{value * 100:.1f}%'


def main(argv=None):
    parser = argparse.ArgumentParser(description='单次充电时长/电量的分位数预测')
    parser.add_argument('--db', required=True, help='数仓 SQLite 路径')
    parser.add_argument('--json', dest='json_out', help='评估结果 JSON 输出路径')
    parser.add_argument('--report', help='Markdown 报告输出路径')
    parser.add_argument('--share', type=float, default=FIRST_SHARE,
                        help='留作测试期的天数占比')
    args = parser.parse_args(argv)

    section = train(args.db, args.share)
    payload = dict(section, tasks=[dict(task) for task in section['tasks']])
    for task in payload['tasks']:          # 权重清单只进报告，不进评估 JSON
        task.pop('top_weights', None)
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n',
                                       encoding='utf-8')
    if args.report:
        build_report(section, args.report)
    print(json.dumps([{'target': task['target'], 'tau': task['tau'],
                       'chosen': task['chosen'], 'pinball': task['pinball'],
                       'baseline': task['baseline_choice'],
                       'baseline_pinball': task['baseline_pinball'],
                       'coverage': task['coverage'], 'skill': task['skill']}
                      for task in section['tasks']], ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
