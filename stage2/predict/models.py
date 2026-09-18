#!/usr/bin/env python3
"""模型层：带滞后特征的岭回归（多元线性回归 + L2 正则）。

为什么是岭回归而不是普通最小二乘：设计矩阵里有 23 个小时哑变量、6 个星期哑变量
和多组高度相关的滞后项，直接用正规方程会不稳定甚至秩亏；样本只有两百多天，
正则能把方差压下来。求解仍用高斯消元手写，不引入 numpy。

特征（共 40 项左右）：
  小时哑变量（保留基准小时）、星期哑变量（保留基准星期）、是否周末、
  lag_1d、lag_7d、lag_7d_ma、缺失指示位、前一日总量、近一周日均总量、近 28 天观测天数

缺失处理：昨日/上周同日缺测时用四周均值顶替，并置对应的缺失指示位为 1，
让模型自己判断"这是替代值"而不是当成真实观测。
"""
import math

METHODS = ['ridge', 'ridge_residual']
LABELS = {'ridge': '岭回归（小时/星期哑变量 + 滞后特征）',
          'ridge_residual': '岭回归（以季节 naive 为底，学残差）'}
DAY_NAMES = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']

DEFAULT_ALPHA = 1.0


def baseline_value(row, spec, trailing=None):
    """残差模型使用的底：上周同一目标时段 → 昨天 → 训练期均值。"""
    value = row.get(f"lag_7d{spec['suffix']}")
    if value is None:
        value = row.get(f"lag_1d{spec['suffix']}")
    if value is None:
        value = trailing or 0.0
    return float(value)


def has_history(row, spec):
    """有一条以上历史观测才谈得上预测。"""
    return (row.get(spec['column']) is not None
            and row.get(f"lag_7d_ma{spec['suffix']}") is not None)


def design_row(row, spec):
    """把一行展开成特征向量；返回 (特征名, 取值)，顺序固定。"""
    names, values = [], []
    suffix = spec['suffix']
    total_suffix = spec['total_suffix']

    def add(name, value):
        names.append(name)
        values.append(float(value))

    for hour in range(1, 24):
        add(f'hour={hour}', 1.0 if row['hour'] == hour else 0.0)
    for day in DAY_NAMES[1:]:
        add(f'weekday={day}', 1.0 if row['weekday'] == day else 0.0)
    add('is_weekend', row['is_weekend'])

    average = row.get(f'lag_7d_ma{suffix}')
    recent = row.get(f'lag_1d{suffix}')
    weekly = row.get(f'lag_7d{suffix}')
    substitute = average if average is not None else 0.0
    add('lag_1d', recent if recent is not None else substitute)
    add('lag_1d_missing', 0.0 if recent is not None else 1.0)
    add('lag_7d', weekly if weekly is not None else substitute)
    add('lag_7d_missing', 0.0 if weekly is not None else 1.0)
    add('lag_7d_ma', substitute)
    add('prev_total', row.get(f'prev_day{total_suffix}') or 0.0)
    add('roll_total', row.get(f'roll_7d{total_suffix}') or 0.0)
    add('obs_28d', row.get('obs_28d') or 0.0)
    return names, values


def solve(matrix, vector, tolerance=1e-12):
    """高斯-约当消元解正规方程；不可辨识方向保留 0 系数。"""
    size = len(vector)
    augmented = [list(matrix[i]) + [vector[i]] for i in range(size)]
    for column in range(size):
        pivot = max(range(column, size), key=lambda r: abs(augmented[r][column]))
        if abs(augmented[pivot][column]) <= tolerance:
            continue
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(size):
            if row == column:
                continue
            factor = augmented[row][column]
            if factor:
                augmented[row] = [a - factor * b
                                  for a, b in zip(augmented[row], augmented[column])]
    return [augmented[i][size] for i in range(size)]


def fit(rows, method, spec, alpha=DEFAULT_ALPHA):
    """在训练行上拟合岭回归。"""
    if method not in METHODS:
        raise ValueError(f'未知模型：{method}')
    residual = method == 'ridge_residual'
    usable = [row for row in rows if row['mask'] and has_history(row, spec)]
    if len(usable) < 2:
        raise ValueError('训练样本不足，无法拟合模型')

    names, matrix, targets, baselines_used = None, [], [], []
    for row in usable:
        names, values = design_row(row, spec)
        matrix.append(values)
        target = float(row[spec['column']])
        base = baseline_value(row, spec) if residual else 0.0
        baselines_used.append(base)
        targets.append(target - base)

    count = len(matrix)
    width = len(names)
    means = [sum(column) / count for column in zip(*matrix)]
    stds = []
    for index, mean in enumerate(means):
        variance = sum((row[index] - mean) ** 2 for row in matrix) / count
        stds.append(math.sqrt(variance) or 1.0)
    scaled = [[(value - means[i]) / stds[i] for i, value in enumerate(row)]
              for row in matrix]

    gram = [[sum(row[i] * row[j] for row in scaled) + (alpha if i == j else 0.0)
             for j in range(width)] for i in range(width)]
    moment = [sum(row[i] * target for row, target in zip(scaled, targets))
              for i in range(width)]
    weights = solve(gram, moment)

    return dict(method=method, target=spec['column'], spec=spec, alpha=alpha, features=names,
                residual=residual, trailing_mean=sum(baselines_used) / len(baselines_used)
                if baselines_used else 0.0,
                weights=weights, means=means, stds=stds,
                intercept=sum(targets) / count, training_rows=count)


def predict(model, rows, spec=None):
    """逐行预测；没有历史可依据的行返回 None。负数截断为 0。"""
    spec = spec or model['spec']
    residual = model.get('residual', False)
    values, missing = [], 0
    for row in rows:
        if not row['mask'] or not has_history(row, spec):
            values.append(None)
            missing += 1
            continue
        _, vector = design_row(row, spec)
        total = model['intercept']
        for weight, value, mean, std in zip(model['weights'], vector,
                                            model['means'], model['stds']):
            total += weight * (value - mean) / std
        if residual:
            total += baseline_value(row, spec, model.get('trailing_mean'))
        values.append(max(total, 0.0))
    return values, missing


def top_weights(model, limit=8):
    """按绝对值挑出权重最大的特征，供报告解释模型。"""
    pairs = sorted(zip(model['features'], model['weights']),
                   key=lambda item: -abs(item[1]))[:limit]
    return [dict(feature=name, weight=round(weight, 4)) for name, weight in pairs]


class ExpandingRidge:
    """扩展窗口下的增量拟合。

    正规方程只依赖 XᵀX、Xᵀy 与样本数，这三个量可以随窗口展开逐日累加。
    逐 fold 重算整个设计矩阵是 O(折数 × 样本 × 特征²)，在纯 Python 里要跑几分钟；
    增量累加把总代价降到 O(样本 × 特征²)，整个滚动评估回到秒级。
    """

    def __init__(self, spec, alpha=DEFAULT_ALPHA, residual=False):
        self.spec = spec
        self.target = spec['column']
        self.alpha = alpha
        self.residual = residual
        self.names = None
        self.gram = None
        self.moment = None
        self.sums = None
        self.count = 0
        self.total = 0.0

    def add(self, rows):
        for row in rows:
            if not row['mask'] or not has_history(row, self.spec):
                continue
            names, vector = design_row(row, self.spec)
            if self.names is None:
                self.names = names
                width = len(names)
                self.gram = [[0.0] * width for _ in range(width)]
                self.moment = [0.0] * width
                self.sums = [0.0] * width
            base = baseline_value(row, self.spec) if self.residual else 0.0
            target = float(row[self.target]) - base
            width = len(vector)
            for i in range(width):
                value = vector[i]
                self.sums[i] += value
                self.moment[i] += value * target
                row_gram = self.gram[i]
                for j in range(i, width):
                    row_gram[j] += value * vector[j]
            self.count += 1
            self.total += target

    def fit(self):
        if not self.count or self.names is None:
            raise ValueError('训练样本不足，无法拟合模型')
        count = self.count
        width = len(self.names)
        means = [value / count for value in self.sums]
        target_mean = self.total / count
        stds = []
        for index in range(width):
            variance = self.gram[index][index] / count - means[index] ** 2
            stds.append(math.sqrt(max(variance, 0.0)) or 1.0)

        matrix = [[0.0] * width for _ in range(width)]
        vector = [0.0] * width
        for i in range(width):
            vector[i] = (self.moment[i] - count * means[i] * target_mean) / stds[i]
            for j in range(i, width):
                value = (self.gram[i][j] - count * means[i] * means[j]) / (stds[i] * stds[j])
                matrix[i][j] = value
                matrix[j][i] = value
            matrix[i][i] += self.alpha
        weights = solve(matrix, vector)
        method = 'ridge_residual' if self.residual else 'ridge'
        return dict(method=method, target=self.target, spec=self.spec, alpha=self.alpha,
                    residual=self.residual, trailing_mean=self.total / count,
                    features=list(self.names), weights=weights, means=means, stds=stds,
                    intercept=target_mean, training_rows=count)
