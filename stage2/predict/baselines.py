#!/usr/bin/env python3
"""基线：全局均值、季节 naive、四周移动平均。

没有基线的模型指标没有意义。任何回归模型都必须先证明它比"照抄上周同日"
和"照抄昨天同时段"更好，否则就是白折腾。
"""

METHODS = ['global_mean', 'seasonal_naive', 'moving_average']

LABELS = {'global_mean': '全局均值',
          'seasonal_naive': '季节 naive（上周同一目标时段）',
          'moving_average': '四周移动平均（同一目标时段）'}


def _available(rows, spec):
    return [row for row in rows if row['mask'] and row.get(spec['column']) is not None]


def fit(rows, method, spec):
    """拟合基线。只有全局均值需要"训练"，其余是严格的无参数规则。"""
    if method not in METHODS:
        raise ValueError(f'未知基线：{method}')
    if method == 'global_mean':
        usable = _available(rows, spec)
        if not usable:
            raise ValueError('训练集为空，无法拟合全局均值')
        return dict(mean=sum(float(row[spec['column']]) for row in usable) / len(usable))
    return {}


def predict(rows, method, fitted, spec):
    """逐行给出预测。取不到滞后值时按 上周 → 昨天 → 全局均值 回退。

    返回 (values, fallback_count)：values 与 rows 等长，None 表示连回退值都没有。
    """
    if method not in METHODS:
        raise ValueError(f'未知基线：{method}')
    suffix, column = spec['suffix'], spec['column']
    weekly = f'lag_7d{suffix}'
    recent = f'lag_1d{suffix}'
    average = f'lag_7d_ma{suffix}'
    mean = (fitted or {}).get('mean')

    values, fallbacks = [], 0
    for row in rows:
        if row.get(column) is None:
            values.append(None)
            continue
        if method == 'global_mean':
            values.append(mean)
            continue
        primary = row.get(weekly) if method == 'seasonal_naive' else row.get(average)
        if primary is None:
            primary = row.get(recent)
        if primary is None:
            primary = mean
            fallbacks += 1
        values.append(primary)
    return values, fallbacks
