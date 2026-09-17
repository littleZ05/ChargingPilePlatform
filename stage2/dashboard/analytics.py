"""Read-only operating analytics over verified course DWD, independent of Qt DB."""
import csv
import hashlib
import json
from collections import Counter, defaultdict
from decimal import Decimal
from pathlib import Path

FILTERS = {'station_id', 'facility_type', 'platform', 'weekday', 'time_period', 'energy'}
DAYS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']


def amount(value):
    return Decimal(str(value))


def summarize(rows):
    count = len(rows)
    energy = sum((amount(r['kwh']) for r in rows), Decimal(0))
    hours = sum((amount(r['duration_hours']) for r in rows), Decimal(0))
    return dict(sessions=count, positive_energy=sum(amount(r['kwh']) > 0 for r in rows),
                zero_energy=sum(amount(r['kwh']) == 0 for r in rows),
                total_kwh=str(energy), duration_hours=str(hours),
                mean_kwh=str((energy / count).quantize(Decimal('.01'))) if count else None,
                mean_duration_hours=str((hours / count).quantize(Decimal('.01'))) if count else None,
                zero_fee=sum(amount(r['fee_original']) == 0 for r in rows),
                source_fee_unverified=str(sum((amount(r['fee_original']) for r in rows), Decimal(0))),
                estimated_fee_model=str(sum((amount(r['estimated_fee']) for r in rows if r.get('estimated_fee')), Decimal(0))),
                active_stations=len({r['station_id'] for r in rows}))


class Analytics:
    def __init__(self, directory):
        root = Path(directory).resolve()
        manifest = json.loads((root / 'manifest.json').read_text())
        required = ['dwd/sessions.csv', 'dwd/stations.csv', 'dwd/battery.csv']
        for name in required:
            expected = manifest.get('output_sha256', {}).get(name)
            if not expected or hashlib.sha256((root/name).read_bytes()).hexdigest() != expected:
                raise ValueError(f'清洗文件校验失败：{name}，请重新生成清洗结果')
        def read(name):
            with (root/name).open(encoding='utf-8-sig', newline='') as stream:
                return list(csv.DictReader(stream))
        self.sessions = read(required[0])
        self.stations = {r['stationId']: r for r in read(required[1])}
        self.battery = read(required[2])
        for name, rows in [('sessions', self.sessions), ('stations', self.stations), ('battery', self.battery)]:
            if len(rows) != manifest['reconciliation'][name]['retained']:
                raise ValueError(f'{name}行数与清洗清单不一致')
        self.version = hashlib.sha256((root/'manifest.json').read_bytes()).hexdigest()[:16]
        self.options = {key: sorted({r[key] for r in self.sessions})
                        for key in FILTERS - {'energy'}}
        self.options['weekday'] = [d for d in DAYS if d in self.options['weekday']]
        self.options['energy'] = ['all', 'positive', 'zero']
        self.facility_labels = {r['facility_type']: r['facility_label'] for r in self.sessions}
        self.period_order = ['peak', 'normal', 'off_peak']

    def overview(self, filters):
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
        dimensions = {}
        for field in ['station_id', 'facility_type', 'platform', 'weekday', 'start_hour', 'time_period']:
            groups = defaultdict(list)
            for row in rows:
                if field == 'start_hour' and row['time_of_day_usable'] != '1':
                    continue
                if field == 'weekday' and row['weekday_usable'] != '1':
                    continue
                if field == 'time_period' and not row['time_period']:
                    continue
                groups[row[field]].append(row)
            buckets = []
            if field == 'start_hour':
                keys = [str(i) for i in range(24)]
            elif field == 'weekday':
                keys = DAYS
            elif field == 'time_period':
                keys = [p for p in self.period_order if p in groups]
            else:
                keys = sorted(groups)
            for key in keys:
                label = key
                if field == 'station_id':
                    label = self.stations.get(key, {}).get('station_name', key)
                elif field == 'facility_type':
                    label = self.facility_labels.get(key, key)
                buckets.append(dict(key=key, label=label, **summarize(groups[key])))
            if field == 'station_id':
                buckets.sort(key=lambda r: (-r['sessions'], r['key']))
            dimensions[field] = buckets
        durations = [('0–1小时', Decimal(0), Decimal(1)), ('1–2小时', Decimal(1), Decimal(2)),
                     ('2–4小时', Decimal(2), Decimal(4)), ('4–8小时', Decimal(4), Decimal(8)),
                     ('8–24小时', Decimal(8), Decimal(24)), ('超过24小时', Decimal(24), Decimal('Infinity'))]
        duration_histogram = [dict(label=label, count=sum(low < amount(r['duration_hours']) <= high for r in rows))
                              for label, low, high in durations]
        flags = Counter(flag for row in rows for flag in row['quality_flags'].split(';') if flag)
        return dict(version=self.version, filters=selected, summary=summarize(rows), dimensions=dimensions,
                    day_type=self.day_type(rows),
                    duration_histogram=duration_histogram, quality=dict(flags),
                    coverage=dict(all_sessions=len(self.sessions), selected_sessions=len(rows),
                                  source_stations=len(self.stations), battery_records=len(self.battery)),
                    notes=['会话数不是桩数；正电量不代表已支付。',
                           '小时与星期来自已校验源字段，年份不可信，不发布真实日期趋势。',
                           '原费用币种与含义未核实，不作营收或补算收入。',
                           '估算电费为峰谷模型口径（高峰1.5/平时1.0/低谷0.7元×kWh），非真实营收。',
                           '电池样本不与会话ID关联，不随会话筛选变化。'])

    def metadata(self):
        return dict(version=self.version, options=self.options,
                    facility_labels=self.facility_labels,
                    stations=[dict(id=key, name=row['station_name']) for key, row in self.stations.items()])

    def day_type(self, rows):
        """工作日 vs 周末对比（对当前筛选结果），按日均归一，避免总量直接误导。"""
        groups = {'weekday': [], 'weekend': []}
        for row in rows:
            groups['weekend' if str(row.get('weekend')) in ('1', 'True', 'true') else 'weekday'].append(row)
        out = {}
        for key, members in groups.items():
            energy = sum((amount(r['kwh']) for r in members), Decimal(0))
            days = 5 if key == 'weekday' else 2
            out[key] = dict(sessions=len(members), days=days, total_kwh=str(energy),
                            per_day_sessions=round(len(members) / days, 2),
                            per_day_kwh=str((energy / days).quantize(Decimal('.01'))) if energy else '0',
                            avg_kwh=str((energy / len(members)).quantize(Decimal('.01'))) if members else None)
        return out

    def report(self, kind, filters):
        """报表导出：按维度返回当前筛选下的聚合明细（CSV 由服务端拼装）。"""
        if kind not in ('station_id', 'facility_type', 'platform', 'weekday', 'start_hour'):
            raise ValueError(f'不支持的报告维度：{kind}')
        data = self.overview(filters)
        return data['dimensions'][kind], data['summary'], data['version']

    def battery_summary(self):
        histograms = {}
        specs = {'soc_percent': [0,20,40,60,80,100], 'current_a': [-100,-80,-60,-40,-20,0,20],
                 'max_temperature_c': [-50,0,20,30,40,60,85]}
        for field, edges in specs.items():
            histograms[field] = [dict(label=f'{a}至{b}', count=sum(
                (amount(r[field]) >= a if i == 0 else amount(r[field]) > a) and amount(r[field]) <= b
                for r in self.battery)) for i, (a,b) in enumerate(zip(edges, edges[1:]))]
        # Individual anonymous measurements, no esd/user identifiers or invented timestamps.
        points = [[float(r['soc_percent']), float(r['pack_voltage_v'])] for r in self.battery]
        return dict(records=len(self.battery), scope='独立电池样本，不受会话筛选影响',
                    histograms=histograms, soc_voltage=points,
                    flagged_current=sum('outside_tutorial_current_range_retained' in r['quality_flags'] for r in self.battery))
