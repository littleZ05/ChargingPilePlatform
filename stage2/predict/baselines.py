#!/usr/bin/env python3
"""基线：全局均值、季节 naive、四周移动平均。

没有基线的模型指标没有意义。任何回归模型都必须先证明它比"照抄上周同日"
和"照抄昨天同时段"更好，否则就是白折腾。
"""

METHODS = ['global_mean', 'seasonal_naive', 'moving_average']

LABELS = {'global_mean': '全局均值',
          'seasonal_naive': '季节 naive（上周同日同时段）',
          'moving_average': '四周移动平均（同一时段）'}

# 目标 -> 取用的滞后列
COLUMNS = {
    'sessions': dict(recent='lag_1d', weekly='lag_7d', average='lag_7d_ma'),
    'kwh': dict(recent='lag_1d_kwh', weekly='lag_7d_kwh', average='lag_7d_ma_kwh'),
}


def fit(rows, method, target='sessions'):
    """拟合基线。只有全局均值需要"训练"，其余是严格的无参数规则。"""
    if method not in METHODS:
        raise ValueError(f'未知基线：{method}')
    if method == 'global_mean':
        usable = [row for row in rows if row['mask']]
        if not usable:
            raise ValueError('训练集为空，无法拟合全局均值')
        return dict(mean=sum(float(row[target]) for row in usable) / len(usable))
    return {}


def predict(rows, method, fitted=None, target='sessions'):
    """逐行给出预测。取不到滞后值时按 上周同日 → 昨天同时段 → 全局均值 回退。

    返回 (values, fallback_count)：values 与 rows 等长，None 表示连回退值都没有。
    """
    if method not in METHODS:
        raise ValueError(f'未知基线：{method}')
    columns = COLUMNS[target]
    fitted = fitted or {}
    fallback = fitted.get('mean')
    values, fallbacks = [], 0
    for row in rows:
        if method == 'global_mean':
            values.append(fallback)
            continue
        if method == 'seasonal_naive':
            primary = row.get(columns['weekly'])
        else:
            primary = row.get(columns['average'])
        if primary is None:
            primary = row.get(columns['recent'])
        if primary is None:
            primary = fallback
            fallbacks += 1
        values.append(primary)
    return values, fallbacks
