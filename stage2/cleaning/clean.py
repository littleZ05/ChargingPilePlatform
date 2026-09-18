#!/usr/bin/env python3
"""Auditable, deterministic course-data cleaning; standard library only."""
import argparse
import csv
import hashlib
import json
import shutil
import sys
import unicodedata
from collections import Counter, defaultdict
from datetime import datetime
from decimal import Decimal, InvalidOperation
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'common'))
from contract import (  # noqa: E402
    RULE_VERSION, facility_label, period_of as time_period, unit_price,
)

FILES = {'stations': 'nvv2t_md_end.csv', 'sessions': 'nvv2t.csv', 'battery': 'dsv13r2.csv'}
MISSING = {'', 'null', 'none', 'nan', 'n/a'}


def normalize(value):
    return unicodedata.normalize('NFKC', value).replace('\ufeff', '').replace('\u200b', '').strip()


def number(value):
    try:
        result = Decimal(value)
    except InvalidOperation:
        raise ValueError('invalid number') from None
    if not result.is_finite():
        raise ValueError('nonfinite number')
    return result


def numeric(value):
    return format(number(value), 'f')


def save_csv(path, rows, fields=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = fields or (list(rows[0]) if rows else ['source_row', 'reason'])
    with path.open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def run(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    if output.exists():
        raise ValueError('输出目录已存在，请指定新目录，避免覆盖历史清洗证据')
    output.mkdir(parents=True)
    audit, profiles, raw_hashes, quality = [], {}, {}, {}
    tables, rejected = {}, {}

    def event(table, row, rule, action, detail):
        audit.append(dict(table=table, source_row=row, rule=rule, action=action, detail=detail))

    for table, filename in FILES.items():
        path = source / filename
        raw_hashes[filename] = hashlib.sha256(path.read_bytes()).hexdigest()
        (output / 'ods').mkdir(exist_ok=True)
        shutil.copyfile(path, output / 'ods' / filename)
        with path.open(encoding='utf-8-sig', newline='') as stream:
            reader = csv.DictReader(stream)
            rows = list(reader)
            headers = reader.fieldnames
        if not headers or any(None in row or any(v is None for v in row.values()) for row in rows):
            raise ValueError(f'{filename}: 列数不一致，拒绝静默截断')
        profiles[table] = {'rows': len(rows), 'columns': headers, 'fields': {}}
        for key in headers:
            vals = [normalize(row[key]) for row in rows]
            nums = []
            for value in vals:
                try:
                    nums.append(number(value))
                except ValueError:
                    pass
            profiles[table]['fields'][key] = dict(
                missing=sum(v.lower() in MISSING for v in vals), distinct=len(set(vals)),
                numeric=len(nums), minimum=str(min(nums)) if nums else None,
                maximum=str(max(nums)) if nums else None)
        tables[table], rejected[table] = [], []
        pk = {'stations': 'stationId', 'sessions': 'sessionId', 'battery': 'esd'}[table]
        # Battery esd has no proven entity definition: only exact full-row duplicates are removed.
        normalized = [{k: normalize(v) for k, v in row.items()} for row in rows]
        keys = defaultdict(set)
        for row in normalized:
            keys[row[pk]].add(tuple(row.items()))
        seen = set()
        for index, row in enumerate(normalized, 2):
            original = rows[index - 2]
            if row != original:
                event(table, index, 'R01', 'normalize', 'Unicode/边缘空白标准化；原值见ODS')
            reason = None
            identity = tuple(row.items())
            if table != 'battery' and len(keys[row[pk]]) > 1:
                reason = 'conflicting_primary_key'
            elif identity in seen:
                reason = 'exact_duplicate'
            elif row[pk].lower() in MISSING:
                reason = 'missing_primary_key'
            seen.add(identity)
            if reason:
                rejected[table].append(dict(source_row=index, reason=reason, raw_json=json.dumps(original, ensure_ascii=False)))
                event(table, index, 'R02', 'quarantine', reason)
                continue
            tables[table].append({'source_row': index, **row})

    stations = []
    for row in tables['stations']:
        try:
            count = number(row['device_count'])
            if count <= 0 or count != count.to_integral_value():
                raise ValueError('invalid device count')
            if not row['station_name'] or not row['address']:
                raise ValueError('missing station description')
            if any(row[k].lower() in MISSING for k in ['locationId', 'facilityType']):
                raise ValueError('missing station identifiers')
            updated = datetime.strptime(row['update_time'], '%Y/%m/%d').strftime('%Y-%m-%d')
            stations.append({**row, 'device_count': int(count), 'update_time': updated,
                             'facility_label': facility_label(row['facilityType']),
                             'geography_status': 'provided_metadata_not_verified', 'quality_flags': 'facility_dictionary_assumed'})
        except ValueError as error:
            rejected['stations'].append(dict(source_row=row['source_row'], reason=str(error), raw_json=json.dumps(row, ensure_ascii=False)))
            event('stations', row['source_row'], 'R03', 'quarantine', str(error))
    metadata = {row['stationId']: row for row in stations}
    sessions = []
    days = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']
    hot = ['Mon', 'Tues', 'Wed', 'Thurs', 'Fri', 'Sat', 'Sun']
    for row in tables['sessions']:
        flags = []
        try:
            if any(row[k].lower() in MISSING for k in ['userId', 'stationId', 'locationId', 'facilityType']):
                raise ValueError('missing session identifiers')
            kwh, fee, hours = [number(row[k]) for k in ['kwhTotal', 'charging_fees', 'chargeTimeHrs']]
            if kwh < 0 or fee < 0 or hours <= 0:
                raise ValueError('invalid energy/fee/duration')
            start = datetime.strptime(row['created'], '%Y-%m-%d %H:%M:%S')
            end = datetime.strptime(row['ended'], '%Y-%m-%d %H:%M:%S')
            if end < start:
                raise ValueError('end_before_start')
            if abs(Decimal(str((end-start).total_seconds())) / 3600 - hours) > Decimal(1)/60:
                flags.append('duration_mismatch')
            if start.hour != int(row['startTime']) or end.hour != int(row['endTime']):
                flags.append('hour_mismatch')
            if row['weekday'] not in days:
                raise ValueError('invalid weekday')
            weekday = days.index(row['weekday'])
            if [int(row[k]) for k in hot] != [int(i == weekday) for i in range(7)]:
                flags.append('weekday_onehot_mismatch')
            if row['managerVehicle'] not in ['0', '1']:
                flags.append('unknown_manager_vehicle')
            if not 2000 <= start.year <= 2100 or not 2000 <= end.year <= 2100:
                flags.append('calendar_year_unverified')
            if hours > 24:
                flags.append('duration_over_24h_review')
            if kwh == 0:
                flags.append('zero_energy_retained')
            if fee == 0:
                flags.append('zero_fee_semantics_unknown')
            station = metadata.get(row['stationId'])
            if not station:
                raise ValueError('missing_station_metadata')
            if any(row[k] != station[k] for k in ['locationId', 'facilityType']):
                raise ValueError('station_metadata_conflict')
            # 保留原始费用字段（币种与语义未核实）；估算电费按项目设定的分时电价单独计算并明确标注，不与原始收费混同。
            period = time_period(int(row['startTime'])) if 'hour_mismatch' not in flags else ''
            price = unit_price(period)
            estimated_fee = (kwh * price).quantize(Decimal('.01')) if price is not None else None
            cleaned = dict(source_row=row['source_row'], session_id=row['sessionId'], user_id=row['userId'],
                station_id=row['stationId'], location_id=row['locationId'], facility_type=row['facilityType'],
                facility_label=station['facility_label'], station_name=station['station_name'], address=station['address'],
                kwh=numeric(row['kwhTotal']), fee_original=numeric(row['charging_fees']),
                fee_status='zero_unverified' if fee == 0 else 'source_reported_unverified',
                created_raw=row['created'], ended_raw=row['ended'],
                calendar_date='' if 'calendar_year_unverified' in flags else start.strftime('%Y-%m-%d'), timezone='unknown',
                start_hour=int(row['startTime']), end_hour=int(row['endTime']), weekday=row['weekday'],
                weekday_index=weekday, weekend=int(weekday >= 5), duration_hours=numeric(row['chargeTimeHrs']),
                time_period=period,
                unit_price=format(price, 'f') if price is not None else '',
                estimated_fee=format(estimated_fee, 'f') if estimated_fee is not None else '',
                platform=row['platform'].lower(), manager_vehicle=row['managerVehicle'],
                positive_energy=int(kwh > 0), time_of_day_usable=int('hour_mismatch' not in flags),
                weekday_usable=int('weekday_onehot_mismatch' not in flags),
                quality_flags=';'.join(flags + ['facility_dictionary_assumed', 'currency_unverified']))
            sessions.append(cleaned)
            for flag in flags:
                event('sessions', row['source_row'], 'R04', 'retain_flag', flag)
        except (ValueError, OverflowError) as error:
            rejected['sessions'].append(dict(source_row=row['source_row'], reason=str(error), raw_json=json.dumps(row, ensure_ascii=False)))
            event('sessions', row['source_row'], 'R04', 'quarantine', str(error))

    battery = []
    mapping = {'soc': 'soc_percent', 'pack_voltage (V)': 'pack_voltage_v', 'charge_current (A)': 'current_a',
               'max_cell_voltage (V)': 'max_cell_voltage_v', 'min_cell_voltage (V)': 'min_cell_voltage_v',
               'max_temperature (℃)': 'max_temperature_c', 'min_temperature (℃)': 'min_temperature_c',
               'available_energy (kw)': 'available_energy_source_unit_unverified',
               'available_capacity (Ah)': 'available_capacity_ah'}
    for row in tables['battery']:
        try:
            values = {dest: number(row[src]) for src, dest in mapping.items()}
            if not 0 <= values['soc_percent'] <= 100:
                raise ValueError('soc_out_of_bounds')
            if values['pack_voltage_v'] <= 0 or values['min_cell_voltage_v'] <= 0:
                raise ValueError('nonpositive_voltage')
            if values['max_cell_voltage_v'] < values['min_cell_voltage_v'] or values['max_temperature_c'] < values['min_temperature_c']:
                raise ValueError('min_max_conflict')
            flags = ['record_time_precision_unrecoverable' if 'e' in row['record_time'].lower()
                     else 'record_time_format_unverified', 'energy_unit_unverified', 'esd_semantics_unverified']
            if abs(values['current_a']) > 40:
                flags.append('outside_tutorial_current_range_retained')
            if not -50 <= values['min_temperature_c'] <= values['max_temperature_c'] <= 85:
                flags.append('temperature_range_review')
            battery.append(dict(source_row=row['source_row'], esd_raw=row['esd'], record_time_raw=row['record_time'],
                timestamp='', time_series_usable=0, **{key: format(value, 'f') for key, value in values.items()},
                instantaneous_power_magnitude_kw=format(abs(values['pack_voltage_v']*values['current_a'])/1000, 'f'),
                quality_flags=';'.join(flags)))
            for flag in flags:
                event('battery', row['source_row'], 'R05', 'retain_flag', flag)
        except ValueError as error:
            rejected['battery'].append(dict(source_row=row['source_row'], reason=str(error), raw_json=json.dumps(row, ensure_ascii=False)))
            event('battery', row['source_row'], 'R05', 'quarantine', str(error))

    for name, rows in [('stations', stations), ('sessions', sessions), ('battery', battery)]:
        save_csv(output/'dwd'/f'{name}.csv', rows)
        save_csv(output/'quarantine'/f'{name}.csv', rejected[name], ['source_row','reason','raw_json'])
        quality[name] = dict(input=profiles[name]['rows'], retained=len(rows), quarantined=len(rejected[name]))
        assert quality[name]['input'] == len(rows) + len(rejected[name])

    for dimension in ['station_id', 'facility_type', 'start_hour', 'weekday', 'platform', 'time_period']:
        groups = defaultdict(list)
        for row in sessions:
            if dimension == 'start_hour' and not row['time_of_day_usable']:
                continue
            if dimension == 'weekday' and not row['weekday_usable']:
                continue
            if dimension == 'time_period' and not row['time_period']:
                continue
            groups[str(row[dimension])].append(row)
        aggregates = []
        for key, members in sorted(groups.items()):
            aggregates.append(dict(dimension_value=key, session_count=len(members),
                positive_energy_count=sum(r['positive_energy'] for r in members),
                zero_energy_count=sum(not r['positive_energy'] for r in members),
                total_kwh=str(sum((number(r['kwh']) for r in members), Decimal(0))),
                source_fee_total_unverified=str(sum((number(r['fee_original']) for r in members), Decimal(0))),
                zero_fee_count=sum(number(r['fee_original']) == 0 for r in members),
                estimated_fee_total=str(sum((number(r['estimated_fee']) for r in members if r['estimated_fee']), Decimal(0))),
                duration_hours_total=str(sum((number(r['duration_hours']) for r in members), Decimal(0)))))
        save_csv(output/'ads'/f'by_{dimension}.csv', aggregates)
        assert sum(r['session_count'] for r in aggregates) == sum(len(g) for g in groups.values())
    save_csv(output/'audit/row_actions.csv', audit)
    counts = Counter((e['table'], e['action'], e['detail']) for e in audit)
    save_csv(output/'audit/rule_counts.csv', [dict(table=t, action=a, detail=d, count=n) for (t,a,d),n in sorted(counts.items())])
    (output/'audit/profile_before.json').write_text(json.dumps(profiles, ensure_ascii=False, indent=2)+'\n')
    for filename, digest in raw_hashes.items():
        assert hashlib.sha256((source/filename).read_bytes()).hexdigest() == digest
    manifest = dict(rule_version=RULE_VERSION, input_sha256=raw_hashes, reconciliation=quality,
                    total_kwh=str(sum((number(r['kwh']) for r in sessions), Decimal(0))),
                    fee_total_source_unverified=str(sum((number(r['fee_original']) for r in sessions), Decimal(0))),
                    fee_total_estimated_model=str(sum((number(r['estimated_fee']) for r in sessions if r['estimated_fee']), Decimal(0))),
                    output_sha256={str(p.relative_to(output)): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in sorted(output.rglob('*')) if p.is_file()})
    (output/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
    lines = ['# 数据清洗质量报告', '', f'规则版本：{RULE_VERSION}。原始数据未修改；不推测修复年份。估算电费按项目设定的分时电价派生，与原费用字段分列呈现。', '',
             '|数据集|输入|保留|隔离|','|---|---:|---:|---:|']
    lines += [f'|{k}|{v["input"]}|{v["retained"]}|{v["quarantined"]}|' for k,v in quality.items()]
    lines += ['', '## 六维质量评估', '',
              '- 完整性：每列缺失与数值范围见audit/profile_before.json。标记性空值与空字符串统一检测，关键错误隔离，不盲目均值填充。',
              '- 唯一性：全行重复去重隔离，站点/会话主键冲突全部隔离；电池esd语义未证实，不按其单列去重。',
              '- 有效性：数值有限、SOC范围、时间先后、电压大小、站点设备数逐行校验。',
              '- 一致性：会话与站点的ID/位置/设施类型核对；小时、星期one-hot、时长交叉校验。',
              '- 准确性：没有外部真实凭证，无法声称通过；费用含义、币种、设施码表、地理资料均保留待核标记。',
              '- 时效性：年份0014/0015和电池压缩时间不足以证明真实日期，不计算数据新鲜度、不发布真实日期趋势。',
              '', '## 逐规则影响数量', '', '|表|动作|原因|数量|','|---|---|---|---:|']
    lines += [f'|{t}|{a}|{d}|{n}|' for (t,a,d),n in sorted(counts.items())]
    lines += ['', '## 可用于第二阶段大屏', '',
              'ads按站点、设施类型（含码表标签）、开始小时、星期、平台、峰谷时段汇总。session_count是会话次数，不是桩数；零电量仍计入会话次数，另有正电量次数。',
              f'保留会话电量总计 {manifest["total_kwh"]} kWh；原字段费用总计 {manifest["fee_total_source_unverified"]}（币种与收费语义待核，不称真实营收）。',
              f'估算电费（高峰1.5/平时1.0/低谷0.7 元/kWh，单价为项目设定的参考值）总计 {manifest["fee_total_estimated_model"]}，不与原始收费字段混同、不称真实营收。',
              '设施类型按课堂码表映射为直流(1)/交流(2)/直交流一体(3)；编码4无对应标签，保留原编码并标记待核，该映射为假设。',
              '电池明细可做SOC、电压、电流、温度分布及关联分析。瞬时功率为电压×电流绝对值/1000，不是累计电量；没有采样间隔，不积分求能量。',
              '', '## 遗留与使用限制', '',
              '不将0014/0015自动改为2014/2015：加2000年后的星期一致并不能证明真实年份。calendar_date为空，原值保留。',
              '电池时间不可恢复，timestamp为空、time_series_usable=0；esd和sessionId数值重合也不构成已证实关联。',
              '负电流可能表示充电方向，超出课堂[-40,40]的记录仅标记，不机械删除。超过24小时会话保留待核。',
              '本次只用04.数据集最终版的三份源CSV；根目录part输出是已有派生物，不重复混入输入。',
              '原始层ODS、DWD、ADS、逐行审计及SHA256可追溯。输入数千行，标准库足够全量处理，不虚称分布式集群执行。']
    (output/'质量报告.md').write_text('\n'.join(lines)+'\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.input, args.output), ensure_ascii=False, indent=2))
