#!/usr/bin/env python3
"""面板特征：滞后项、观测覆盖率与目标编码。

两条硬约束：
1. 所有特征只看 t 之前的数据，`verify_backward_only` 用"篡改未来再看特征是否变化"
   的方式把这条钉死，不靠自觉；
2. 缺测日（掩码 0）不参与任何统计，既不当零，也不进滞后窗口。
"""
HOURS = 24

LAG_WINDOW = 28          # 滞后均值回看的天数（四周）
LAG_COLUMNS = ['lag_1d', 'lag_7d', 'lag_7d_ma', 'lag_1d_kwh', 'lag_7d_kwh',
               'lag_7d_ma_kwh', 'obs_28d', 'prev_day_total', 'roll_7d_total']


def _index(panel, target):
    """把面板整理成查表结构：值表 + 观测日集合。"""
    values, kwh, observed = {}, {}, set()
    for row in panel['panel']:
        key = (row['day_index'], row['hour'])
        values[key] = float(row[target])
        kwh[key] = float(row['kwh'])
        if row['mask']:
            observed.add(row['day_index'])
    return values, kwh, observed


def attach_lags(panel, target='sessions', window=LAG_WINDOW):
    """给面板每一行补滞后特征；缺测导致取不到值的字段为 None。

    lag_1d       昨天同一小时
    lag_7d       上周同日同一小时
    lag_7d_ma    前 28 个日历日里同一小时的平均值（只统计有采集的天）
    obs_28d      前 28 个日历日里真有采集的天数，用作滞后均值的置信度
    prev_day_total / roll_7d_total   前一天、前一周的日总量
    """
    values, kwh, observed = _index(panel, target)
    rows = []
    for row in panel['panel']:
        day, hour = row['day_index'], row['hour']
        out = dict(row)

        out['lag_1d'] = values[(day - 1, hour)] if (day - 1) in observed else None
        out['lag_7d'] = values[(day - 7, hour)] if (day - 7) in observed else None
        out['lag_1d_kwh'] = kwh[(day - 1, hour)] if (day - 1) in observed else None
        out['lag_7d_kwh'] = kwh[(day - 7, hour)] if (day - 7) in observed else None

        past = [values[(day - step, hour)] for step in range(1, window + 1)
                if (day - step) in observed]
        out['obs_28d'] = len(past)
        out['lag_7d_ma'] = round(sum(past) / len(past), 4) if past else None

        past_kwh = [kwh[(day - step, hour)] for step in range(1, window + 1)
                    if (day - step) in observed]
        out['lag_7d_ma_kwh'] = round(sum(past_kwh) / len(past_kwh), 4) if past_kwh else None

        out['prev_day_total'] = _day_total(values, day - 1, observed)
        totals = [_day_total(values, day - step, observed) for step in range(1, 8)]
        totals = [total for total in totals if total is not None]
        out['roll_7d_total'] = round(sum(totals) / len(totals), 4) if totals else None
        rows.append(out)
    return rows


def _day_total(values, day_index, observed):
    if day_index not in observed:
        return None
    return round(sum(values[(day_index, hour)] for hour in range(HOURS)), 4)


def expanding_target_encode(rows, key, target, prior_weight=5.0):
    """目标编码：用严格更早的时间点统计分组均值，避免把当前行的目标值漏进特征。

    分组样本不足时向全局先验收缩：value = (sum + w*prior) / (count + w)。
    返回与 rows 等长、按 (day_index, hour) 升序后的编码列。
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


def usable_rows(rows, columns=None):
    """取出训练/评估可用的行：当天有采集，且所需特征都不为空。"""
    columns = columns if columns is not None else LAG_COLUMNS
    return [row for row in rows
            if row['mask'] and all(row.get(column) is not None for column in columns)]
