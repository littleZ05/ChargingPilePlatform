#!/usr/bin/env python3
"""把第二阶段全部交付内容打成一个自包含的 zip，并用解压后的副本验证它确实能跑。

用法：
  python3 tools/package_stage2_zip.py \
      --data /home/bit/vmware_share/大数据开发/04.数据集最终版 \
      --run build-stage2/run-defense-20260918-180325 \
      --out "/home/bit/vmware_share/大数据开发/第二阶段代码-第4组-20260918.zip"

包里放什么：
  stage2/          代码（取 git HEAD，排除 node_modules / __pycache__；前端 dist 已入库，开箱可跑）
  tools/           答辩演示程序源码（defense_demo.cpp）
  build-demo/      采集要用的演示库与日志（业务库 SQLite + 两份日志）
  数据集/          课程原始数据集三份 CSV（包内自带，不依赖共享目录）
  预置产物/        一次全链路的完整产物（清洗、数仓、模型、评估报告），可直接起大屏
  一键运行-全链路.sh / 一键起大屏.sh / 先读我-交付说明.md

验证（默认开启，--no-verify 可关）：解包前先在暂存目录里跑五组单测、跑一次全链路、
用新产物和预置产物各起一次大屏服务并访问接口，结果写进包里的 验证记录.md。
"""
import argparse
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

SUITES = ['stage2/cleaning/tests', 'stage2/ingest/tests', 'stage2/analysis/tests',
          'stage2/predict/tests', 'stage2/dashboard/tests']
RUN_FILES = ['analysis.json', '分析报告.md', 'model.json', 'model_v2.json', '预测评估.md',
             '预测评估_v2.md', '数据字典.md', 'warehouse.db', 'warehouse_manifest.json']
RUN_DIRS = ['cleaning-v2', 'ingest']
DEMO_FILES = ['chargingpile.db', 'PcServer.log', 'UserClient.log']


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 16), b''):
            digest.update(block)
    return digest.hexdigest()


def clean_env(extra=None):
    """绕开 proxychains：本机 LD_PRELOAD 会把 127.0.0.1 的请求劫持走，HTTP 用例会误判。"""
    env = os.environ.copy()
    for name in ('LD_PRELOAD', 'PROXYCHAINS_CONF_FILE', 'http_proxy', 'https_proxy',
                 'HTTP_PROXY', 'HTTPS_PROXY'):
        env.pop(name, None)
    env['no_proxy'] = '*'
    env['NO_PROXY'] = '*'
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    if extra:
        env.update(extra)
    return env


def maybe_reexec():
    """本机装了 proxychains（LD_PRELOAD）时，自己的回环请求也会被劫持，先用干净环境重启自己。"""
    if 'LD_PRELOAD' in os.environ or 'PROXYCHAINS_CONF_FILE' in os.environ:
        print('提示：检测到 LD_PRELOAD / proxychains，本进程用干净环境重启一次（否则 127.0.0.1 的请求会被劫持）',
              file=sys.stderr)
        os.execve(sys.executable, [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]], clean_env())


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def http_json(url, timeout=10):
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(url, timeout=timeout) as response:
        return json.loads(response.read().decode('utf-8'))


def wait_health(port, seconds=40):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            payload = http_json(f'http://127.0.0.1:{port}/api/health')
            if payload.get('data', {}).get('status') == 'ok':
                return payload['data']
        except (urllib.error.URLError, OSError, ValueError):
            pass
        time.sleep(1)
    return None


def start_server(package, run_dir, cleaning, records, label):
    port = free_port()
    log = package.parent / f'verify-server-{label}.log'
    command = [sys.executable, str(package / 'stage2' / 'dashboard' / 'server.py'),
               '--data', str(cleaning), '--model', str(run_dir / 'model.json'),
               '--model-v2', str(run_dir / 'model_v2.json'), '--port', str(port)]
    with log.open('wb') as stream:
        process = subprocess.Popen(command, cwd=package, env=clean_env(), stdout=stream,
                                   stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    try:
        health = wait_health(port)
        if not health:
            tail = log.read_text(encoding='utf-8', errors='replace')[-200:] if log.exists() else '没有日志'
            records.append((f'起大屏（{label}）', False, f'健康检查没通过：{tail}'))
            return
        detail = (f"端口 {port}；model_version={health.get('model_version')}、"
                  f"sessions={health.get('sessions')}、stations={health.get('stations')}、"
                  f"classification_ready={health.get('classification_ready')}")
        ok = bool(health.get('classification_ready'))
        overview = http_json(f'http://127.0.0.1:{port}/api/overview')['data']['summary']
        station_hour = http_json(f'http://127.0.0.1:{port}/api/station-hour?limit=3')['data']
        ok = ok and overview['sessions'] > 0 and len(station_hour['stations']) == 3
        detail += (f"；/api/overview 会话 {overview['sessions']} 条、电量 {overview['total_kwh']} kWh；"
                   f"/api/station-hour 返回 {len(station_hour['stations'])} 个站点 × {len(station_hour['hours'])} 小时")
        records.append((f'起大屏（{label}）', ok, detail))
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()


def build_staging(package, root, args, records):
    if package.exists():
        shutil.rmtree(package)
    package.mkdir(parents=True)

    # 代码：取 git HEAD 的 stage2/，保证交付内容等于某个提交
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    payload = subprocess.check_output(['git', 'archive', '--format=tar', 'HEAD', 'stage2'], cwd=root)
    archive = package / '_code.tar'
    archive.write_bytes(payload)
    subprocess.run(['tar', '-xf', str(archive), '-C', str(package)], check=True)
    archive.unlink()

    # 答辩演示程序源码（defense_demo.sh 会编译它）
    (package / 'tools').mkdir()
    shutil.copy2(root / 'tools' / 'defense_demo.cpp', package / 'tools' / 'defense_demo.cpp')

    # 采集要用的演示库与日志
    (package / 'build-demo').mkdir()
    for name in DEMO_FILES:
        shutil.copy2(root / 'build-demo' / name, package / 'build-demo' / name)

    # 课程原始数据集（包内自带）
    (package / '数据集').mkdir()
    for path in sorted(Path(args.data).glob('*.csv')):
        shutil.copy2(path, package / '数据集' / path.name)

    # 预置产物：可直接起大屏
    run_dir = package / '预置产物'
    run_dir.mkdir()
    for name in RUN_FILES:
        source = Path(args.run) / name
        if source.exists():
            shutil.copy2(source, run_dir / name)
    for name in RUN_DIRS:
        source = Path(args.run) / name
        if source.exists():
            shutil.copytree(source, run_dir / name)

    for relative, text, mode in templates(revision):
        path = package / relative
        path.write_text(text, encoding='utf-8')
        path.chmod(mode)
    (package / 'SOURCE_REVISION').write_text(revision + '\n', encoding='utf-8')
    return revision


def templates(revision):
    readme = """# 第二阶段交付包 · 第4组（充电桩应用管理平台）

解压后直接可用，**不需要装 Hadoop / Spark / Kafka / JDK，也不需要联网**：
只依赖系统自带的 `python3`（3.8 以上，标准库），大屏前端 `dist` 已经构建好放在包里。

对应提交：`@@REVISION@@`

## 目录

| 目录 | 内容 |
|---|---|
| `stage2/` | 第二阶段全部代码：采集、清洗、分层存储、分析、预测、大屏（后端 + 前端源码与构建产物）、文档、测试 |
| `tools/` | 答辩演示程序源码 `defense_demo.cpp` |
| `build-demo/` | 采集要用的演示库与日志（业务库 SQLite、PcServer 与 UserClient 日志） |
| `数据集/` | 课程原始数据集三份 CSV |
| `预置产物/` | 一次完整全链路的产物：清洗结果、SQLite 数仓、模型、评估报告、数据字典 |
| `验证记录.md` | 打包时在本机跑过的验证结果（单测、全链路、大屏接口） |

## 一、最快的方式：直接起大屏（用预置产物，约 10 秒）

```bash
bash 一键起大屏.sh            # 默认 8765 端口，可用 PORT=9000 bash 一键起大屏.sh 换端口
# 浏览器打开 http://127.0.0.1:8765
```

五个视图：总览、站点与报表（含站点 × 小时热力图）、预测（负荷 / 单次分位数 / 站点画像 / 长时长占用预警）、
电池样本、数据来源与质量。

## 二、从零跑通全链路（约 2 分钟）

```bash
bash 一键运行-全链路.sh       # 采集 → 清洗 → 分层存储 → 分析 → 预测，最后自动起大屏
# 只想跑数据不起服务：START_DASHBOARD=0 bash 一键运行-全链路.sh
```

产物写在包内 `build-stage2/run-<时间戳>/`，包含采集清单、清洗质量报告、SQLite 数仓、数据字典、
分析报告、模型与预测评估。脚本不会覆盖已有产物目录。

## 三、跑单测（165 项）

```bash
python3 -m unittest discover -s stage2/cleaning/tests    # 7
python3 -m unittest discover -s stage2/ingest/tests      # 6
python3 -m unittest discover -s stage2/analysis/tests    # 7
python3 -m unittest discover -s stage2/predict/tests     # 122
python3 -m unittest discover -s stage2/dashboard/tests   # 23（HTTP 用例要能访问 127.0.0.1）
```

> 如果机器上装了 proxychains（`LD_PRELOAD=libproxychains.so`），本机地址的请求会被劫持，
> 大屏 HTTP 用例会误判为环境不允许。用
> `env -u LD_PRELOAD -u PROXYCHAINS_CONF_FILE python3 -m unittest discover -s stage2/dashboard/tests`
> 复跑即可。

## 四、环境要求

- `python3` 3.8+（只用标准库，没有 numpy/pandas/sklearn 依赖）
- 浏览器（大屏）；想重新构建前端才需要 Node.js：`cd stage2/dashboard/web && npm install && npm run build`
- 可选：`sqlite3` 命令行（即席查询数仓）、`ffmpeg`（把录屏转 MP4）、Qt6（编译答辩演示程序）

## 五、数据边界与使用说明

- 只读课程数据集，不改写原始文件；本包不包含也不影响第一阶段的 Qt 平台。
- 83 个空白日按缺测掩码排除，不当 0；保留会话电量 19723.69 kWh，与会话数 3395 条。
- 原费用字段合计 401.52 的币种与收费语义未经核实，不称营收；包内另有按项目设定参考电价算出的估算电费。
- 站点画像只描述充电行为，不代表经营优先级；设施类型按课堂码表映射，无标签的编码保留待核。
"""
    run_sh = """#!/usr/bin/env bash
# 包内一键跑通全链路：用包里的数据集与演示库，产物写在包内 build-stage2/run-<时间戳>。
set -euo pipefail
PKG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DATA="${DATA:-$PKG/数据集}"
export BUSINESS_DB="${BUSINESS_DB:-$PKG/build-demo/chargingpile.db}"
export LOG1="${LOG1:-$PKG/build-demo/PcServer.log}"
export LOG2="${LOG2:-$PKG/build-demo/UserClient.log}"
export RUN="${RUN:-$PKG/build-stage2/run-$(date +%Y%m%d-%H%M%S)}"
exec bash "$PKG/stage2/run_all.sh"
"""
    dash_sh = """#!/usr/bin/env bash
# 包内一键起大屏：直接读预置产物，不重跑训练。
set -euo pipefail
PKG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${PORT:-8765}"
exec env -u LD_PRELOAD -u PROXYCHAINS_CONF_FILE python3 "$PKG/stage2/dashboard/server.py" \\
  --data "$PKG/预置产物/cleaning-v2" --model "$PKG/预置产物/model.json" \\
  --model-v2 "$PKG/预置产物/model_v2.json" --port "$PORT"
"""
    return [
        ('先读我-交付说明.md', readme.replace('@@REVISION@@', revision), 0o644),
        ('一键运行-全链路.sh', run_sh, 0o755),
        ('一键起大屏.sh', dash_sh, 0o755),
    ]


def verify(package, records, workdir):
    """在解压前先按解压后的用法验证一遍：单测 → 全链路 → 起大屏（新产物 + 预置产物）。"""
    for suite in SUITES:
        completed = subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', suite],
                                   cwd=package, env=clean_env(), text=True, capture_output=True,
                                   timeout=600)
        combined = completed.stdout + completed.stderr
        tail = [line for line in combined.strip().splitlines() if line.startswith(('Ran ', 'OK', 'FAILED'))]
        records.append((f'单测 {suite}', completed.returncode == 0, '；'.join(tail) or combined[-200:]))

    run_dir = workdir / 'run-verify'
    # START_DASHBOARD=0 必须给：run_all.sh 默认跑完会 exec 大屏服务并一直阻塞
    completed = subprocess.run(['bash', '一键运行-全链路.sh'], cwd=package,
                               env=clean_env({'RUN': str(run_dir), 'START_DASHBOARD': '0'}),
                               text=True, capture_output=True, timeout=1800)
    ok = completed.returncode == 0 and (run_dir / 'warehouse.db').exists()
    summary = ''
    if (run_dir / 'warehouse_manifest.json').exists():
        manifest = json.loads((run_dir / 'warehouse_manifest.json').read_text(encoding='utf-8'))
        summary = (f"数仓 {manifest['tables']} 张表 / {manifest['rows']} 行；"
                   f"模型版本 {manifest.get('prediction_model_version')}")
    records.append(('全链路（采集→清洗→数仓→分析→预测）', ok,
                    summary or (completed.stdout + completed.stderr)[-300:]))

    if ok:
        start_server(package, run_dir, run_dir / 'cleaning', records, '新产物')
    preset = package / '预置产物'
    if (preset / 'warehouse.db').exists():
        start_server(package, preset, preset / 'cleaning-v2', records, '预置产物')
    else:
        records.append(('起大屏（预置产物）', False, '预置产物缺失'))


def write_records(package, records, revision):
    lines = ['# 打包验证记录', '',
             f'- 提交：`{revision}`',
             f'- 打包时间：{time.strftime("%Y-%m-%d %H:%M:%S")}',
             f'- 环境：{sys.version.split()[0]}（{sys.platform}）',
             '',
             '| 验证项 | 结果 | 说明 |', '|---|---|---|']
    for name, ok, detail in records:
        detail = str(detail).replace('|', '/').replace('\n', ' ')
        lines.append(f'| {name} | {"通过" if ok else "未通过"} | {detail} |')
    failed = [name for name, ok, _ in records if not ok]
    lines += ['', f'合计 {len(records)} 项，通过 {len(records) - len(failed)} 项。'
              + (f'未通过：{"、".join(failed)}' if failed else '全部通过。')]
    (package / '验证记录.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return failed


def make_zip(package, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()
    relative = os.path.relpath(output, package.parent)
    subprocess.run(['zip', '-r', '-q', '-9', relative, package.name],
                   cwd=package.parent, check=True)
    subprocess.run(['unzip', '-tq', str(output)], check=True)
    return output


def main(argv=None):
    maybe_reexec()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', required=True, help='课程原始数据集目录（三份 CSV）')
    parser.add_argument('--run', required=True, help='一次全链路运行目录，作为预置产物')
    parser.add_argument('--out', required=True, help='输出的 zip 路径')
    parser.add_argument('--staging', help='暂存目录，默认放在输出 zip 同级的同名目录')
    parser.add_argument('--no-verify', action='store_true', help='跳过验证（默认会验证）')
    args = parser.parse_args(argv)

    root = Path(__file__).resolve().parents[1]
    output = Path(args.out).expanduser().resolve()
    name = output.stem
    package = Path(args.staging) if args.staging else output.parent / name
    dirty = subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()
    if dirty:
        print('提示：工作区有未提交改动，包内代码取的是 HEAD（未提交内容不会进包）', file=sys.stderr)
    for path in (args.data, args.run):
        if not Path(path).exists():
            raise SystemExit(f'找不到 {path}')

    records = []
    revision = build_staging(package, root, args, records)
    print(f'暂存目录：{package}')

    workdir = package.parent / f'.{name}-verify'
    if workdir.exists():
        shutil.rmtree(workdir)
    workdir.mkdir()
    try:
        if args.no_verify:
            records.append(('验证', True, '按 --no-verify 跳过'))
        else:
            print('开始验证：五组单测 → 全链路 → 起大屏（新产物 / 预置产物）')
            verify(package, records, workdir)
        for leftover in package.rglob('__pycache__'):
            shutil.rmtree(leftover, ignore_errors=True)
        failed = write_records(package, records, revision)
        archive = make_zip(package, output)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    for name_, ok, detail in records:
        print(f'  [{"通过" if ok else "未通过"}] {name_}：{detail}')
    print(f'\nzip：{archive}（{archive.stat().st_size / 1024 / 1024:.1f} MB）')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
