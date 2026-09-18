#!/usr/bin/env python3
"""面板特征：滞后项、观测覆盖率、未来 H 小时目标与目标编码。

两条硬约束：
1. 所有特征只看 t 之前的数据，`verify_backward_only` 用"篡改未来再看特征是否变化"
   的方式把这条钉死，不靠自觉；
2. 缺测日（掩码 0）不参与任何统计，既不当零，也不进滞后窗口。

目标对齐课程要求"预测未来 1 小时、6 小时、24 小时"：
  小时级       sessions / kwh            （未来 1 小时）
  未来 6 小时   sessions_h6 / kwh_h6
  未来 24 小时  sessions_h24 / kwh_h24
未来窗口里只要有一格缺测，该行目标即为空——不拿不完整的窗口当标签。
"""
HOURS = 24
LAG_WINDOW = 28

LAG_COLUMNS = ['lag_1d', 'lag_7d', 'lag_7d_ma', 'obs_28d',
               'lag_1d_kwh', 'lag_7d_kwh', 'lag_7d_ma_kwh',
               'prev_day_total', 'roll_7d_total',
               'prev_day_kwh_total', 'roll_7d_kwh_total']

HORIZONS = (1, 6, 24)


def task_spec(column):
    """把目标列名解析成建模所需信息：基线、跨度、滞后后缀。"""
    if column in ('sessions', 'kwh'):
        return dict(column=column, base=column, hours=1,
                    suffix='' if column == 'sessions' else '_kwh',
                    total_suffix='' if column == 'sessions' else '_kwh')
    for hours in (24, 6):
        for base in ('sessions', 'kwh'):
            if column == f'{base}_h{hours}':
                return dict(column=column, base=base, hours=hours,
                            suffix=f'_{base}_h{hours}',
                            total_suffix='' if base == 'sessions' else '_kwh')
    raise ValueError(f'未知目标列：{column}')


def _value_map(rows, column):
    return {(row['day_index'], row['hour']): row[column] for row in rows}


def _observed_days(rows):
    return {row['day_index'] for row in rows if row['mask']}


def _attach_value_lags(rows, column, suffix, window):
    """为某个取值列生成滞后特征；被滞后的值本身为空时也算缺失。"""
    values = _value_map(rows, column)
    observed = _observed_days(rows)
    for row in rows:
        day, hour = row['day_index'], row['hour']

        def past_at(step):
            if (day - step) not in observed:
                return None
            return values.get((day - step, hour))

        row['lag_1d' + suffix] = past_at(1)
        row['lag_7d' + suffix] = past_at(7)
        window_values = [past_at(step) for step in range(1, window + 1)]
        window_values = [value for value in window_values if value is not None]
        row['lag_7d_ma' + suffix] = (round(sum(window_values) / len(window_values), 4)
                                     if window_values else None)


def _attach_totals(rows, column, suffix, window):
    """前一日与近一周的日总量，作为近期水平的背景特征。"""
    values = _value_map(rows, column)
    observed = _observed_days(rows)

    def day_total(day_index):
        if day_index not in observed:
            return None
        cells = [values.get((day_index, hour)) for hour in range(HOURS)]
        if any(cell is None for cell in cells):
            return None
        return round(sum(cells), 4)

    for row in rows:
        day = row['day_index']
        row['prev_day' + suffix] = day_total(day - 1)
        totals = [day_total(day - step) for step in range(1, window + 1)]
        totals = [total for total in totals if total is not None]
        row['roll_7d' + suffix] = round(sum(totals) / len(totals), 4) if totals else None


def attach_lags(panel, target='sessions', window=LAG_WINDOW):
    """给小时级面板补全部滞后特征；缺测导致取不到值的字段为 None。"""
    rows = [dict(row) for row in panel['panel']]
    for row in rows:
        row['obs_28d'] = 0
    observed = _observed_days(rows)
    for row in rows:
        row['obs_28d'] = sum(1 for step in range(1, window + 1)
                             if (row['day_index'] - step) in observed)
    _attach_value_lags(rows, 'sessions', '', window)
    _attach_value_lags(rows, 'kwh', '_kwh', window)
    _attach_totals(rows, 'sessions', '_total', window)
    _attach_totals(rows, 'kwh', '_kwh_total', window)
    return rows


def add_horizon(rows, hours, base='sessions'):
    """把目标改成"从当前小时起未来 hours 小时的总量"，并补该目标自己的滞后项。"""
    column = f'{base}_h{hours}' if hours > 1 else base
    index = {(row['day_index'], row['hour']): row for row in rows}
    for row in rows:
        total, complete = 0.0, True
        for step in range(hours):
            position = row['hour'] + step
            cell = index.get((row['day_index'] + position // HOURS, position % HOURS))
            if cell is None or not cell['mask'] or cell[base] is None:
                complete = False
                break
            total += float(cell[base])
        row[column] = round(total, 4) if complete else None
    if hours > 1:
        _attach_value_lags(rows, column, f'_{base}_h{hours}', LAG_WINDOW)
    return column


def expanding_target_encode(rows, key, target, prior_weight=5.0):
    """目标编码：用严格更早的时间点统计分组均值，避免把当前行的目标值漏进特征。

    分组样本不足时向全局先验收缩：value = (sum + w*prior) / (count + w)。
    返回编码列，顺序与按 (day_index, hour) 升序后的行一致。
    """
    ordered = sorted(rows, key=lambda row: (row['day_index'], row['hour']))
    totals, counts = {}, {}
    seen, running_sum = 0, 0.0
    encoded = []
    for row in ordered:
        prior = running_sum / seen if seen else 0.0
        group = row.get(key)
        total, count = totals.get(group, 0.0), counts.get(group, 0)
        denominator = count + prior_weight
        if group is None:
            encoded.append(None)
        elif denominator == 0:
            encoded.append(0.0)          # 无先验也无历史：退化为 0，不抛异常
        else:
            encoded.append(round((total + prior_weight * prior) / denominator, 6))
        totals[group] = total + float(row[target])
        counts[group] = count + 1
        running_sum += float(row[target])
        seen += 1
    return encoded


def verify_backward_only(panel, target='sessions', pivot=None):
    """把 pivot 当天及之后的观测全部放大，特征在 pivot 之前必须逐字段不变。

    这是"不泄漏未来"的可执行证明：任何用到未来信息的实现都会让它失败。
    """
    if pivot is None:
        pivot = max(row['day_index'] for row in panel['panel']) // 2
    before = attach_lags(panel, target=target)

    mutated = dict(panel)
    mutated['panel'] = [dict(row) for row in panel['panel']]
    for row in mutated['panel']:
        if row['day_index'] >= pivot:
            row['sessions'] = row['sessions'] * 7 + 100
            row['kwh'] = row['kwh'] * 7 + 100
    after = attach_lags(mutated, target=target)

    bad = []
    for old, new in zip(before, after):
        if old['day_index'] >= pivot:
            continue
        for column in LAG_COLUMNS:
            if old[column] != new[column]:
                bad.append(dict(day_index=old['day_index'], hour=old['hour'],
                                column=column, before=old[column], after=new[column]))
    return bad


def usable_rows(rows, spec, columns=None):
    """取出训练/评估可用的行：当天有采集、目标可用、且所需特征都不为空。"""
    columns = columns if columns is not None else ('lag_1d', 'lag_7d', 'lag_7d_ma')
    columns = tuple(name + spec['suffix'] for name in columns)
    return [row for row in rows
            if row['mask'] and row.get(spec['column']) is not None
            and all(row.get(column) is not None for column in columns)]
