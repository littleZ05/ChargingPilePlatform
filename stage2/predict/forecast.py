#!/usr/bin/env python3
"""预测推理：读 model.json 输出小时级会话量与电量预测（供 API 与服务复用）。"""

DAYS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']


def baseline_predict(model, weekday, hour):
    """季节基线：小时画像 × 星期系数。"""
    profile = {int(k): v for k, v in model['hour_profile'].items()}
    return profile.get(hour, 0.0) * model['weekday_factor'].get(weekday, 1.0)


def linear_predict(coefficients, weekday, hour):
    """线性回归：截距 + hour + is_weekend + weekday_index。"""
    value = (coefficients[0] + coefficients[1] * hour
             + coefficients[2] * (1.0 if weekday in ('Sat', 'Sun') else 0.0)
             + coefficients[3] * DAYS.index(weekday))
    return max(value, 0.0)


def forecast(model, weekday, horizon=24):
    """按星期输出 1–24 小时预测；quiet_hours 为预测最空闲的 5 个时段。"""
    if weekday not in DAYS:
        raise ValueError(f'星期无效：{weekday}')
    hours = []
    for hour in range(horizon):
        if model['method'] == 'seasonal':
            sessions = baseline_predict(model, weekday, hour)
        else:
            sessions = linear_predict(model['linear_coefficients'], weekday, hour)
        hours.append(dict(hour=hour, sessions=round(sessions, 2),
                          kwh=round(sessions * (model.get('energy_per_session') or 0.0), 2)))
    return dict(weekday=weekday, method=model['method'], version=model['version'], hours=hours,
                total_sessions=round(sum(h['sessions'] for h in hours), 2),
                total_kwh=round(sum(h['kwh'] for h in hours), 2),
                quiet_hours=[h['hour'] for h in sorted(hours, key=lambda x: x['sessions'])[:5]],
                evaluation=model.get('evaluation'), caveats=model.get('caveats', []))
