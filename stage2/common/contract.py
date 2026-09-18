"""第二阶段数据契约的唯一真源（对应 stage2/docs/数据来源与字段定义.md）。

清洗环节是生产方，数仓与大屏是消费方；码表、峰谷时段、电价、必需列都只在这里写一份，
避免同一套定义在多个文件里各存一套、改一处漏三处。

消费方用法：
    from contract import load_cleaning
    data = load_cleaning(<清洗结果目录>)   # 校验不通过直接抛 ContractError
"""
import csv
import hashlib
import json
from decimal import Decimal
from pathlib import Path

# 清洗规则版本：字段或取值变更必须同时改这里、clean.py 的规则表与《数据来源与字段定义.md》。
RULE_VERSION = '2.0'
REBUILD_HINT = ('请在仓库根目录执行：bash stage2/run_all.sh'
                '（或 CLEANING=<目录> bash stage2/run_all.sh 复用已有清洗结果）')

FILES = {'sessions': 'dwd/sessions.csv', 'stations': 'dwd/stations.csv', 'battery': 'dwd/battery.csv'}

REQUIRED_COLUMNS = {
    'sessions': ['source_row', 'session_id', 'user_id', 'station_id', 'location_id', 'facility_type',
                 'facility_label', 'station_name', 'address', 'kwh', 'fee_original', 'fee_status',
                 'calendar_date', 'start_hour', 'end_hour', 'weekday', 'weekday_index', 'weekend',
                 'duration_hours', 'time_period', 'unit_price', 'estimated_fee', 'platform',
                 'positive_energy', 'time_of_day_usable', 'weekday_usable', 'quality_flags'],
    'stations': ['source_row', 'stationId', 'locationId', 'facilityType', 'station_name', 'address',
                 'device_count', 'open_time', 'update_time', 'facility_label', 'geography_status', 'quality_flags'],
    'battery': ['source_row', 'esd_raw', 'record_time_raw', 'timestamp', 'time_series_usable', 'soc_percent',
                'pack_voltage_v', 'current_a', 'max_cell_voltage_v', 'min_cell_voltage_v', 'max_temperature_c',
                'min_temperature_c', 'available_energy_source_unit_unverified', 'available_capacity_ah',
                'instantaneous_power_magnitude_kw', 'quality_flags'],
}

MANIFEST_KEYS = ['rule_version', 'output_sha256', 'reconciliation', 'total_kwh',
                 'fee_total_source_unverified', 'fee_total_estimated_model']

# 课程资料注释给出的码表：1=直流 / 2=交流 / 3=直交流一体；数据集另有编码 4，无对应标签，保留原编码并标记待核。
FACILITY_LABELS = {'1': '直流', '2': '交流', '3': '直交流一体'}
# 分时电价（元/kWh，项目设定的参考电价）：高峰 1.5 / 平时 1.0 / 低谷 0.7。
TARIFF = {'peak': Decimal('1.5'), 'normal': Decimal('1.0'), 'off_peak': Decimal('0.7')}
PERIODS = ['peak', 'normal', 'off_peak']
PERIOD_LABELS = {'peak': '高峰', 'normal': '平时', 'off_peak': '低谷'}
PERIOD_HOURS = {'peak': '8–11、18–21', 'normal': '12–17', 'off_peak': '0–7、22–23'}
DAY_TYPE_LABELS = {'weekday': '工作日', 'weekend': '周末'}


class ContractError(RuntimeError):
    """清洗产物与《数据来源与字段定义.md》不一致。禁止静默降级，必须重跑清洗链路。"""


def period_of(hour):
    """峰谷时段划分：高峰 8–11 与 18–21，平时 12–17，低谷 0–7 与 22–23。"""
    hour = int(hour)
    if hour in (8, 9, 10, 11, 18, 19, 20, 21):
        return 'peak'
    if 12 <= hour <= 17:
        return 'normal'
    return 'off_peak'


def facility_label(code):
    """桩类型码表标签；未收录编码保留原编码，不猜测语义。"""
    return FACILITY_LABELS.get(str(code), f'待核编码{code}')


def unit_price(period):
    """时段电价（元/kWh）；时段为空返回 None，不按低谷兜底。"""
    return TARIFF.get(period)


def read_table(root, name):
    """读取 CSV 并校验必需列齐全；缺列直接抛 ContractError，不进入 KeyError。"""
    path = Path(root) / FILES[name]
    if not path.exists():
        raise ContractError(f'缺少清洗产物 {FILES[name]}。{REBUILD_HINT}')
    with path.open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        columns = reader.fieldnames or []
        missing = [c for c in REQUIRED_COLUMNS[name] if c not in columns]
        if missing:
            raise ContractError(f'{FILES[name]} 缺少必需列：{"、".join(missing)}。'
                                f'期望列契约见 stage2/docs/数据来源与字段定义.md。{REBUILD_HINT}')
        rows = [row for row in reader]
    broken = [i for i, row in enumerate(rows, start=2)
              if any(row.get(c) is None for c in REQUIRED_COLUMNS[name])]
    if broken:
        raise ContractError(f'{FILES[name]} 第 {broken[0]} 行列数与表头不一致，清洗产物已损坏。{REBUILD_HINT}')
    return rows


def amount(value):
    return Decimal(str(value))


def amount_or_none(value):
    return None if value in ('', None) else amount(value)


def load_cleaning(directory):
    """加载并校验清洗产物；任何不一致都抛 ContractError，绝不静默降级。"""
    root = Path(directory).resolve()
    manifest_path = root / 'manifest.json'
    if not manifest_path.exists():
        raise ContractError(f'缺少清洗清单 manifest.json：{root}。{REBUILD_HINT}')
    try:
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    except json.JSONDecodeError as error:
        raise ContractError(f'manifest.json 无法解析（{error}）。{REBUILD_HINT}')
    for key in MANIFEST_KEYS:
        if key not in manifest:
            raise ContractError(f'manifest.json 缺少必需键 {key}，产物早于 v{RULE_VERSION} 定义。{REBUILD_HINT}')
    if manifest['rule_version'] != RULE_VERSION:
        raise ContractError(f'清洗规则版本不匹配：期望 {RULE_VERSION}，实际 {manifest["rule_version"]}。'
                            f'大屏与该版本列契约（含 time_period / estimated_fee）配套，旧产物无法渲染。{REBUILD_HINT}')

    sessions = read_table(root, 'sessions')
    stations = read_table(root, 'stations')
    battery = read_table(root, 'battery')

    for name, rows in [('sessions', sessions), ('stations', stations), ('battery', battery)]:
        expected = manifest['reconciliation'].get(name)
        if not expected:
            raise ContractError(f'manifest.json 缺少 {name} 对账记录。{REBUILD_HINT}')
        if len(rows) != expected['retained']:
            raise ContractError(f'{FILES[name]} 行数与清洗清单不一致：清单 {expected["retained"]}，'
                                f'实际 {len(rows)}。{REBUILD_HINT}')
        expected_hash = manifest['output_sha256'].get(FILES[name])
        if not expected_hash:
            raise ContractError(f'manifest.json 缺少 {FILES[name]} 的输出哈希。{REBUILD_HINT}')
        if hashlib.sha256((root / FILES[name]).read_bytes()).hexdigest() != expected_hash:
            raise ContractError(f'{FILES[name]} 内容与清洗清单哈希不一致，文件可能被改动。{REBUILD_HINT}')

    checks = [('会话总电量', 'total_kwh', str(sum((amount(r['kwh']) for r in sessions), Decimal(0)))),
              ('原始费用合计', 'fee_total_source_unverified',
               str(sum((amount(r['fee_original']) for r in sessions), Decimal(0)))),
              ('估算电费合计', 'fee_total_estimated_model',
               str(sum((amount_or_none(r['estimated_fee']) or Decimal(0) for r in sessions), Decimal(0))))]
    for label, key, actual in checks:
        if actual != manifest[key]:
            raise ContractError(f'{label}与清单不一致：清单 {manifest[key]}，实际 {actual}。{REBUILD_HINT}')

    # 派生列交叉校验，防止"列在、取值与定义不一致"
    for row in sessions:
        expected_period = '' if row['time_of_day_usable'] != '1' else period_of(row['start_hour'])
        if row['time_period'] != expected_period:
            raise ContractError(f'第 {row["source_row"]} 行时段取值不符：time_period={row["time_period"]}，'
                                f'按 start_hour={row["start_hour"]} 应为 {expected_period}。{REBUILD_HINT}')
        price = amount_or_none(row['unit_price'])
        if price is not None and price != unit_price(row['time_period']):
            raise ContractError(f'第 {row["source_row"]} 行电价与时段不符：{row["unit_price"]} vs '
                                f'{row["time_period"]}。{REBUILD_HINT}')

    return dict(root=root, manifest=manifest, sessions=sessions, stations=stations, battery=battery,
                version=hashlib.sha256(manifest_path.read_bytes()).hexdigest()[:16])
