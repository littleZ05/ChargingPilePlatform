#!/usr/bin/env python3
"""Build isolated qmake projects and report actual QtTest results.

Usage: python3 tools/verify.py [--filter substring] [--jobs 4]
Builds/logs live under ignored build-verification/, never in source directories.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--filter', default='')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / 'build-verification'
    output.mkdir(exist_ok=True)
    env = dict(os.environ)
    env.pop('LD_PRELOAD', None)
    env['QT_QPA_PLATFORM'] = 'offscreen'
    # Qt tests that use application data never touch the user's existing DB.
    env['XDG_DATA_HOME'] = str(output / 'app-data')
    projects = [root / 'src/pcserver/pcserver.pro',
                root / 'src/userclient/userclient.pro']
    projects += sorted(root.glob('src/*/tests/*.pro'))
    selected = [p for p in projects if args.filter in str(p.relative_to(root))]
    if not selected:
        parser.error('filter matched no projects')
    results = []
    for project in selected:
        name = str(project.relative_to(root)).replace('/', '__')[:-4]
        build = output / name
        build.mkdir(exist_ok=True)
        log = build / 'verify.log'
        target = re.search(r'^TARGET\s*=\s*(\S+)', project.read_text(), re.M).group(1)
        started = time.monotonic()
        commands = [['qmake6', str(project)], ['make', f'-j{args.jobs}']]
        if project.parent.name == 'tests':
            commands.append([str(build / target), '-o', 'results.xml,junitxml',
                             '-o', '-,txt'])
        code = 0
        with log.open('w') as stream:
            for command in commands:
                stream.write('$ ' + ' '.join(command) + '\n')
                stream.flush()
                try:
                    code = subprocess.run(command, cwd=build, env=env, stdout=stream,
                                          stderr=subprocess.STDOUT, timeout=300).returncode
                except subprocess.TimeoutExpired:
                    code = 124
                    stream.write('TIMEOUT after 300 seconds\n')
                if code:
                    break
        result = dict(project=str(project.relative_to(root)), exit_code=code,
                      seconds=round(time.monotonic() - started, 1), log=str(log))
        results.append(result)
        print(('PASS' if code == 0 else 'FAIL') + ' ' + result['project'], flush=True)
        if code:
            print('\n'.join(log.read_text(errors='replace').splitlines()[-14:]), flush=True)
    report = dict(commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'],
                                                 cwd=root, text=True).strip(),
                  timestamp=datetime.datetime.now().astimezone().isoformat(),
                  results=results)
    report_path = output / ('report-' + (args.filter.replace('/', '_') or 'all') + '.json')
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print('Report: ' + str(report_path))
    return 1 if any(r['exit_code'] for r in results) else 0


if __name__ == '__main__':
    sys.exit(main())
