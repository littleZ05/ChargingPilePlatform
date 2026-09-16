#!/usr/bin/env python3
"""Local read-only course dashboard. No database writes or demo substitutions."""
import argparse
import csv
import io
import json
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from analytics import Analytics

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'predict'))
from forecast import forecast  # noqa: E402


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, analytics, forecast_model=None, **kwargs):
        self.analytics = analytics
        self.forecast_model = forecast_model
        super().__init__(*args, **kwargs)

    def list_directory(self, path):
        self.send_error(403, 'Directory listing disabled')
        return None

    def do_GET(self):
        url = urlsplit(self.path)
        if not url.path.startswith('/api/'):
            if url.path == '/' and not (Path(self.directory)/'index.html').exists():
                self.send_error(503, 'Dashboard UI not installed')
                return
            return super().do_GET()
        try:
            params = parse_qs(url.query, keep_blank_values=True)
            if any(len(v) != 1 for v in params.values()):
                raise ValueError('筛选参数不能重复')
            filters = {k: v[0] for k,v in params.items()}
            if url.path == '/api/overview':
                data = self.analytics.overview(filters)
            elif url.path == '/api/forecast':
                if not self.forecast_model:
                    raise ValueError('未加载预测模型，请用 --model 指定 model.json')
                weekday = filters.pop('weekday', None) or 'Mon'
                horizon = int(filters.pop('horizon', 24))
                if filters or horizon < 1 or horizon > 24:
                    raise ValueError('预测参数无效：仅支持 weekday 与 1–24 的 horizon')
                data = forecast(self.forecast_model, weekday, horizon)
            elif url.path == '/api/options' and not filters:
                data = self.analytics.metadata()
            elif url.path == '/api/battery' and not filters:
                data = self.analytics.battery_summary()
            elif url.path == '/api/report':
                kind = filters.pop('kind', None) or 'station_id'
                rows, _, version = self.analytics.report(kind, filters)
                buffer = io.StringIO()
                writer = csv.writer(buffer)
                columns = list(rows[0].keys()) if rows else ['key']
                writer.writerow(columns)
                writer.writerows([[row.get(column, '') for column in columns] for row in rows])
                body = buffer.getvalue().encode('utf-8-sig')
                self.send_response(200)
                self.send_header('Content-Type', 'text/csv; charset=utf-8')
                self.send_header('Content-Disposition', f'attachment; filename="report-{kind}-{version}.csv"')
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control', 'no-store')
                self.end_headers()
                self.wfile.write(body)
                return
            elif url.path == '/api/health' and not filters:
                data = {'status': 'ok', 'version': self.analytics.version}
            else:
                self.send_error(404)
                return
            body = json.dumps({'code': 0, 'data': data}, ensure_ascii=False).encode()
            status = 200
        except ValueError as error:
            body = json.dumps({'code': 400, 'message': str(error)}, ensure_ascii=False).encode()
            status = 400
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)


def create_server(data, port=8765, model=None):
    analytics = Analytics(data)
    forecast_model = json.loads(Path(model).read_text(encoding='utf-8')) if model else None
    public = Path(__file__).resolve().parent/'public'
    handler = partial(Handler, analytics=analytics, forecast_model=forecast_model, directory=str(public))
    return ThreadingHTTPServer(('127.0.0.1', port), handler)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', required=True, help='Verified cleaning output folder')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--model', help='预测模型 model.json 路径（可选，启用 /api/forecast）')
    args = parser.parse_args()
    with create_server(args.data, args.port, args.model) as server:
        print(f'第二阶段大屏：http://127.0.0.1:{server.server_port}', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
