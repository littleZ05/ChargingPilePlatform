#!/usr/bin/env python3
"""第二阶段负荷预测：多跨度岭回归训练与评估。

对齐课程要求"预测未来 1 小时、6 小时、24 小时"：会话数 / 电量 × 1/6/24 小时
= 6 个任务，每个任务在三条基线与两种岭回归之间选出方法。

挑选与汇报分开：用前 70% 的测试日选方法，后 30% 只用来报告指标，
避免"用同一批数据既挑模型又报指标"的乐观偏差。

产物：model_v2.json（模型系数与指标）+ 预测评估_v2.md（模型卡）。
旧版 train.py / forecast.py 保留，等 P4 把服务与大屏切过来后再移除。
"""
import argparse
import json
import sys
from datetime import datetime
from pathlib import Path

import baselines
import evaluate
import features
import models
import sessions
import timeline

BASES = ['sessions', 'kwh']
HORIZONS = [1, 6, 24]
SELECTION_SHARE = 0.7
ADOPT_MARGIN = 0.10     # 模型在选择集上至少要领先最佳基线 10% 才被采纳


def train_task(rows, base, horizon, min_train_days=28, alpha=models.DEFAULT_ALPHA,
               share=SELECTION_SHARE):
    """训练单个任务：滚动评估 → 选择集选方法 → 报告集报指标 → 全量拟合最终模型。"""
    column = features.add_horizon(rows, horizon, base)
    spec = features.task_spec(column)
    methods = baselines.METHODS + models.METHODS
    results = [evaluate.walk_forward(rows, method, column=column,
                                     min_train_days=min_train_days, alpha=alpha)
               for method in methods]
    split = evaluate.selection_split(results, share)

    baseline_choice = evaluate.pick(split['selection'], baselines.METHODS)
    best_model = evaluate.pick(split['selection'], models.METHODS)
    baseline_mae = split['selection'][baseline_choice]['mae']
    model_mae = split['selection'][best_model]['mae'] if best_model else None
    adopted = bool(best_model and baseline_mae is not None and model_mae is not None
                   and model_mae <= baseline_mae * (1 - ADOPT_MARGIN))
    chosen = best_model if adopted else baseline_choice
    chosen_report = split['report'][chosen]
    baseline_report = split['report'][baseline_choice]
    skill = evaluate.skill_score(chosen_report['mae'], baseline_report['mae'])

    final = models.fit([row for row in rows if row.get(column) is not None],
                       chosen if chosen in models.METHODS else 'ridge', spec, alpha=alpha)
    return dict(
        column=column, base=base, horizon=horizon, chosen=chosen, alpha=alpha,
        baseline_choice=baseline_choice, skill_vs_best_baseline=skill,
        model_adopted=adopted, model_candidate=best_model,
        selection_advantage=(round(1 - model_mae / baseline_mae, 4)
                             if adopted and baseline_mae else None),
        better_than_best_baseline=bool(skill and skill > 0),
        selection=dict(cut_day=split['cut_day'], days=split['selection_days'],
                       metrics=split['selection']),
        report=dict(days=split['report_days'], metrics=split['report'],
                    chosen=chosen_report, best_baseline=baseline_report),
        all_metrics={result['method']: result['metrics'] for result in results},
        labels={result['method']: result['label'] for result in results},
        by_hour=next(r for r in results if r['method'] == chosen)['by_hour'],
        by_day_type=next(r for r in results if r['method'] == chosen)['by_day_type'],
        model=final, top_weights=models.top_weights(final),
        training=dict(rows=final['training_rows'],
                      folds=next(r for r in results if r['method'] == chosen)['folds']))


def train(database, min_train_days=28, alpha=models.DEFAULT_ALPHA, share=SELECTION_SHARE,
          session_share=sessions.FIRST_SHARE, with_sessions=True):
    panel = timeline.build(database)
    rows = features.attach_lags(panel)
    tasks = []
    for base in BASES:
        for horizon in HORIZONS:
            tasks.append(train_task(rows, base, horizon, min_train_days, alpha, share))
    model = dict(
        version=datetime.now().strftime('%Y%m%d%H%M'),
        generated_at=datetime.now().astimezone().isoformat(timespec='seconds'),
        protocol=dict(
            evaluation='扩展窗口滚动前进，用第 0..t-1 天拟合，预测第 t 天',
            selection=f'前 {int(share * 100)}% 的测试日用于挑方法，其余只用于报告',
            min_train_days=min_train_days),
        timeline=timeline.summary(panel),
        note=('源年份字段不可信，时间轴由 created_raw 的月日构造（与 weekday 逐行自洽），'
              '对外只用相对天数描述，不声称真实日历日期。'),
        tasks=tasks)
    if with_sessions:
        model['sessions'] = sessions.train(database, session_share, alpha)
    return model


def build_report(model, path):
    protocol = model['protocol']
    lines = ['# 第二阶段预测评估（v2）', '',
             f"模型版本 `{model['version']}`。", '',
             f"评估协议：{protocol['evaluation']}；{protocol['selection']}；"
             f"最少训练 {protocol['min_train_days']} 天。", '',
             f"时间轴：跨度 {model['timeline']['span_days']} 天，其中 "
             f"{model['timeline']['observed_days']} 天有采集、"
             f"{model['timeline']['missing_days']} 天空白（掩码排除，不参与训练也不当零）。", '',
             '## 一、选定方法在报告集上的表现', '',
             '| 任务 | 是否采纳模型 | 上线的预测器 | MAE | RMSE | 相对误差 | 对照基线 | 其 MAE | 相对基线 |',
             '|---|---|---|---:|---:|---:|---|---:|---:|']
    for task in model['tasks']:
        report = task['report']
        lines.append(
            f"| `{task['column']}` | {'是' if task['model_adopted'] else '否'} "
            f"| {task['labels'][task['chosen']]} "
            f"| {report['chosen']['mae']} | {report['chosen']['rmse']} "
            f"| {_percent(report['chosen'].get('relative_error'))} "
            f"| {task['labels'][task['baseline_choice']]} | {report['best_baseline']['mae']} "
            f"| {_percent(task['skill_vs_best_baseline'])} |")
    lines += ['', f"> 采纳规则：模型在选择集（前 {int(SELECTION_SHARE * 100)}% 测试日）上"
              f"至少要领先最佳基线 {int(ADOPT_MARGIN * 100)}% 才上线，否则直接上线基线。"
              '门槛定在 10% 是为了过滤"小而不稳"的优势：实测 24 小时跨度在选择集上领先 16–17%，'
              '6 小时跨度只领先 2.5–6.3%。表格指标只统计后 30% 的测试日。',
              '', '## 二、全部候选方法（全时段滚动指标）', '',
              '| 任务 | 方法 | MAE | RMSE | 相对误差 |', '|---|---|---:|---:|---:|']
    for task in model['tasks']:
        for method, stats in task['all_metrics'].items():
            lines.append(f"| `{task['column']}` | {task['labels'][method]} | {stats['mae']} "
                         f"| {stats['rmse']} | {_percent(stats.get('relative_error'))} |")
    lines += ['', '## 三、模型可解释性（权重最大的特征）', '']
    for task in model['tasks']:
        lines += [f"### {task['column']}（选定 {task['labels'][task['chosen']]}）", '',
                  '| 特征 | 权重（标准化后） |', '|---|---:|']
        for item in task['top_weights']:
            lines.append(f"| `{item['feature']}` | {item['weight']} |")
        lines.append('')
    lines.append('')
    if model.get('sessions'):
        lines.append(sessions.build_report(model['sessions']))
        lines.append('')
    lines += ['## 五、限制', '',
              f"- {model['note']}",
              '- 缺测日在训练与评估中一律排除，不做零值填充。',
              '- 方法选择与指标报告分属不同时段，但同属一份数据，仍属课程级评估。',
              '- 模型用于运营参考与低拥堵推荐，不构成收费或产能承诺。',
              '- 未使用深度学习（LSTM 等）：样本量与环境均不支持，升级路径见架构文档。']
    Path(path).write_text('\n'.join(lines) + '\n', encoding='utf-8')


def _percent(value):
    return '-' if value is None else f'{value * 100:.1f}%'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', required=True, help='数仓 SQLite 路径')
    parser.add_argument('--out', required=True, help='model_v2.json 输出路径')
    parser.add_argument('--report', help='Markdown 评估报告输出路径')
    parser.add_argument('--min-train-days', type=int, default=28)
    parser.add_argument('--alpha', type=float, default=models.DEFAULT_ALPHA)
    parser.add_argument('--no-sessions', dest='with_sessions', action='store_false',
                        help='跳过单次充电分位数（时长/电量）部分')
    args = parser.parse_args(argv)

    model = train(args.db, args.min_train_days, args.alpha,
                  with_sessions=args.with_sessions)
    Path(args.out).write_text(json.dumps(model, ensure_ascii=False, indent=2) + '\n',
                              encoding='utf-8')
    if args.report:
        build_report(model, args.report)
    print(json.dumps({'version': model['version'], 'tasks': [
        {'column': task['column'], 'chosen': task['chosen'],
         'model_adopted': task['model_adopted'], 'model_candidate': task['model_candidate'],
         'chosen_mae': task['report']['chosen']['mae'],
         'baseline': task['baseline_choice'],
         'baseline_mae': task['report']['best_baseline']['mae'],
         'skill': task['skill_vs_best_baseline']} for task in model['tasks']],
        'session_tasks': [
            {'target': task['target'], 'tau': task['tau'], 'chosen': task['chosen'],
             'model_adopted': task['model_adopted'], 'pinball': task['pinball'],
             'baseline': task['baseline_choice'], 'baseline_pinball': task['baseline_pinball'],
             'coverage': task['coverage'],
             'calibration_gap': task['calibration_gap'],
             'skill': task['skill']} for task in model.get('sessions', {}).get('tasks', [])]},
        ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
