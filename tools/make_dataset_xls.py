#!/usr/bin/env python3
"""生成「数据集-第4组.xls」：课程数据集 + 采集/清洗清单，整理成一个真正的 .xls（BIFF8）。

用法：
  python3 tools/make_dataset_xls.py \
      --data "/home/bit/vmware_share/大数据开发/04.数据集最终版" \
      --run build-stage2/run-defense-20260918-180325 \
      --out "/home/bit/vmware_share/第二阶段项目大数据部分/第二阶段项目-大数据部分/03.项目文件/数据集-第4组.xls"

实现：先用标准库写一份 .fods（单文件 OpenDocument 表格），再调用 LibreOffice 转成 Excel 97 的 .xls，
最后把 .xls 转回 .xlsx 读一遍，核对工作表名与行数。不依赖 openpyxl / xlwt / pandas。
"""
import argparse
import csv
import json
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

SHEET_LIMIT_ROWS = 65536


class Text(str):
    """强制按文本写入的单元格：版本号 2.0 这类值不能当数字，否则会变成 2。"""


RAW_FILES = [
    ('nvv2t.csv', '充电会话（nvv2t）', '每行一次充电会话，分析主体'),
    ('nvv2t_md_end.csv', '站点资料（nvv2t_md_end）', '站点名称、地址、设备数与营业时间'),
    ('dsv13r2.csv', '电池样本（dsv13r2）', '电池电压、电流、温度、SOC 采样，与会话不关联'),
]

FIELDS = [
    # 文件, 字段, 类型, 含义, 单位与取值, 备注
    ('nvv2t.csv', 'sessionId', '整数', '会话编号', '主键', ''),
    ('nvv2t.csv', 'kwhTotal', '小数', '本次充电电量', 'kWh', '含 0 值会话，本批保留 55 条'),
    ('nvv2t.csv', 'charging_fees', '小数', '订单费用（原始字段）', '币种未核实', '合计 401.52，不当作营收'),
    ('nvv2t.csv', 'created', '文本', '订单创建时间', '原始字符串', '年份为 0014/0015，不推测修复'),
    ('nvv2t.csv', 'ended', '文本', '订单结束时间', '原始字符串', '同上'),
    ('nvv2t.csv', 'startTime', '整数', '开始小时', '0–23', '用于小时维度，校验未通过不进统计'),
    ('nvv2t.csv', 'endTime', '整数', '结束小时', '0–23', ''),
    ('nvv2t.csv', 'chargeTimeHrs', '小数', '充电时长', '小时', '> 24 小时 1 条，保留并标记复核'),
    ('nvv2t.csv', 'weekday', '文本', '星期', 'Mon–Sun 缩写', '源字段，非日均需求'),
    ('nvv2t.csv', 'platform', '文本', '充电平台', 'android / ios', ''),
    ('nvv2t.csv', 'userId', '整数', '用户编号', '—', '对外只出聚合与哈希键，不回传原始标识'),
    ('nvv2t.csv', 'stationId', '整数', '站点编号', '外键', '与站点表核对一致'),
    ('nvv2t.csv', 'locationId', '整数', '地理位置编号', '与站点表对应', ''),
    ('nvv2t.csv', 'managerVehicle', '整数', '是否管理车辆', '0 / 1', ''),
    ('nvv2t.csv', 'facilityType', '整数', '设施类型编码', '1 直流 / 2 交流 / 3 直交流一体', '编码 4 无对应标签，写作“待核编码4”'),
    ('nvv2t.csv', 'Mon–Sun', '整数', '星期 one-hot 列', '0 / 1', '部分行缺列，清洗时逐行校验'),
    ('nvv2t_md_end.csv', 'stationId', '整数', '站点编号', '主键', ''),
    ('nvv2t_md_end.csv', 'locationId', '整数', '地理位置编号', '—', ''),
    ('nvv2t_md_end.csv', 'facilityType', '整数', '设施类型编码', '同会话表', ''),
    ('nvv2t_md_end.csv', 'station_name', '文本', '站点名称', '中文', ''),
    ('nvv2t_md_end.csv', 'address', '文本', '站点地址', '中文，郑州市', ''),
    ('nvv2t_md_end.csv', 'device_count', '整数', '站点设备数', '个', ''),
    ('nvv2t_md_end.csv', 'open_time', '文本', '营业时间', '如 00:00-24:00', ''),
    ('nvv2t_md_end.csv', 'update_time', '文本', '资料来源更新时间', '如 2019/7/26', '年份未核实，不改写'),
    ('dsv13r2.csv', 'esd', '整数', '电池/设备标识', '—', '与会话表数值重合不构成关联证据'),
    ('dsv13r2.csv', 'record_time', '科学计数', '记录时间', '原始值', '精度不可恢复，不据此发布真实日期趋势'),
    ('dsv13r2.csv', 'soc', '小数', '荷电状态', '百分比', '范围校验通过'),
    ('dsv13r2.csv', 'pack_voltage (V)', '小数', '电池组电压', 'V', ''),
    ('dsv13r2.csv', 'charge_current (A)', '小数', '充电电流', 'A', '负值表示充电方向'),
    ('dsv13r2.csv', 'max_cell_voltage (V)', '小数', '单体电压最大值', 'V', ''),
    ('dsv13r2.csv', 'min_cell_voltage (V)', '小数', '单体电压最小值', 'V', ''),
    ('dsv13r2.csv', 'max_temperature (℃)', '小数', '最高温度', '℃', ''),
    ('dsv13r2.csv', 'min_temperature (℃)', '小数', '最低温度', '℃', ''),
    ('dsv13r2.csv', 'available_energy (kw)', '小数', '可用能量', '原始标注 kw', '单位语义未核实，不做能量积分'),
    ('dsv13r2.csv', 'available_capacity (Ah)', '小数', '可用容量', 'Ah', ''),
    ('派生字段（清洗后）', 'facility_label', '文本', '设施类型标签', '直流 / 交流 / 直交流一体 / 待核编码N', '按课堂码表映射，映射为假设'),
    ('派生字段（清洗后）', 'duration_hours', '小数', '单次时长', '小时', '由起止时间派生'),
    ('派生字段（清洗后）', 'time_period', '文本', '峰谷时段', '高峰 8–11、18–21；平时 12–17；低谷 0–7、22–23', '按课程资料划分，小时校验未通过留空'),
    ('派生字段（清洗后）', 'unit_price', '小数', '分时电价', '高峰 1.5 / 平时 1.0 / 低谷 0.7 元/kWh', '项目设定的参考电价，不用低谷值兜底'),
    ('派生字段（清洗后）', 'estimated_fee', '小数', '估算电费', '元', '按参考电价派生，与原费用字段分列呈现'),
    ('派生字段（清洗后）', 'quality_flags', '文本', '质量标记', '多个标记用分号连接', '标记可重叠，不能相加当异常率'),
]


def read_csv(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        return [row for row in csv.reader(stream)]


def cell_xml(value):
    """把单元格值写成 FODS 片段：能当数字的写 float，其余写 string。"""
    text = '' if value is None else str(value).strip()
    if text == '':
        return '<table:table-cell/>'
    if isinstance(value, Text):
        return f'<table:table-cell office:value-type="string"><text:p>{escape(text)}</text:p></table:table-cell>'
    try:
        number = float(text)
    except ValueError:
        return f'<table:table-cell office:value-type="string"><text:p>{escape(text)}</text:p></table:table-cell>'
    if number.is_integer() and abs(number) < 1e15 and 'e' not in text.lower():
        rendered = str(int(number))
        return (f'<table:table-cell office:value-type="float" office:value="{rendered}">'
                f'<text:p>{rendered}</text:p></table:table-cell>')
    return (f'<table:table-cell office:value-type="float" office:value="{text}">'
            f'<text:p>{escape(text)}</text:p></table:table-cell>')


def row_xml(values, header=False):
    style = ' table:style-name="ce-header"' if header else ''
    cells = ''.join(cell_xml(value).replace('<table:table-cell', f'<table:table-cell{style}', 1)
                    for value in values)
    return f'<table:table-row>{cells}</table:table-row>'


def sheet_xml(name, rows, width):
    body = ''.join(row_xml(row, header=(index == 0)) for index, row in enumerate(rows))
    return (f'<table:table table:name="{escape(name)}">'
            f'<table:table-column table:number-columns-repeated="{max(width, 1)}"/>'
            f'{body}</table:table>')


def markdown_table(text, heading):
    """从质量报告里取一张 markdown 表。"""
    lines = text.splitlines()
    try:
        start = next(index for index, line in enumerate(lines) if line.strip() == heading)
    except StopIteration:
        return []
    rows = []
    for line in lines[start + 1:]:
        stripped = line.strip()
        if not stripped.startswith('|'):
            if rows:
                break
            continue
        cells = [part.strip() for part in stripped.strip('|').split('|')]
        if all(set(cell) <= set('-: ') for cell in cells):
            continue
        rows.append(cells)
    return rows[1:]  # 去掉 markdown 表自身的表头行，调用方会另写表头


def build_fods(data_dir, run_dir):
    data_dir, run_dir = Path(data_dir), Path(run_dir)
    ingest = json.loads((run_dir / 'ingest' / 'manifest.json').read_text(encoding='utf-8'))
    cleaning = json.loads((run_dir / 'cleaning-v2' / 'manifest.json').read_text(encoding='utf-8'))
    report = (run_dir / 'cleaning-v2' / '质量报告.md').read_text(encoding='utf-8')

    sheets = []

    overview = [['项目', '内容'],
                ['项目名称', '东软电动汽车充电桩应用管理平台（第二阶段 · 大数据部分）'],
                ['小组', '第4组'],
                ['数据集名称', '充电桩充电会话 / 站点资料 / 电池样本数据集（郑州）'],
                ['原始数据来源', '课程提供数据集（共享目录 大数据开发/04.数据集最终版）'],
                ['文件清单', '、'.join(name for name, _, _ in RAW_FILES)],
                ['数据规模', '会话 3395 行 × 22 列；站点 105 行 × 8 列；电池 1594 行 × 11 列'],
                ['采集合计', f"{ingest['total_rows']} 行（课程 CSV 5094 + 平台业务库 289 + 运行日志 187 + 设备与事件流 85）"],
                ['清洗规则版本', Text(cleaning['rule_version'])],
                ['保留与隔离', '105 站点 / 3395 会话 / 1594 电池全部保留，隔离 0 条'],
                ['保留电量合计', f"{cleaning['total_kwh']} kWh"],
                ['费用字段说明', f"原始费用字段合计 {cleaning['fee_total_source_unverified']}，币种与收费语义未核实，不称营收；"
                                 f"另按项目设定的参考电价估算 {cleaning['fee_total_estimated_model']}，两个数分列呈现"],
                ['时间字段说明', '源数据年份为 0014/0015，无法证明真实日期，不改写、不据此发布日期趋势'],
                ['缺测处理', '83 个空白日按缺测掩码排除，不当 0；小时与星期校验未通过的行不进对应维度统计'],
                ['隐私处理', 'user_id 仅保留在明细层，对外接口与页面只出聚合结果，用户行为表使用哈希键'],
                ['关联性说明', '电池样本与会话表不建立关联：esd 与 sessionId 数值重合不构成业务证据'],
                ['整理时间', '2026-09-18'],
                ['整理依据', 'build-stage2/run-defense-20260918-180325 的采集清单、质量报告、清洗清单与数仓产物']]
    sheets.append(('说明', overview))

    for name, sheet_name, _ in RAW_FILES:
        rows = read_csv(data_dir / name)
        if len(rows) > SHEET_LIMIT_ROWS:
            raise SystemExit(f'{name} 有 {len(rows)} 行，超过 xls 的 65536 行上限')
        sheets.append((sheet_name, rows))

    sheets.append(('字段说明', [['文件', '字段', '类型', '含义', '单位与取值', '备注']] + [list(row) for row in FIELDS]))

    manifest_rows = [['来源类型', '来源', '表 / 文件', '行数', '列数', 'SHA256']]
    type_labels = {'db': '业务数据库', 'file': '批量数据文件', 'log': '应用日志', 'stream': '设备与事件流'}
    for source in ingest['sources']:
        for output in source.get('outputs', []):
            manifest_rows.append([type_labels.get(source['type'], source['type']), source.get('name', ''),
                                  output.get('table', ''), output.get('rows', ''),
                                  len(output.get('columns', [])), output.get('sha256', '')])
    manifest_rows.append([])
    manifest_rows.append(['清洗对账', '数据集', '输入行数', '保留行数', '隔离行数', ''])
    for key, label in (('stations', '站点'), ('sessions', '会话'), ('battery', '电池')):
        item = cleaning['reconciliation'][key]
        manifest_rows.append(['', label, item['input'], item['retained'], item['quarantined'], ''])
    manifest_rows.append([])
    manifest_rows.append(['逐规则影响', '表', '原因', '数量', '动作', ''])
    for row in markdown_table(report, '## 逐规则影响数量'):
        manifest_rows.append(['', row[0], row[2], row[3], row[1], ''])
    manifest_rows.append([])
    manifest_rows.append(['输入文件 SHA256', '文件', '摘要', '', '', ''])
    for name, digest in sorted(cleaning['input_sha256'].items()):
        manifest_rows.append(['', name, digest, '', '', ''])
    sheets.append(('采集与清洗清单', manifest_rows))

    tables = ''.join(sheet_xml(name, rows, max(len(row) for row in rows)) for name, rows in sheets)
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<office:document '
            'xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
            'xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" '
            'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
            'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
            'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" '
            'office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.spreadsheet">'
            '<office:automatic-styles>'
            '<style:style style:name="ce-header" style:family="table-cell">'
            '<style:table-cell-properties fo:background-color="#dbe5f1" '
            'fo:border="0.5pt solid #8ea9db"/>'
            '<style:text-properties fo:font-weight="bold"/>'
            '</style:style>'
            '</office:automatic-styles>'
            f'<office:body><office:spreadsheet>{tables}</office:spreadsheet></office:body>'
            '</office:document>\n')


def convert(source, target_extension, outdir):
    subprocess.run(['soffice', '--headless',
                    f'-env:UserInstallation=file://{Path(outdir) / "profile"}',
                    '--convert-to', target_extension, '--outdir', str(outdir), str(source)],
                   check=True, capture_output=True, timeout=600)
    produced = Path(outdir) / (Path(source).stem + f'.{target_extension.split(":")[0]}')
    if not produced.exists():
        raise SystemExit(f'转换失败：没有生成 {produced}')
    return produced


def read_workbook(path):
    """读 .xlsx：返回 {工作表名: 行数}。"""
    ns = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'
    with zipfile.ZipFile(path) as archive:
        workbook = ET.fromstring(archive.read('xl/workbook.xml'))
        names = [sheet.get('name') for sheet in workbook.iter(ns + 'sheet')]
        sheets = {}
        for index, name in enumerate(names, start=1):
            sheet = ET.fromstring(archive.read(f'xl/worksheets/sheet{index}.xml'))
            sheets[name] = len(list(sheet.iter(ns + 'row')))
    return sheets


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', required=True, help='课程数据集目录')
    parser.add_argument('--run', required=True, help='全链路运行目录（取采集与清洗清单）')
    parser.add_argument('--out', required=True, help='输出的 .xls 路径')
    parser.add_argument('--keep-fods', action='store_true', help='保留中间 .fods 便于排查')
    args = parser.parse_args(argv)

    if not shutil.which('soffice'):
        raise SystemExit('需要 LibreOffice（soffice）来做 .fods → .xls 转换')
    output = Path(args.out).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    work = Path(tempfile.mkdtemp(prefix='dataset-xls-'))
    try:
        fods = work / '数据集-第4组.fods'
        fods.write_text(build_fods(args.data, args.run), encoding='utf-8')
        produced = convert(fods, 'xls', work)
        shutil.copy2(produced, output)
        if args.keep_fods:
            shutil.copy2(fods, output.with_suffix('.fods'))

        check = convert(output, 'xlsx', work)
        sheets = read_workbook(check)
        expected = {'说明': None, '充电会话（nvv2t）': 3396, '站点资料（nvv2t_md_end）': 106,
                    '电池样本（dsv13r2）': 1595, '字段说明': None, '采集与清洗清单': None}
        problems = []
        for name, want in expected.items():
            if name not in sheets:
                problems.append(f'缺工作表 {name}')
            elif want is not None and sheets[name] != want:
                problems.append(f'{name} 行数 {sheets[name]}，期望 {want}')
        print(f'生成：{output}（{output.stat().st_size / 1024:.0f} KB）')
        for name, count in sheets.items():
            print(f'  {name}：{count} 行')
        if problems:
            raise SystemExit('校验未通过：' + '；'.join(problems))
        print('校验通过：工作表与行数符合预期')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
