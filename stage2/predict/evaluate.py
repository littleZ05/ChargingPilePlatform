#!/usr/bin/env python3
"""评估：指标、扩展窗口滚动前进、基线对比与报告。

为什么不用留一星期交叉验证：那等于拿同一段日历里其他星期几去预测被留出的星期几，
既不是上线场景（上线永远是拿历史预测未来），又因为时间自相关让结果偏乐观。
这里改成扩展窗口滚动前进——用第 0..t-1 天拟合，预测第 t 天，逐日推进。
"""
import math
import argparse
import json
import sys
from pathlib import Path

import baselines
import features
import models
import timeline

DEFAULT_METHODS = baselines.METHODS + models.METHODS


def label_of(method):
    return baselines.LABELS.get(method) or models.LABELS.get(method) or method


def mae(actual, predicted):
    pairs = [(a, p) for a, p in zip(actual, predicted) if p is not None]
    if not pairs:
        return None
    return sum(abs(a - p) for a, p in pairs) / len(pairs)


def rmse(actual, predicted):
    pairs = [(a, p) for a, p in zip(actual, predicted) if p is not None]
    if not pairs:
        return None
    return math.sqrt(sum((a - p) ** 2 for a, p in pairs) / len(pairs))


def mape(actual, predicted):
    """只统计真实值大于 0 的样本：零值会把百分比误差放大到无意义。"""
    pairs = [(a, p) for a, p in zip(actual, predicted) if p is not None and a > 0]
    if not pairs:
        return None
    return sum(abs(a - p) / a for a, p in pairs) / len(pairs)


def skill_score(model_mae, baseline_mae):
    """相对基线的误差下降比例；>0 才说明模型有价值。"""
    if model_mae is None or not baseline_mae:
        return None
    return round(1 - model_mae / baseline_mae, 4)


def metrics(actual, predicted):
    values = [a for a, p in zip(actual, predicted) if p is not None]
    stats = dict(samples=len(values),
                 mae=_round(mae(actual, predicted)),
                 rmse=_round(rmse(actual, predicted)),
                 mape=_round(mape(actual, predicted)))
    if values:
        stats['mean_actual'] = round(sum(values) / len(values), 4)
        if stats['mae'] is not None:
            stats['relative_error'] = round(stats['mae'] / stats['mean_actual'], 4) \
                if stats['mean_actual'] else None
    return stats


def _round(value, digits=4):
    return None if value is None else round(value, digits)


def walk_forward(rows, method, column='sessions', min_train_days=28,
                 start_day=None, end_day=None, alpha=models.DEFAULT_ALPHA):
    """扩展窗口滚动前进。返回逐点预测与分层指标。"""
    spec = features.task_spec(column)
    usable = sorted((row for row in rows if row['mask'] and row.get(column) is not None),
                    key=lambda row: (row['day_index'], row['hour']))
    days = sorted({row['day_index'] for row in usable})
    first = min_train_days if start_day is None else start_day
    by_day = {}
    for row in usable:
        by_day.setdefault(row['day_index'], []).append(row)
    is_model = method in models.METHODS
    accumulator = (models.ExpandingRidge(spec, alpha, residual=(method == 'ridge_residual'))
                   if is_model else None)
    train, added = [], set()

    records, folds, fallbacks = [], 0, 0
    for day_index in days:
        for earlier in days:                      # 把当天之前的观测并入训练集
            if earlier >= day_index:
                break
            if earlier in added:
                continue
            added.add(earlier)
            if is_model:
                accumulator.add(by_day[earlier])
            else:
                train.extend(by_day[earlier])
        if day_index < first or (end_day is not None and day_index > end_day):
            continue
        if not added:
            continue
        test = by_day[day_index]
        if is_model:
            predicted, missed = models.predict(accumulator.fit(), test, spec)
        else:
            fitted = baselines.fit(train, method, spec)
            predicted, missed = baselines.predict(test, method, fitted, spec)
        fallbacks += missed
        folds += 1
        for row, value in zip(test, predicted):
            records.append(dict(day_index=row['day_index'], hour=row['hour'],
                                weekday=row['weekday'], is_weekend=row['is_weekend'],
                                actual=float(row[column]),
                                predicted=None if value is None else round(float(value), 4)))

    actual = [record['actual'] for record in records]
    predicted = [record['predicted'] for record in records]
    result = dict(method=method, label=label_of(method), target=column,
                  base=spec['base'], horizon=spec['hours'],
                  folds=folds, fallbacks=fallbacks,
                  metrics=metrics(actual, predicted),
                  by_hour=_by_hour(records), by_day_type=_by_day_type(records),
                  predictions=records)
    return result


def _by_hour(records):
    grouped = {}
    for hour in range(24):
        subset = [record for record in records if record['hour'] == hour]
        if not subset:
            continue
        grouped[hour] = metrics([r['actual'] for r in subset],
                                [r['predicted'] for r in subset])
    return grouped


def _by_day_type(records):
    grouped = {}
    for label, flag in (('工作日', 0), ('周末', 1)):
        subset = [record for record in records if record['is_weekend'] == flag]
        if not subset:
            continue
        grouped[label] = metrics([r['actual'] for r in subset], [r['predicted'] for r in subset])
    return grouped


def compare(rows, methods=None, column='sessions', min_train_days=28,
            start_day=None, end_day=None, alpha=models.DEFAULT_ALPHA):
    """跑完全部基线，并把每个方法的 skill score 对齐到最佳基线。"""
    methods = methods or DEFAULT_METHODS
    results = [walk_forward(rows, method, column, min_train_days, start_day, end_day, alpha)
               for method in methods]
    scored = [r for r in results if r['metrics']['mae'] is not None]
    baseline_only = [r for r in scored if r['method'] in baselines.METHODS]
    best = min(baseline_only, key=lambda r: r['metrics']['mae']) if baseline_only else None
    raw_best = min((r['metrics']['mae'] for r in baseline_only), default=None)
    for result in results:
        result['skill_vs_best_baseline'] = skill_score(result['metrics']['mae'], raw_best)
        result['better_than_best_baseline'] = (result['metrics']['mae'] is not None
                                               and raw_best is not None
                                               and result['metrics']['mae'] < raw_best)
    spec = features.task_spec(column)
    return dict(column=column, base=spec['base'], horizon=spec['hours'],
                min_train_days=min_train_days,
                best_baseline=best['method'] if best else None,
                best_baseline_mae=raw_best, results=results)


def selection_split(results, share=0.7):
    """按时间把逐点预测切成选择集与报告集。

    用同一批数据既挑模型又报指标会偏乐观。这里拿前 70% 的测试日挑方法，
    后 30% 只用来报告——挑选和汇报分开，结论才有说服力。
    """
    days = sorted({record['day_index'] for result in results
                   for record in result['predictions']})
    if len(days) < 2:
        raise ValueError('测试日不足，无法切分选择集与报告集')
    cut = days[max(1, int(len(days) * share)) - 1] + 1
    selection, report = {}, {}
    for result in results:
        early = [record for record in result['predictions'] if record['day_index'] < cut]
        late = [record for record in result['predictions'] if record['day_index'] >= cut]
        selection[result['method']] = metrics([r['actual'] for r in early],
                                              [r['predicted'] for r in early])
        report[result['method']] = metrics([r['actual'] for r in late],
                                           [r['predicted'] for r in late])
    return dict(cut_day=cut, selection=selection, report=report,
                selection_days=sum(1 for day in days if day < cut),
                report_days=sum(1 for day in days if day >= cut))


def pick(split_metrics, candidates, key='mae'):
    """在候选中挑指标最好的方法；都没有有效指标时返回 None。"""
    scored = [(name, split_metrics[name][key]) for name in candidates
              if split_metrics.get(name, {}).get(key) is not None]
    return min(scored, key=lambda item: item[1])[0] if scored else None


def build_report(evaluation, path, note=''):
    """把基线对比写成 Markdown 报告。"""
    unit = '次' if evaluation['base'] == 'sessions' else 'kWh'
    horizon = evaluation['horizon']
    lines = ['# 预测评估：基线与滚动验证', '',
             f'目标：未来 {horizon} 小时的{unit}负荷（`{evaluation["column"]}`）；'
             f'协议：扩展窗口滚动前进，'
             f'最少训练 {evaluation["min_train_days"]} 天，逐日推进。', '',
             f'最佳基线：`{evaluation["best_baseline"]}`'
             f'（MAE {evaluation["best_baseline_mae"]}）。', '',
             '| 方法 | 样本 | MAE | RMSE | MAPE | 实际均值 | 相对误差 | 相对最佳基线 |',
             '|---|---:|---:|---:|---:|---:|---:|---:|']
    for result in evaluation['results']:
        stats = result['metrics']
        lines.append(f"| {result['label']} | {stats['samples']} | {stats['mae']} | {stats['rmse']} "
                     f"| {_percent(stats.get('mape'))} | {stats.get('mean_actual')} "
                     f"| {_percent(stats.get('relative_error'))} "
                     f"| {_percent(result.get('skill_vs_best_baseline'))} |")
    lines += ['', '按小时分层（MAE）：', '',
              '| 小时 | ' + ' | '.join(str(h) for h in range(24)) + ' |',
              '|---|' + '---:|' * 24]
    best = next((r for r in evaluation['results']
                 if r['method'] == evaluation['best_baseline']), None)
    if best:
        lines.append('| MAE | ' + ' | '.join(
            str(best['by_hour'].get(hour, {}).get('mae', '-')) for hour in range(24)) + ' |')
    if best and best['by_day_type']:
        lines += ['', '按日型分层（MAE）：', '',
                  '| 日型 | 样本 | MAE | RMSE |', '|---|---:|---:|---:|']
        for label, stats in best['by_day_type'].items():
            lines.append(f"| {label} | {stats['samples']} | {stats['mae']} | {stats['rmse']} |")
    if note:
        lines += ['', '## 说明', '', note]
    Path(path).write_text('\n'.join(lines) + '\n', encoding='utf-8')


def _percent(value):
    return '-' if value is None else f'{value * 100:.1f}%'


def main(argv=None):
    parser = argparse.ArgumentParser(description='基线与滚动评估：产出指标与对比报告')
    parser.add_argument('--db', required=True, help='数仓 SQLite 路径')
    parser.add_argument('--base', default='sessions', choices=['sessions', 'kwh'],
                        help='预测目标：会话数或电量')
    parser.add_argument('--horizon', type=int, default=1, choices=[1, 6, 24],
                        help='预测跨度：未来 1 / 6 / 24 小时')
    parser.add_argument('--min-train-days', type=int, default=28, help='滚动评估的最少训练天数')
    parser.add_argument('--report', help='Markdown 报告输出路径')
    parser.add_argument('--json', dest='json_out', help='指标 JSON 输出路径')
    args = parser.parse_args(argv)

    panel = timeline.build(args.db)
    rows = features.attach_lags(panel)
    column = features.add_horizon(rows, args.horizon, args.base)
    evaluation = compare(rows, column=column, min_train_days=args.min_train_days)
    summary = timeline.summary(panel)
    note = (f"时间轴：跨度 {summary['span_days']} 天，其中 {summary['observed_days']} 天有采集、"
            f"{summary['missing_days']} 天空白（掩码 0，不参与训练也不当零）。")
    if args.report:
        build_report(evaluation, args.report, note=note)
    payload = dict(timeline=summary, evaluation=dict(
        column=evaluation['column'], base=evaluation['base'], horizon=evaluation['horizon'],
        best_baseline=evaluation['best_baseline'],
        best_baseline_mae=evaluation['best_baseline_mae'],
        results=[dict(method=r['method'], label=r['label'], metrics=r['metrics'],
                      fallbacks=r['fallbacks'],
                      skill_vs_best_baseline=r['skill_vs_best_baseline']) for r in evaluation['results']]))
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n',
                                       encoding='utf-8')
    print(json.dumps(payload['evaluation'], ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
