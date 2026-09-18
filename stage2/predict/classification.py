#!/usr/bin/env python3
"""会话级分类：这次充电会不会长时间占用车位（CART 分类树）。

负荷预测回答"未来某个小时有多少单"，分位数回答"这次大概充多久"，
但运营真正想提前知道的是一句是非题：**这次会不会长时间占用车位**，
要不要提前提醒或引导。这就是分类树在这里的位置。

标签：训练窗口内单次时长的 P75 及以上记为正类（长时长占用）。
阈值每折都只在它自己的训练窗口里算，测试块不参与，避免把答案漏给模型。

特征只用扫码那一刻已知的信息：设施类型、峰谷时段、平台、开始时段（4 小时一档）、
是否周末、是否管理用车，以及站点历史时长中位数（向全局收缩）。
不使用本次会话的电量、费用、结束时间等结果字段。

评估与其它预测任务同一套协议：前 70% 的天留作测试期，之后每 30 天一个测试块，
每块都用它之前最近 90 天重新拟合模型与基线。指标用 ROC-AUC、平均精度（PR-AUC，
不平衡数据下比准确率更能说明问题）、Brier 分数，以及按 F1 选阈值后的
精确率/召回率；概率阈值只在训练窗口内选，不看测试块。

采纳规则同样要先证明自己，但主指标按这个任务的决策来定：运营问的是"这次要不要
提前提示"，是带阈值的判断，因此以 F1（阈值在每个训练窗口内按 F1 选）为主指标，
要求模型领先最佳基线 10%；同时用平均精度作护栏——排序质量不得低于最佳基线，
防止靠调阈值换指标。两条都满足才上线模型，否则如实上线
"按设施类型 × 峰谷时段查表"的经验规则。
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import sessions  # noqa: E402
import tree  # noqa: E402

LABEL_NAME = '长时长占用'
POSITIVE_LABEL = '长时长占用'
NEGATIVE_LABEL = '正常周转'
DEFAULT_QUANTILE = 0.75
ADOPT_MARGIN = 0.10
MIN_TRAIN_SESSIONS = 120
DEFAULT_THRESHOLD = 0.5
SMOOTHING = 10.0
VALIDATION_SHARE = 0.2
PARAM_GRID = ((2, 20), (2, 40), (3, 40), (3, 80), (4, 40), (4, 80))
PRUNING_CANDIDATES = 3      # 每条剪枝路径最多评估 3 个 alpha，控制训练时间
METHODS = ['global_rate', 'facility_rate', 'facility_period_rate', 'cart']
BASELINES = METHODS[:-1]
METHOD_LABELS = {
    'global_rate': '全局正类比例（常数打分）',
    'facility_rate': '按设施类型的历史比例',
    'facility_period_rate': '按设施类型 × 峰谷时段的历史比例',
    'cart': 'CART 分类树（Gini + 代价复杂度剪枝）',
}


def quantile(values, fraction):
    return sessions.quantile(values, fraction)


def roc_auc(actual, scores):
    """秩和法算 AUC，同分取平均秩；只有单一类别时返回 None 而不是编一个数。"""
    pairs = list(zip(actual, scores))
    positives = sum(1 for label, _ in pairs if label == 1)
    negatives = len(pairs) - positives
    if positives == 0 or negatives == 0:
        return None
    ordered = sorted(range(len(pairs)), key=lambda index: pairs[index][1])
    ranks = [0.0] * len(pairs)
    index = 0
    while index < len(ordered):
        stop = index
        while stop + 1 < len(ordered) and pairs[ordered[stop + 1]][1] == pairs[ordered[index]][1]:
            stop += 1
        average = (index + stop) / 2 + 1
        for position in range(index, stop + 1):
            ranks[ordered[position]] = average
        index = stop + 1
    rank_sum = sum(ranks[i] for i, (label, _) in enumerate(pairs) if label == 1)
    return (rank_sum - positives * (positives + 1) / 2) / (positives * negatives)


def average_precision(actual, scores):
    """PR 曲线下面积（平均精度）：不平衡场景下比 AUC 更贴近"抓得到几个正类"。"""
    pairs = sorted(zip(scores, actual), key=lambda item: -item[0])
    positives = sum(actual)
    if not positives:
        return None
    hits = 0
    total = 0.0
    previous_recall = 0.0
    for index, (_, label) in enumerate(pairs, start=1):
        if label != 1:
            continue
        hits += 1
        precision = hits / index
        recall = hits / positives
        total += precision * (recall - previous_recall)
        previous_recall = recall
    return total


def brier(actual, scores):
    if not actual:
        return None
    return sum((score - label) ** 2 for score, label in zip(scores, actual)) / len(actual)


def confusion(actual, scores, threshold=DEFAULT_THRESHOLD):
    tp = fp = tn = fn = 0
    for label, score in zip(actual, scores):
        predicted = 1 if score >= threshold else 0
        if label == 1 and predicted == 1:
            tp += 1
        elif label == 1:
            fn += 1
        elif predicted == 1:
            fp += 1
        else:
            tn += 1
    return tp, fp, tn, fn


def metrics(actual, scores, threshold=DEFAULT_THRESHOLD):
    tp, fp, tn, fn = confusion(actual, scores, threshold)
    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = tp / (tp + fn) if tp + fn else 0.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return dict(samples=len(actual),
                positives=int(sum(actual)),
                base_rate=round(sum(actual) / len(actual), 4) if actual else None,
                accuracy=round((tp + tn) / len(actual), 4) if actual else None,
                precision=round(precision, 4), recall=round(recall, 4), f1=round(f1, 4),
                roc_auc=_round(roc_auc(actual, scores)),
                average_precision=_round(average_precision(actual, scores)),
                brier=_round(brier(actual, scores)),
                threshold=round(threshold, 4),
                confusion=dict(tp=tp, fp=fp, tn=tn, fn=fn))


def pooled_metrics(actual, scores, thresholds):
    """每个测试块用各自训练窗口选出的阈值，再汇总决策指标。

    直接拿一个全局阈值套所有块是不公平的：各块的正类比例不同，
    基线查表分数也在漂移。排序指标（AUC / 平均精度 / Brier）用汇总后的分数算，
    决策指标（精确率 / 召回率 / F1）按各自的阈值累计混淆矩阵后算。
    """
    tp = fp = tn = fn = 0
    for label, score, threshold in zip(actual, scores, thresholds):
        predicted = 1 if score >= threshold else 0
        if label == 1 and predicted == 1:
            tp += 1
        elif label == 1:
            fn += 1
        elif predicted == 1:
            fp += 1
        else:
            tn += 1
    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = tp / (tp + fn) if tp + fn else 0.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return dict(samples=len(actual),
                positives=int(sum(actual)),
                base_rate=round(sum(actual) / len(actual), 4) if actual else None,
                accuracy=round((tp + tn) / len(actual), 4) if actual else None,
                precision=round(precision, 4), recall=round(recall, 4), f1=round(f1, 4),
                roc_auc=_round(roc_auc(actual, scores)),
                average_precision=_round(average_precision(actual, scores)),
                brier=_round(brier(actual, scores)),
                threshold=round(sum(thresholds) / len(thresholds), 4) if thresholds else None,
                confusion=dict(tp=tp, fp=fp, tn=tn, fn=fn))


def _round(value, digits=4):
    return None if value is None else round(value, digits)


def best_threshold(actual, scores):
    """在训练窗口内按 F1 选概率阈值；并列时取更保守（更高）的那个。"""
    candidates = sorted({round(score, 4) for score in scores})
    if not candidates:
        return DEFAULT_THRESHOLD
    best, best_f1 = DEFAULT_THRESHOLD, -1.0
    for threshold in candidates:
        f1 = metrics(actual, scores, threshold)['f1']
        if f1 > best_f1 + 1e-12 or (abs(f1 - best_f1) <= 1e-12 and threshold > best):
            best, best_f1 = threshold, f1
    return best


def label_rows(rows, threshold):
    """按时长阈值打标签；阈值当天由训练窗口给出，测试块不参与。"""
    return [1 if float(row['duration_hours']) >= threshold else 0 for row in rows]


def design(rows, categories):
    """复用分位数模型的特征展开，附加站点历史时长中位数（扫码时已知）。"""
    statistics = sessions.station_statistics(rows, 'duration_hours')
    enriched = sessions.attach_station_features(rows, statistics)
    names, matrix = None, []
    for row in enriched:
        names, values = sessions.design_row(row, categories, with_station=True)
        matrix.append(values)
    return names or [], matrix, statistics


def design_with(rows, categories, statistics):
    enriched = sessions.attach_station_features(rows, statistics)
    names, matrix = None, []
    for row in enriched:
        names, values = sessions.design_row(row, categories, with_station=True)
        matrix.append(values)
    return names or [], matrix


def fit_baseline(rows, labels, method):
    base = sum(labels) / len(labels) if labels else 0.0
    table = {}
    if method in ('facility_rate', 'facility_period_rate'):
        keys = (('facility_label',) if method == 'facility_rate'
                else ('facility_label', 'time_period'))
        totals, counts = {}, {}
        for row, label in zip(rows, labels):
            key = tuple(row[field] for field in keys)
            totals[key] = totals.get(key, 0) + label
            counts[key] = counts.get(key, 0) + 1
        for key, count in counts.items():
            table[key] = (totals[key] + base * SMOOTHING) / (count + SMOOTHING)
    return dict(method=method, base_rate=base, table=table,
                keys=(('facility_label',) if method == 'facility_rate'
                      else ('facility_label', 'time_period')))


def predict_baseline(fitted, rows):
    if fitted['method'] == 'global_rate':
        return [fitted['base_rate']] * len(rows)
    keys = fitted['keys']
    return [fitted['table'].get(tuple(row[field] for field in keys), fitted['base_rate'])
            for row in rows]


def _day_split(rows, share):
    """按天切内部验证集：后 share 的天留作验证，避免同一批样本既训练又挑参数。"""
    days = sorted({row['day_index'] for row in rows})
    if len(days) < 5:
        return None
    cut = days[max(1, min(len(days) - 1, int(len(days) * (1 - share))))]
    train = [row for row in rows if row['day_index'] < cut]
    valid = [row for row in rows if row['day_index'] >= cut]
    if len(train) < MIN_TRAIN_SESSIONS or not valid:
        return None
    return train, valid


def choose_params(rows, threshold, share=VALIDATION_SHARE):
    """在训练窗口内部挑树参数与剪枝强度；验证集留出，不碰测试块。"""
    split = _day_split(rows, share)
    if split is None:
        return dict(max_depth=3, min_samples_leaf=40, alpha=0.0, scored_on='窗口内样本不足，取默认参数')
    train, valid = split
    categories = sessions.categories_from(train)
    train_labels = label_rows(train, threshold)
    valid_labels = label_rows(valid, threshold)
    _, train_matrix, statistics = design(train, categories)
    _, valid_matrix = design_with(valid, categories, statistics)
    if not train_matrix or not valid_matrix:
        return dict(max_depth=3, min_samples_leaf=40, alpha=0.0, scored_on='特征为空，取默认参数')
    names = tree_names(train, categories)
    best, best_score = None, None
    for depth, leaf in PARAM_GRID:
        grown = tree.fit(train_matrix, train_labels, names, max_depth=depth, min_samples_leaf=leaf)
        # 剪枝只是把已有的子树折叠掉，不需要重新拟合：一条剪枝路径上只做树操作。
        alphas = [0.0] + tree.pruning_path(grown['tree'])[:PRUNING_CANDIDATES]
        for alpha in alphas:
            candidate = grown if alpha == 0 else dict(grown, tree=tree.prune(grown['tree'], alpha),
                                                      alpha=alpha)
            scores = [tree.predict_proba(candidate, row) for row in valid_matrix]
            ap = average_precision(valid_labels, scores)
            if ap is None:
                continue
            leaves = tree.leaves(candidate)
            key = (round(ap, 6), -leaves)
            if best_score is None or key > best_score:
                best_score = key
                best = dict(max_depth=depth, min_samples_leaf=leaf, alpha=alpha,
                            validation_average_precision=round(ap, 4), leaves=leaves,
                            scored_on=f'窗口内留出 {len(valid)} 条会话做验证')
    return best or dict(max_depth=3, min_samples_leaf=40, alpha=0.0, scored_on='验证集无正负样本，取默认参数')


def tree_names(rows, categories):
    """特征名与 design_row 的顺序一致，单独抽出来避免重复展开矩阵。"""
    return sessions.design_row({'facility_label': categories['facility_label'][0],
                                'time_period': categories['time_period'][0],
                                'platform': categories['platform'][0], 'hour': 0,
                                'is_weekend': 0, 'manager_vehicle': 0,
                                'station_median': 0.0, 'station_count': 0.0},
                               categories, with_station=True)[0]


def evaluate_rolling(rows, window_days=sessions.WINDOW_DAYS, block_days=sessions.BLOCK_DAYS,
                     quantile_level=DEFAULT_QUANTILE, share=sessions.FIRST_SHARE):
    """滚动重训评估：每个测试块都用之前的窗口重新拟合全部候选方法。"""
    blocks = sessions.rolling_blocks(rows, share, block_days)
    pooled = {method: {'actual': [], 'scores': [], 'thresholds': []} for method in METHODS}
    per_block, sizes, thresholds = [], [], []
    for start, end in blocks:
        train = sessions.training_window(rows, start, window_days)
        test = [row for row in rows if start <= row['day_index'] <= end]
        if len(train) < MIN_TRAIN_SESSIONS or not test:
            continue
        threshold = quantile([float(row['duration_hours']) for row in train], quantile_level)
        thresholds.append(round(threshold, 4))
        train_labels = label_rows(train, threshold)
        test_labels = label_rows(test, threshold)
        categories = sessions.categories_from(train)
        names, matrix, statistics = design(train, categories)
        _, test_matrix = design_with(test, categories, statistics)
        params = choose_params(train, threshold)
        model = tree.fit(matrix, train_labels, names, max_depth=params['max_depth'],
                         min_samples_leaf=params['min_samples_leaf'], alpha=params['alpha'])
        scores_by_method = {'cart': [tree.predict_proba(model, row) for row in test_matrix]}
        for method in BASELINES:
            scores_by_method[method] = predict_baseline(fit_baseline(train, train_labels, method),
                                                        test)
        # 概率阈值只在训练窗口内选：模型用训练矩阵上的分数，基线用它自己的训练分数。
        train_scores = {'cart': [tree.predict_proba(model, row) for row in matrix]}
        for method in BASELINES:
            train_scores[method] = predict_baseline(fit_baseline(train, train_labels, method), train)
        block_metrics = {}
        for method, scores in scores_by_method.items():
            threshold_for_method = best_threshold(train_labels, train_scores[method])
            pooled[method]['actual'].extend(test_labels)
            pooled[method]['scores'].extend(scores)
            pooled[method]['thresholds'].extend([threshold_for_method] * len(test_labels))
            block_metrics[method] = {
                'average_precision': _round(average_precision(test_labels, scores)),
                'roc_auc': _round(roc_auc(test_labels, scores)),
                'f1': metrics(test_labels, scores, threshold_for_method)['f1'],
                'threshold': round(threshold_for_method, 4)}
        per_block.append(dict(start_day=start, end_day=end, sessions=len(test),
                              threshold=round(threshold, 4), params=params,
                              metrics=block_metrics))
        sizes.append(dict(start_day=start, end_day=end, sessions=len(test),
                          training_rows=len(train), threshold=round(threshold, 4)))
    summary = {}
    for method in METHODS:
        entry = pooled[method]
        summary[method] = pooled_metrics(entry['actual'], entry['scores'], entry['thresholds'])
    return dict(metrics=summary, blocks=sizes, per_block=per_block, thresholds=thresholds,
                test_sessions=len(pooled['cart']['actual']),
                positive_rate=round(sum(pooled['cart']['actual']) / len(pooled['cart']['actual']), 4)
                if pooled['cart']['actual'] else None)


def select(evaluation):
    """挑方法：F1 领先最佳基线 10%，且平均精度不低于最佳基线，才上线模型。"""
    entries = evaluation['metrics']
    baseline_choice = min(BASELINES,
                          key=lambda name: (-(entries[name]['f1'] or 0), name))
    baseline_ap = entries[baseline_choice]['average_precision']
    model_ap = entries['cart']['average_precision']
    baseline_f1 = entries[baseline_choice]['f1']
    model_f1 = entries['cart']['f1']
    beats_on_decisions = bool(baseline_f1 and model_f1 is not None
                              and model_f1 >= baseline_f1 * (1 + ADOPT_MARGIN))
    ranking_not_worse = bool(model_ap is not None and baseline_ap is not None
                             and model_ap >= baseline_ap)
    adopted = beats_on_decisions and ranking_not_worse
    wins = total = 0
    for block in evaluation['per_block']:
        stats = block['metrics']
        if stats['cart']['f1'] is None or stats[baseline_choice]['f1'] is None:
            continue
        total += 1
        if stats['cart']['f1'] > stats[baseline_choice]['f1']:
            wins += 1
    return dict(chosen='cart' if adopted else baseline_choice, model_adopted=adopted,
                baseline_choice=baseline_choice,
                beats_on_decisions=beats_on_decisions, ranking_not_worse=ranking_not_worse,
                f1_advantage=round(model_f1 / baseline_f1 - 1, 4) if baseline_f1 else None,
                skill=round(1 - baseline_ap / model_ap, 4) if adopted and model_ap else None,
                advantage=round(model_f1 / baseline_f1 - 1, 4) if baseline_f1 else None,
                block_wins=wins, block_total=total)


def calibration(actual, scores, bins=5):
    """把预测概率分箱，检查"说 80% 的时候是不是真的接近 80%"。"""
    if not actual:
        return []
    edges = [index / bins for index in range(bins + 1)]
    out = []
    for index in range(bins):
        low, high = edges[index], edges[index + 1]
        picked = [(score, label) for score, label in zip(scores, actual)
                  if (low < score <= high) or (index == 0 and score <= low)]
        if not picked:
            continue
        out.append(dict(range=f'({low:.1f}, {high:.1f}]', samples=len(picked),
                        mean_score=round(sum(score for score, _ in picked) / len(picked), 4),
                        observed_rate=round(sum(label for _, label in picked) / len(picked), 4)))
    return out


def train(database, window_days=sessions.WINDOW_DAYS, block_days=sessions.BLOCK_DAYS,
          quantile_level=DEFAULT_QUANTILE, share=sessions.FIRST_SHARE):
    """完整流程：滚动评估 → 挑方法 → 用全量数据拟合部署树 → 规则与校准表。"""
    rows, _ = sessions.load_sessions(database)
    if len(rows) < MIN_TRAIN_SESSIONS:
        raise ValueError(f'可用会话不足 {MIN_TRAIN_SESSIONS} 条，无法训练分类树')
    evaluation = evaluate_rolling(rows, window_days, block_days, quantile_level, share)
    choice = select(evaluation)

    threshold = quantile([float(row['duration_hours']) for row in rows], quantile_level)
    labels = label_rows(rows, threshold)
    categories = sessions.categories_from(rows)
    names, matrix, statistics = design(rows, categories)
    params = choose_params(rows, threshold)
    model = tree.fit(matrix, labels, names, max_depth=params['max_depth'],
                     min_samples_leaf=params['min_samples_leaf'], alpha=params['alpha'])
    scores = [tree.predict_proba(model, row) for row in matrix]
    decision = best_threshold(labels, scores)
    top_rules = tree.rules(model)
    for rule in top_rules:
        rule['translation'] = _translate(rule['conditions'])
    # 上线方法的查表、站点统计与特征枚举都要随模型一起存下来：
    # 服务端只读模型 JSON，不再回查明细表。
    baseline = fit_baseline(rows, labels, choice['baseline_choice'])
    baseline_table = {'|'.join(str(part) for part in key): value
                      for key, value in baseline['table'].items()}
    baseline_counts = {}
    for key, count in _segment_counts(rows, baseline['keys']).items():
        baseline_counts['|'.join(str(part) for part in key)] = count
    return dict(
        label=LABEL_NAME, positive=POSITIVE_LABEL, negative=NEGATIVE_LABEL,
        target='duration_hours', quantile=quantile_level,
        threshold_hours=round(threshold, 4), threshold_source=_threshold_note(rows, quantile_level),
        chosen=choice['chosen'], chosen_label=METHOD_LABELS[choice['chosen']],
        model_adopted=choice['model_adopted'], baseline_choice=choice['baseline_choice'],
        baseline_label=METHOD_LABELS[choice['baseline_choice']],
        categories=categories, statistics=statistics,
        advantage=choice['advantage'], skill=choice['skill'],
        f1_advantage=choice['f1_advantage'],
        beats_on_decisions=choice['beats_on_decisions'],
        ranking_not_worse=choice['ranking_not_worse'],
        block_wins=choice['block_wins'], block_total=choice['block_total'],
        labels=METHOD_LABELS, baselines=BASELINES,
        metrics=evaluation['metrics'], blocks=evaluation['blocks'],
        test_sessions=evaluation['test_sessions'], fold_thresholds=evaluation['thresholds'],
        fold_positive_rate=evaluation['positive_rate'],
        deployment=dict(params=params, features=names, tree=model['tree'],
                        categories=categories, statistics=statistics,
                        baseline=dict(method=baseline['method'],
                                      keys=list(baseline['keys']),
                                      base_rate=baseline['base_rate'],
                                      table=baseline_table, counts=baseline_counts),
                        decision_threshold=round(decision, 4),
                        train_sessions=len(rows),
                        train_positive_rate=round(sum(labels) / len(labels), 4),
                        in_sample_metrics=metrics(labels, scores, decision),
                        calibration=calibration(labels, scores),
                        rules=top_rules, importance=tree.feature_importance(model)[:8],
                        leaves=tree.leaves(model)),
        limits=['标签是训练窗口内的时长 P75，阈值随数据更新，不是业务固定标准；',
                '概率来自叶子上的历史比例，叶子样本少时分辨率有限，只作运营提示；',
                '不使用本次会话的结果字段，因此不能解释"这一单为什么特别长"；',
                '结论用于运营参考，不作为对用户的承诺或处罚依据。'])


def _threshold_note(rows, quantile_level):
    days = len({row['day_index'] for row in rows})
    return (f'全部可用会话（{len(rows)} 条、{days} 天）单次时长的 '
            f'P{int(quantile_level * 100)} 分位；评估时每折只用各自窗口内的分位')


def _segment_counts(rows, keys):
    counts = {}
    for row in rows:
        key = tuple(row[field] for field in keys)
        counts[key] = counts.get(key, 0) + 1
    return counts


FEATURE_TEXT = {
    'facility=': '设施类型为',
    'period=': '峰谷时段为',
    'platform=': '平台为',
    'hour_bucket=': '开始时段属于',
    'is_weekend': '周末',
    'manager_vehicle': '管理用车',
    'station_median': '站点历史中位时长',
    'station_count': '站点历史样本量',
}


def _translate(conditions):
    """把机器规则翻成人话，方便直接写进汇报与接口说明。"""
    parts = []
    for condition in conditions:
        feature, _, value = condition.partition((' <= ' if ' <= ' in condition else ' > '))
        comparison = '<=' if ' <= ' in condition else '>'
        text = feature
        for prefix, phrase in FEATURE_TEXT.items():
            if feature.startswith(prefix):
                text = phrase + feature[len(prefix):]
                break
        parts.append(f'{text} {comparison} {value}')
    return '、'.join(parts)


def build_report(section, path=None):
    """把分类结果写成模型卡的一节。"""
    lines = ['', '## 六、会话分类树（长时长占用预警）', '',
             f"标签：单次时长 ≥ {section['threshold_hours']} 小时的会话记为「{section['positive']}」"
             f"（{section['threshold_source']}）。",
             f"评估协议同负荷预测：前 70% 的天留作测试期，之后每 {sessions.BLOCK_DAYS} 天一个测试块，"
             f"每块用之前最近 {sessions.WINDOW_DAYS} 天重训；概率阈值只在训练窗口内选。",
             f"参与评估的测试会话 {section['test_sessions']} 条，正类比例 {section['fold_positive_rate']}。",
             '', '| 方法 | 平均精度 (PR-AUC) | ROC-AUC | 准确率 | 精确率 | 召回率 | F1 | Brier |',
             '|---|---:|---:|---:|---:|---:|---:|---:|']
    for method in METHODS:
        stats = section['metrics'][method]
        lines.append(f"| {METHOD_LABELS[method]} | {_fmt(stats['average_precision'])} | "
                     f"{_fmt(stats['roc_auc'])} | {_fmt(stats['accuracy'])} | "
                     f"{_fmt(stats['precision'])} | {_fmt(stats['recall'])} | "
                     f"{_fmt(stats['f1'])} | {_fmt(stats['brier'])} |")
    lines += ['', f'采纳规则：运营问的是"这次要不要提前提示"，是带阈值的判断，'
                  f"因此以 F1（阈值在每个训练窗口内按 F1 选）为主指标，要求模型领先最佳基线 "
                  f"{int(ADOPT_MARGIN * 100)}%；同时用平均精度作护栏——排序质量不得低于最佳基线，"
                  f"防止靠调阈值换指标。两条都满足才上线模型，否则如实上线经验规则。"
                  f"本次上线的是 **{section['chosen_label']}**"
                  f"（{'模型采纳' if section['model_adopted'] else '未达门槛，上线基线'}），"
                  f"逐块胜出 {section['block_wins']}/{section['block_total']}。", '']
    if section['model_adopted']:
        lines += [f"相对最佳基线：F1 提升 {_percent(section['advantage'])}，"
                  f"平均精度提升 {_percent(section['skill'])}。", '']
    deployment = section['deployment']
    lines += [f"部署树：{deployment['leaves']} 片叶子，max_depth={deployment['params']['max_depth']}、"
              f"min_samples_leaf={deployment['params']['min_samples_leaf']}、"
              f"ccp_alpha={deployment['params']['alpha']}；"
              f"判定阈值 {deployment['decision_threshold']}（在训练数据上按 F1 选）。",
              '', '### 树读出来的规则（按叶子正类比例排序）', '',
              '| 条件 | 命中的历史会话 | 其中长时长 | 叶子概率 | 说明 |', '|---|---:|---:|---:|---|']
    for rule in deployment['rules']:
        lines.append(f"| `{rule['conditions_text']}` | {rule['samples']} | {rule['positives']} | "
                     f"{rule['probability']} | {rule['translation']} |")
    lines += ['', '### 概率校准（训练数据内）', '',
              '| 预测概率区间 | 样本数 | 平均预测值 | 实际正类比例 |', '|---|---:|---:|---:|']
    for row in deployment['calibration']:
        lines.append(f"| {row['range']} | {row['samples']} | {row['mean_score']} | "
                     f"{row['observed_rate']} |")
    if deployment['importance']:
        lines += ['', '### 特征重要度（Gini 增益 × 节点样本数，归一化）', '',
                  '| 特征 | 重要度 |', '|---|---:|']
        for item in deployment['importance']:
            lines.append(f"| `{item['feature']}` | {item['weight']} |")
    lines += ['', '### 使用方式与限制', '',
              '- 概率高的会话可以提前提示车位周转，概率低的不需要额外动作。',
              '- 叶子概率来自历史同组会话，样本少的叶子分辨率有限；',
              '- 标签阈值随数据更新，跨批次比较前要看模型版本。']
    for item in section['limits']:
        lines.append(f'- {item}')
    text = '\n'.join(lines) + '\n'
    if path:
        Path(path).write_text(text, encoding='utf-8')
    return text


def _fmt(value):
    return '—' if value is None else f'{value}'


def _percent(value):
    return '—' if value is None else f'{value * 100:.1f}%'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', required=True, help='数仓 SQLite 路径')
    parser.add_argument('--out', help='输出 JSON（分类模型与指标）')
    parser.add_argument('--report', help='输出模型卡片段（markdown）')
    parser.add_argument('--quantile', type=float, default=DEFAULT_QUANTILE,
                        help='长时长标签的分位（默认 0.75）')
    args = parser.parse_args(argv)
    section = train(args.db, quantile_level=args.quantile)
    if args.out:
        Path(args.out).write_text(json.dumps(section, ensure_ascii=False, indent=2) + '\n',
                                  encoding='utf-8')
    text = build_report(section, args.report)
    print(text if not args.report else f'写出 {args.report}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
