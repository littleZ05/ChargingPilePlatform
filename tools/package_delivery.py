#!/usr/bin/env python3
"""Produce a reproducible source delivery archive and checksums from a verified clean commit."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import shutil

root = Path(__file__).resolve().parents[1]
if subprocess.check_output(['git', 'status', '--porcelain'], cwd=root).strip():
    raise SystemExit('请先提交变更，交付包必须来自干净提交')
revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
report_path = root / 'build-verification/report-all.json'
report = json.loads(report_path.read_text())
if report['commit'] != revision or report.get('dirty') or any(x['exit_code'] for x in report['results']):
    raise SystemExit('当前提交尚无干净且全部通过的全量验证报告，请运行 tools/verify.py')
output = root / 'build-delivery' / revision[:8]
output.mkdir(parents=True, exist_ok=True)
archive = output / 'ChargingPilePlatform-source.tar.gz'
source_tar = subprocess.check_output(['git', 'archive', '--format=tar',
                                      '--prefix=ChargingPilePlatform/', revision], cwd=root)
with tarfile.open(fileobj=io.BytesIO(source_tar), mode='r:') as original:
    with tarfile.open(archive, 'w:gz') as delivered:
        for member in original:
            delivered.addfile(member, original.extractfile(member) if member.isfile() else None)
        marker = (revision + '\n').encode()
        member = tarfile.TarInfo('ChargingPilePlatform/SOURCE_REVISION')
        member.size = len(marker)
        member.mode = 0o644
        delivered.addfile(member, io.BytesIO(marker))
shutil.copy2(report_path, output / 'verification.json')
with tarfile.open(output / 'test-evidence.tar.gz', 'w:gz') as evidence:
    for path in sorted((root / 'build-verification').rglob('*')):
        if path.is_file() and (path.name in {'results.xml', 'verify.log', 'python-tools-tests.log',
                                            'charging-settled.png', 'charging-server.log',
                                            'dashboard-live.png', 'dashboard-zero-1024.png'}):
            evidence.add(path, arcname=str(path.relative_to(root / 'build-verification')))
manifest = dict(commit=revision, delivery='source-with-test-evidence',
                environment='Ubuntu 22.04 / Qt 6.2.4; Qt libraries required on target', files={})
for path in sorted(output.iterdir()):
    if path.name != 'manifest.json' and path.is_file():
        manifest['files'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
(output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
print(output)
