#!/usr/bin/env python3
"""生成第二阶段提交包：源码包、数据集包、测试证据包与 SHA256 校验清单。

用法：
  python3 tools/package_stage2.py \
      --data /home/bit/vmware_share/大数据开发/04.数据集最终版 \
      --cleaning /home/bit/vmware_share/大数据开发/06.清洗结果_20260916_v2 \
      [--run build-stage2/run-xxx]

约束（与第一阶段一致）：必须在干净提交上打包，产物写入 build-stage2-delivery/<短提交>/。
"""
import argparse
import hashlib
import io
import json
import re
import subprocess
import sys
import tarfile
from pathlib import Path

SUITES = ['stage2/cleaning/tests', 'stage2/ingest/tests', 'stage2/analysis/tests',
          'stage2/predict/tests', 'stage2/dashboard/tests']


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 16), b''):
            digest.update(block)
    return digest.hexdigest()


def add_tree(archive, root, prefix):
    root = Path(root)
    for path in sorted(root.rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:
            archive.add(path, arcname=str(Path(prefix) / path.relative_to(root)))


def run_suites(root, evidence_dir):
    results = []
    evidence_dir.mkdir(parents=True, exist_ok=True)
    for suite in SUITES:
        completed = subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', suite, '-v'],
                                   cwd=root, text=True, capture_output=True)
        combined = completed.stdout + completed.stderr
        log = evidence_dir / (suite.replace('/', '_') + '.log')
        log.write_text(combined, encoding='utf-8')
        ran = re.search(r'Ran (\d+) tests?', combined)
        results.append(dict(suite=suite, exit_code=completed.returncode,
                            tests=int(ran.group(1)) if ran else combined.count('... ok'),
                            passed=combined.count('... ok'), failed=combined.count('... FAIL'),
                            log=str(log.relative_to(root))))
    return results


def source_archive(root, revision, output):
    payload = subprocess.check_output(['git', 'archive', '--format=tar', '--prefix=stage2/',
                                       revision, 'stage2'], cwd=root)
    with tarfile.open(fileobj=io.BytesIO(payload), mode='r:') as original:
        with tarfile.open(output, 'w:gz') as delivered:
            for member in original:
                delivered.addfile(member, original.extractfile(member) if member.isfile() else None)
            marker = (revision + '\n').encode()
            info = tarfile.TarInfo('stage2/SOURCE_REVISION')
            info.size = len(marker)
            info.mode = 0o644
            delivered.addfile(info, io.BytesIO(marker))


def dataset_archive(data, cleaning, output):
    with tarfile.open(output, 'w:gz') as archive:
        add_tree(archive, data, '数据集-第4组/原始数据')
        add_tree(archive, cleaning, '数据集-第4组/清洗结果')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', required=True, help='课程原始数据集目录')
    parser.add_argument('--cleaning', required=True, help='清洗结果目录')
    parser.add_argument('--run', help='全链路运行目录（可选，一并打包分析/预测产物）')
    parser.add_argument('--out', help='输出目录，默认 build-stage2-delivery/<短提交>')
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=root).strip():
        raise SystemExit('请先提交变更：交付包必须来自干净提交')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    output = Path(args.out) if args.out else root / 'build-stage2-delivery' / revision[:8]
    output.mkdir(parents=True, exist_ok=True)

    results = run_suites(root, output / 'evidence')
    if any(item['exit_code'] for item in results):
        raise SystemExit('测试未全部通过，终止打包：' +
                         ', '.join(f"{i['suite']}({i['exit_code']})" for i in results if i['exit_code']))

    source = output / '小组代码-第4组-第二阶段源码.tar.gz'
    source_archive(root, revision, source)
    dataset = output / '数据集-第4组.tar.gz'
    dataset_archive(args.data, args.cleaning, dataset)
    evidence = output / '测试用例-第4组.tar.gz'
    with tarfile.open(evidence, 'w:gz') as archive:
        add_tree(archive, output / 'evidence', '测试用例-第4组/执行日志')
        add_tree(archive, root / 'stage2' / 'ingest' / 'tests', '测试用例-第4组/采集测试')
        add_tree(archive, root / 'stage2' / 'analysis' / 'tests', '测试用例-第4组/分析测试')
        add_tree(archive, root / 'stage2' / 'predict' / 'tests', '测试用例-第4组/预测测试')
        add_tree(archive, root / 'stage2' / 'dashboard' / 'tests', '测试用例-第4组/大屏测试')
        add_tree(archive, root / 'stage2' / 'cleaning' / 'tests', '测试用例-第4组/清洗测试')
    if args.run:
        with tarfile.open(output / '分析预测产物-第4组.tar.gz', 'w:gz') as archive:
            for name in ('analysis.json', '分析报告.md', 'model.json', '预测评估.md',
                         '数据字典.md', 'warehouse_manifest.json'):
                path = Path(args.run) / name
                if path.exists():
                    archive.add(path, arcname=f'分析预测产物-第4组/{name}')

    manifest = dict(commit=revision, delivery='stage2-source-dataset-tests',
                    tests=results, files={})
    for path in sorted(output.iterdir()):
        if path.is_file() and path.name != 'manifest.json':
            manifest['files'][path.name] = sha256(path)
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                          encoding='utf-8')
    print(json.dumps({'output': str(output), 'tests': results,
                      'files': list(manifest['files'])}, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
