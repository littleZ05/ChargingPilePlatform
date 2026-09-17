#!/usr/bin/env python3
"""即席 SQL 查询：对第二阶段数仓执行只读查询（等价 Presto/Impala 的交互式查询）。

用法：
  python3 stage2/analysis/query.py --db warehouse.db --list
  python3 stage2/analysis/query.py --db warehouse.db --sql "select * from ads_by_start_hour limit 5"
  python3 stage2/analysis/query.py --db warehouse.db --table dws_station_hour --limit 10
安全性：只允许 SELECT/WITH 语句，禁止任何写操作。
"""
import argparse
import csv
import re
import sqlite3
import sys
from pathlib import Path

READ_ONLY = re.compile(r'^\s*(select|with)\b', re.IGNORECASE)


def render(rows, columns):
    widths = [max(len(str(column)), *(len(str(row[i])) for row in rows)) if rows else len(str(column))
              for i, column in enumerate(columns)]
    lines = [' | '.join(str(c).ljust(w) for c, w in zip(columns, widths)),
             '-+-'.join('-' * w for w in widths)]
    lines += [' | '.join(str(value).ljust(w) for value, w in zip(row, widths)) for row in rows]
    return '\n'.join(lines)


def run(database, statement=None, table=None, limit=20, as_csv=False):
    connection = sqlite3.connect(f'file:{Path(database).as_posix()}?mode=ro', uri=True)
    try:
        if statement and not READ_ONLY.match(statement):
            raise SystemExit('只允许 SELECT/WITH 只读查询')
        if statement is None:
            if not table:
                raise SystemExit('请提供 --sql 或 --table')
            statement = f'select * from "{table}" limit {int(limit)}'
        cursor = connection.execute(statement)
        columns = [d[0] for d in cursor.description]
        rows = cursor.fetchall()
    finally:
        connection.close()
    if as_csv:
        writer = csv.writer(sys.stdout)
        writer.writerow(columns)
        writer.writerows(rows)
    else:
        print(render(rows, columns))
        print(f'\n共 {len(rows)} 行')
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', required=True, help='数仓 SQLite 路径')
    parser.add_argument('--sql', help='只读 SQL 语句')
    parser.add_argument('--table', help='直接查询某张表')
    parser.add_argument('--limit', type=int, default=20, help='--table 的返回行数上限')
    parser.add_argument('--csv', action='store_true', help='以 CSV 输出')
    parser.add_argument('--list', action='store_true', help='列出全部表及行数')
    args = parser.parse_args(argv)
    if args.list:
        connection = sqlite3.connect(f'file:{Path(args.db).as_posix()}?mode=ro', uri=True)
        try:
            tables = [row[0] for row in connection.execute(
                "select name from sqlite_master where type='table' order by name")]
            rows = [(name, connection.execute(f'select count(*) from "{name}"').fetchone()[0])
                    for name in tables]
        finally:
            connection.close()
        print(render(rows, ['table_name', 'rows']))
        print(f'\n共 {len(rows)} 张表')
        return 0
    return run(args.db, args.sql, args.table, args.limit, args.csv)


if __name__ == '__main__':
    sys.exit(main())
