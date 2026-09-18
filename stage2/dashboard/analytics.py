"""第二阶段运营分析：只读已校验的清洗产物。

数据来源与字段定义的唯一真源是 `stage2/common/contract.py`（人读版：`stage2/docs/数据来源与字段定义.md`）。
本模块只做"聚合与呈现"，不自己定义码表、时段或电价，也不在缺列时静默降级。
"""
import sys
from collections import Counter, defaultdict
from decimal import Decimal
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'common'))
from contract import (  # noqa: E402
    DAY_TYPE_LABELS, FACILITY_LABELS, PERIOD_HOURS, PERIOD_LABELS, PERIODS, TARIFF,
    ContractError, amount, amount_or_none, facility_label, load_cleaning, period_of,
)

__all__ = ['Analytics', 'ContractError', 'period_of', 'FILTERS', 'DAYS']

FILTERS = {'station_id', 'facility_type', 'time_period', 'platform', 'weekday', 'energy'}
DAYS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']
DIMENSIONS = ['station_id', 'facility_type', 'time_period', 'platform', 'weekday', 'start_hour', 'day_type']
FACILITY_CODES = ['1', '2', '3', '4']


def summarize(rows):
    count = len(rows)
    energy = sum((amount(r['kwh']) for r in rows), Decimal(0))
    hours = sum((amount(r['duration_hours']) for r in rows), Decimal(0))
    estimated = [amount_or_none(r['estimated_fee']) for r in rows]
    return dict(sessions=count, positive_energy=sum(amount(r['kwh']) > 0 for r in rows),
                zero_energy=sum(amount(r['kwh']) == 0 for r in rows),
                total_kwh=str(energy), duration_hours=str(hours),
                mean_kwh=str((energy / count).quantize(Decimal('.01'))) if count else None,
                mean_duration_hours=str((hours / count).quantize(Decimal('.01'))) if count else None,
                zero_fee=sum(amount(r['fee_original']) == 0 for r in rows),
                source_fee_unverified=str(sum((amount(r['fee_original']) for r in rows), Decimal(0))),
                estimated_fee_model=str(sum((v for v in estimated if v is not None), Decimal(0))),
                active_stations=len({r['station_id'] for r in rows}))


class Analytics:
    """大屏与报表的数据源；构造即完成契约校验。"""

    def __init__(self, directory):
        data = load_cleaning(directory)
        self.root = data['root']
        self.manifest = data['manifest']
        self.version = data['version']
        self.sessions = data['sessions']
        self.stations = {r['stationId']: r for r in data['stations']}
        self.battery = data['battery']
        self.rule_version = data['manifest']['rule_version']
        self.period_order = [p for p in PERIODS if any(r['time_period'] == p for r in self.sessions)]
        self.options = {key: sorted({r[key] for r in self.sessions if r[key] != ''})
                        for key in FILTERS - {'energy'}}
        self.options['weekday'] = [d for d in DAYS if d in self.options['weekday']]
        self.options['time_period'] = [p for p in PERIODS if p in self.options['time_period']]
        self.options['energy'] = ['all', 'positive', 'zero']

    # ---- 查询 ----
    def select(self, filters):
        """交集筛选；未知筛选键或非法枚举一律报错，不静默忽略。"""
        if set(filters) - FILTERS:
            raise ValueError('不支持的筛选项')
        selected = {k: v for k, v in filters.items() if v and not (k == 'energy' and v == 'all')}
        for key, value in selected.items():
            if value not in self.options[key]:
                raise ValueError(f'筛选值无效：{key}')
        rows = self.sessions
        for key, value in selected.items():
            if key == 'energy':
                rows = [r for r in rows if (amount(r['kwh']) > 0) == (value == 'positive')]
            else:
                rows = [r for r in rows if r[key] == value]
        return selected, rows

    def labeled(self, field, key):
        if field == 'station_id':
            return self.stations.get(key, {}).get('station_name', key)
        if field == 'facility_type':
            return facility_label(key)
        if field == 'time_period':
            return PERIOD_LABELS.get(key, key)
        if field == 'day_type':
            return DAY_TYPE_LABELS.get(key, key)
        return key

    def overview(self, filters):
        selected, rows = self.select(filters)
        dimensions = {}
        for field in DIMENSIONS:
            groups = defaultdict(list)
            for row in rows:
                if field == 'start_hour' and row['time_of_day_usable'] != '1':
                    continue
                if field == 'weekday' and row['weekday_usable'] != '1':
                    continue
                if field == 'time_period' and row['time_period'] == '':
                    continue
                key = ('weekend' if row['weekend'] == '1' else 'weekday') if field == 'day_type' else row[field]
                groups[key].append(row)
            if field == 'start_hour':
                keys = [str(hour) for hour in range(24)]
            elif field == 'weekday':
                keys = DAYS
            elif field == 'time_period':
                keys = self.period_order
            elif field == 'day_type':
                keys = ['weekday', 'weekend']
            elif field == 'facility_type':
                keys = sorted(groups, key=lambda k: (k not in FACILITY_CODES, k))
            else:
                keys = sorted(groups)
            buckets = [dict(key=key, label=self.labeled(field, key), **summarize(groups[key])) for key in keys]
            if field == 'station_id':
                buckets.sort(key=lambda r: (-r['sessions'], r['key']))
            dimensions[field] = buckets
        durations = [('0–1小时', Decimal(0), Decimal(1)), ('1–2小时', Decimal(1), Decimal(2)),
                     ('2–4小时', Decimal(2), Decimal(4)), ('4–8小时', Decimal(4), Decimal(8)),
                     ('8–24小时', Decimal(8), Decimal(24)), ('超过24小时', Decimal(24), Decimal('Infinity'))]
        duration_histogram = [dict(label=label, count=sum(low < amount(r['duration_hours']) <= high for r in rows))
                              for label, low, high in durations]
        flags = Counter(flag for row in rows for flag in row['quality_flags'].split(';') if flag)
        return dict(version=self.version, rule_version=self.rule_version, filters=selected,
                    summary=summarize(rows), dimensions=dimensions, duration_histogram=duration_histogram,
                    quality=dict(flags),
                    coverage=dict(all_sessions=len(self.sessions), selected_sessions=len(rows),
                                  source_stations=len(self.stations), battery_records=len(self.battery),
                                  hour_usable=sum(1 for r in rows if r['time_of_day_usable'] == '1'),
                                  weekday_usable=sum(1 for r in rows if r['weekday_usable'] == '1'),
                                  period_usable=sum(1 for r in rows if r['time_period'] != '')),
                    notes=['会话数不是桩数；正电量不等于已支付。',
                           '小时与星期来自已校验源字段，年份不可信，不发布真实日期趋势。',
                           '原费用币种与含义未核实，不作营收或补算收入。',
                           '估算电费 = 电量 × 时段单价（高峰1.5/平时1.0/低谷0.7 元，单价为项目设定的参考值），非真实营收。',
                           '桩类型按课堂码表映射为直流/交流/直交流一体；编码4无对应标签，保留原编码待核。',
                           '电池样本不与会话ID关联，不随会话筛选变化。'])

    def metadata(self):
        return dict(version=self.version, rule_version=self.rule_version, options=self.options,
                    assumptions=dict(period_hours=PERIOD_HOURS, period_labels=PERIOD_LABELS,
                                     tariff={k: str(v) for k, v in TARIFF.items()},
                                     facility_labels=FACILITY_LABELS, day_type_labels=DAY_TYPE_LABELS),
                    reconciliation=self.manifest['reconciliation'],
                    total_kwh=self.manifest['total_kwh'],
                    source_fee_total_unverified=self.manifest['fee_total_source_unverified'],
                    fee_total_estimated_model=self.manifest['fee_total_estimated_model'],
                    stations=[dict(id=key, name=row['station_name']) for key, row in self.stations.items()])

    def report(self, kind, filters):
        """报表导出：按维度返回当前筛选下的聚合明细（CSV 由服务端拼装）。"""
        if kind not in DIMENSIONS:
            raise ValueError(f'不支持的报告维度：{kind}')
        data = self.overview(filters)
        return data['dimensions'][kind], data['summary'], data['version']

    def battery_summary(self):
        histograms = {}
        specs = {'soc_percent': [0, 20, 40, 60, 80, 100], 'current_a': [-100, -80, -60, -40, -20, 0, 20],
                 'max_temperature_c': [-50, 0, 20, 30, 40, 60, 85]}
        for field, edges in specs.items():
            histograms[field] = [dict(label=f'{a}至{b}', count=sum(
                (amount(r[field]) >= a if i == 0 else amount(r[field]) > a) and amount(r[field]) <= b
                for r in self.battery)) for i, (a, b) in enumerate(zip(edges, edges[1:]))]
        # Individual anonymous measurements, no esd/user identifiers or invented timestamps.
        points = [[float(r['soc_percent']), float(r['pack_voltage_v'])] for r in self.battery]
        return dict(records=len(self.battery), scope='独立电池样本，不受会话筛选影响',
                    histograms=histograms, soc_voltage=points,
                    flagged_current=sum('outside_tutorial_current_range_retained' in r['quality_flags']
                                        for r in self.battery),
                    notes=['电池样本不与会话或用户ID关联。',
                           '瞬时功率为电压×电流绝对值/1000，不是累计电量。',
                           '时间精度不可恢复，不做时间序列或充放电曲线。'])
