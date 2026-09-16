#!/usr/bin/env python3
"""第二阶段数据预测：小时级会话量/电量预测（季节基线 + 多元线性回归）。

课程将机器学习列为选做，本模块提供可运行、可评估、可解释的最小实现：
  - 特征：星期、小时、是否周末（回归）；小时画像 + 星期系数（季节基线）
  - 评估：留一星期交叉验证（7 折），输出 MAE / RMSE / 相对误差
  - 模型管理：训练结果写入 model.json（方法、系数、画像、指标、版本、训练窗口）
  - 预测应用：站点小时级会话量与电量预测、低拥堵时段推荐
不使用年份做真实日期预测（源年份未证实），因此按“星期 × 小时”建模而非真实日历。
"""
import argparse
import json
import math
import sqlite3
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from forecast import DAYS, baseline_predict, forecast, linear_predict  # noqa: E402


def load_cells(database):
    """按 星期 × 小时 聚合成建模单元（只统计可用性校验通过的记录）。"""
    connection = sqlite3.connect(database)
    try:
        rows = connection.execute(
            "select weekday, start_hour, count(*) as sessions, sum(cast(kwh as real)) as kwh "
            "from dwd_sessions where weekday_usable='1' and time_of_day_usable='1' "
            "group by weekday, start_hour").fetchall()
    finally:
        connection.close()
    cells = {}
    for weekday, hour, sessions, kwh in rows:
        cells[(weekday, int(hour))] = dict(sessions=int(sessions), kwh=float(kwh or 0.0))
    return cells


def cell_matrix(cells):
    """展开为 (星期, 小时, 会话数, 电量) 观测，缺失单元按 0 补齐，保证 7×24 完整。"""
    observations = []
    for weekday in DAYS:
        for hour in range(24):
            cell = cells.get((weekday, hour), dict(sessions=0, kwh=0.0))
            observations.append(dict(weekday=weekday, hour=hour,
                                     sessions=cell['sessions'], kwh=cell['kwh']))
    return observations


def seasonal_baseline(observations, excluded_weekday=None):
    """季节基线：小时画像 × 星期系数（训练集内估计）。"""
    training = [o for o in observations if o['weekday'] != excluded_weekday]
    hour_totals, hour_counts = defaultdict(float), defaultdict(int)
    weekday_totals, weekday_counts = defaultdict(float), defaultdict(int)
    for row in training:
        hour_totals[row['hour']] += row['sessions']
        hour_counts[row['hour']] += 1
        weekday_totals[row['weekday']] += row['sessions']
        weekday_counts[row['weekday']] += 1
    overall = sum(hour_totals.values()) / sum(hour_counts.values()) if hour_counts else 0.0
    hour_profile = {hour: (hour_totals[hour] / hour_counts[hour]) for hour in range(24)}
    weekday_factor = {day: (weekday_totals[day] / weekday_counts[day] / overall if overall else 1.0)
                      for day in DAYS if weekday_counts[day]}
    return dict(hour_profile=hour_profile, weekday_factor=weekday_factor, overall_mean=overall)


def fit_linear(observations, excluded_weekday=None):
    """普通最小二乘：sessions ≈ b0 + b1*hour + b2*is_weekend + b3*weekday_index。"""
    training = [o for o in observations if o['weekday'] != excluded_weekday]
    features, targets = [], []
    for row in training:
        features.append([1.0, row['hour'], 1.0 if row['weekday'] in ('Sat', 'Sun') else 0.0,
                         DAYS.index(row['weekday'])])
        targets.append(float(row['sessions']))
    width = len(features[0])
    xtx = [[sum(f[i] * f[j] for f in features) for j in range(width)] for i in range(width)]
    xty = [sum(f[i] * t for f, t in zip(features, targets)) for i in range(width)]
    coefficients = solve(xtx, xty)
    return coefficients


def solve(matrix, vector):
    """高斯-约当消元求解正规方程。

    设计矩阵可能秩亏（例如只用一个星期的数据时，is_weekend/weekday_index 恒为常数），
    因此加入极小岭正则项：冗余方向得到 0 系数，可辨识方向保持最小二乘精确解。
    """
    size = len(vector)
    diagonal = max(abs(matrix[i][i]) for i in range(size)) or 1.0
    ridge = 1e-9 * diagonal
    tolerance = 1e-12 * diagonal
    augmented = [[matrix[i][j] + (ridge if i == j else 0.0) for j in range(size)] + [vector[i]]
                 for i in range(size)]
    for column in range(size):
        pivot = max(range(column, size), key=lambda r: abs(augmented[r][column]))
        if abs(augmented[pivot][column]) <= tolerance:
            row = [0.0] * size + [0.0]
            row[column] = 1.0
            augmented[column] = row
            continue
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [v / divisor for v in augmented[column]]
        for row in range(size):
            if row == column:
                continue
            factor = augmented[row][column]
            augmented[row] = [v - factor * w for v, w in zip(augmented[row], augmented[column])]
    return [augmented[i][size] for i in range(size)]


def metrics(actual, predicted):
    pairs = [(a, p) for a, p in zip(actual, predicted) if a is not None]
    if not pairs:
        return dict(samples=0)
    errors = [p - a for a, p in pairs]
    mae = sum(abs(e) for e in errors) / len(errors)
    rmse = math.sqrt(sum(e * e for e in errors) / len(errors))
    mean_actual = sum(a for a, _ in pairs) / len(pairs)
    return dict(samples=len(pairs), mae=round(mae, 4), rmse=round(rmse, 4),
                mean_actual=round(mean_actual, 4),
                relative_error=round(mae / mean_actual, 4) if mean_actual else None)


def evaluate(observations, method):
    """留一星期交叉验证：每次用一个星期做测试集，其余训练。"""
    actual, baseline_out, linear_out = [], [], []
    for weekday in DAYS:
        model = seasonal_baseline(observations, excluded_weekday=weekday)
        coefficients = fit_linear(observations, excluded_weekday=weekday)
        for hour in range(24):
            cell = next(o for o in observations if o['weekday'] == weekday and o['hour'] == hour)
            actual.append(cell['sessions'])
            baseline_out.append(baseline_predict(model, weekday, hour))
            linear_out.append(linear_predict(coefficients, weekday, hour))
    if method == 'seasonal':
        return metrics(actual, baseline_out), baseline_out
    return metrics(actual, linear_out), linear_out


def train(database, clusters_note=None):
    cells = load_cells(database)
    observations = cell_matrix(cells)
    baseline_scores, _ = evaluate(observations, 'seasonal')
    linear_scores, _ = evaluate(observations, 'linear')
    method = 'seasonal' if (baseline_scores.get('mae', 1e9) <= linear_scores.get('mae', 1e9)) else 'linear'
    model = seasonal_baseline(observations)
    coefficients = fit_linear(observations)
    total_sessions = sum(o['sessions'] for o in observations)
    total_kwh = sum(o['kwh'] for o in observations)
    return dict(
        version=datetime.now().strftime('%Y%m%d%H%M'),
        method=method,
        generated_at=datetime.now().astimezone().isoformat(timespec='seconds'),
        source=dict(database=str(database), cells=len(observations), sessions=total_sessions,
                    total_kwh=round(total_kwh, 2), days=len(DAYS), hours=24),
        hour_profile={str(k): round(v, 4) for k, v in sorted(model['hour_profile'].items())},
        weekday_factor={k: round(v, 6) for k, v in sorted(model['weekday_factor'].items())},
        overall_mean=round(model['overall_mean'], 4),
        linear_coefficients=[round(c, 6) for c in coefficients],
        linear_features=['intercept', 'hour', 'is_weekend', 'weekday_index'],
        evaluation=dict(seasonal=baseline_scores, linear=linear_scores, protocol='留一星期交叉验证（7 折）'),
        energy_per_session=round(total_kwh / total_sessions, 4) if total_sessions else None,
        caveats=['源年份未证实，模型按星期×小时建模，不输出真实日历预测。',
                 '样本为课程数据集，站点级推广需重新评估。',
                 '预测用于运营参考与低拥堵推荐，不构成收费或产能承诺。']
        + ([clusters_note] if clusters_note else []))


def build_report(model, path):
    seasonal, linear = model['evaluation']['seasonal'], model['evaluation']['linear']
    lines = ['# 预测模型评估', '',
             f"模型版本 {model['version']}；选中方法 `{model['method']}`；"
             f"训练单元 {model['source']['cells']} 个（7 天 × 24 小时）；"
             f"会话样本 {model['source']['sessions']}。", '',
             '## 交叉验证（留一星期，7 折）', '',
             '| 方法 | 样本 | MAE | RMSE | 实际均值 | 相对误差 |', '|---|---:|---:|---:|---:|---:|',
             f"| 季节基线 | {seasonal['samples']} | {seasonal['mae']} | {seasonal['rmse']} "
             f"| {seasonal['mean_actual']} | {seasonal['relative_error']} |",
             f"| 线性回归 | {linear['samples']} | {linear['mae']} | {linear['rmse']} "
             f"| {linear['mean_actual']} | {linear['relative_error']} |", '',
             '## 小时画像（平均会话数）', '',
             '| 小时 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |',
             '|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    profile = model['hour_profile']
    lines.append('| 会话 | ' + ' | '.join(str(profile[str(h)]) for h in range(12)) + ' |')
    lines += ['', '| 小时 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 |',
              '|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|',
              '| 会话 | ' + ' | '.join(str(profile[str(h)]) for h in range(12, 24)) + ' |', '',
              '## 星期系数', '',
              '| 星期 | ' + ' | '.join(DAYS) + ' |',
              '|---|' + '---:|' * len(DAYS)]
    lines.append('| 系数 | ' + ' | '.join(f"{model['weekday_factor'][d]:.4f}" for d in DAYS) + ' |')
    lines += ['', f"每次会话平均电量：{model['energy_per_session']} kWh（用于电量预测）。", '',
              '## 限制', ''] + [f'- {c}' for c in model['caveats']]
    Path(path).write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', required=True, help='数仓 SQLite 路径')
    parser.add_argument('--out', required=True, help='模型 model.json 输出路径')
    parser.add_argument('--report', help='评估报告 Markdown 输出路径')
    args = parser.parse_args(argv)
    model = train(args.db)
    Path(args.out).write_text(json.dumps(model, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if args.report:
        build_report(model, args.report)
    print(json.dumps({'method': model['method'], 'version': model['version'],
                      'seasonal': model['evaluation']['seasonal'],
                      'linear': model['evaluation']['linear']}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
