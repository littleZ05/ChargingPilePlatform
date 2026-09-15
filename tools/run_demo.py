#!/usr/bin/env python3
"""Build and launch the course demo using its own database, preserving it by default."""
import argparse
from contextlib import closing
import datetime
import os
from pathlib import Path
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def prepare_database(destination, reset=False, schema=None, seed=None):
    """Construct and validate a replacement before backing up/swapping an existing DB."""
    destination = Path(destination)
    if destination.exists() and not reset:
        return None
    destination.parent.mkdir(parents=True, exist_ok=True)
    schema = schema if schema is not None else (ROOT / 'src/database/schema.sql').read_text()
    seed = seed if seed is not None else (ROOT / 'src/database/demo_seed.sql').read_text()
    fd, temporary = tempfile.mkstemp(prefix='demo-new-', suffix='.db', dir=destination.parent)
    os.close(fd)
    temporary = Path(temporary)
    try:
        with closing(sqlite3.connect(temporary)) as database:
            database.executescript(schema)
            database.executescript(seed)
            database.execute('PRAGMA user_version=3')
            if database.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
                raise RuntimeError('演示库完整性检查失败')
            if database.execute('PRAGMA foreign_key_check').fetchall():
                raise RuntimeError('演示库外键检查失败')
            database.commit()
        backup = None
        if destination.exists():
            stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
            backup = destination.with_name(destination.name + '.backup-' + stamp)
            # SQLite backup includes committed WAL contents, unlike an ordinary file copy.
            with closing(sqlite3.connect(f'file:{destination}?mode=ro', uri=True)) as old:
                with closing(sqlite3.connect(backup)) as saved:
                    old.backup(saved)
        os.replace(temporary, destination)
        return backup
    finally:
        temporary.unlink(missing_ok=True)


def check_ports(ports):
    for port in ports:
        with socket.socket() as probe:
            try:
                probe.bind(('127.0.0.1', port))
            except OSError as error:
                raise RuntimeError(f'端口 {port} 已使用，请关闭此前演示实例；不会自动终止其他程序') from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reset', action='store_true', help='备份并重置独立演示库')
    parser.add_argument('--build-only', action='store_true', help='仅编译，不启动图形程序或修改数据')
    parser.add_argument('--auto', action='store_true', help='兼容旧脚本，不等待回车')
    parser.add_argument('--no-seed', action='store_true', help='不预填演示数据，空库由程序初始化')
    parser.add_argument('--no-map', action='store_true', help='不自动打开大屏浏览器')
    parser.add_argument('--seed-only', action='store_true', help='仅准备独立演示库')
    args = parser.parse_args()
    env = dict(os.environ)
    env.pop('LD_PRELOAD', None)
    if not args.build_only and not args.seed_only:
        if not (env.get('DISPLAY') or env.get('WAYLAND_DISPLAY')):
            parser.error('请在图形桌面终端运行；无桌面可使用 --build-only')
    base = ROOT / 'build-demo'
    base.mkdir(exist_ok=True)
    database = base / 'chargingpile.db'
    if not args.build_only:
        check_ports((9999, 8890))
        # Refuse reset whenever a PcServer exists, including one on custom ports.
        if args.reset and shutil.which('pgrep'):
            if subprocess.run(['pgrep', '-x', 'PcServer'], stdout=subprocess.DEVNULL).returncode == 0:
                parser.error('重置前请先关闭PcServer；不会自动终止进程')
    if not args.seed_only:
        for project in ('pcserver/pcserver.pro', 'userclient/userclient.pro'):
            subprocess.run([sys.executable, str(ROOT / 'tools/verify.py'), '--filter', project],
                           cwd=ROOT, env=env, check=True)
    if args.build_only:
        return 0
    if not args.no_seed or args.reset:
        backup = prepare_database(database, args.reset)
        if backup:
            print(f'原数据库备份：{backup}')
    print(f'独立演示库：{database}')
    if args.seed_only:
        return 0
    env['PCSERVER_DB_PATH'] = str(database)
    env['PCSERVER_PORT'] = '9999'
    env['PCSERVER_DASH_PORT'] = '8890'
    processes = []
    try:
        for directory, binary in [('src__pcserver__pcserver', 'PcServer'),
                                  ('src__userclient__userclient', 'UserClient')]:
            executable = ROOT / 'build-verification' / directory / binary
            with (base / f'{binary}.log').open('a') as log:
                processes.append(subprocess.Popen([str(executable)], cwd=executable.parent, env=env,
                                                   stdout=log, stderr=subprocess.STDOUT, start_new_session=True))
    except Exception:
        for process in processes:
            process.terminate()
        raise
    print('管理员 admin / 123456；用户输入11位手机号。日志位于 build-demo/。')
    if not args.no_map and shutil.which('xdg-open'):
        subprocess.Popen(['xdg-open', str(ROOT / 'src/webdashboard/index.html')],
                         env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, sqlite3.Error, subprocess.CalledProcessError) as error:
        print(f'演示启动失败：{error}', file=sys.stderr)
        sys.exit(1)
