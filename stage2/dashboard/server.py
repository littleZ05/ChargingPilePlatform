#!/usr/bin/env python3
"""第二阶段大屏服务：只读清洗产物，Vue3 构建产物离线托管。

启动即校验数据契约（stage2/common/contract.py）；校验失败打印可读原因并以非零码退出，
不允许出现"大屏服务未就绪"这类无法定位的提示。
"""
import argparse
import csv
import io
import json
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from analytics import Analytics, ContractError
import model_api

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'predict'))
from forecast import forecast  # noqa: E402

UI_DIR = Path(__file__).resolve().parent / 'web' / 'dist'
BUILD_HINT = '请先构建前端：cd stage2/dashboard/web && npm install && npm run build'


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, analytics, forecast_model=None, model_v2=None, **kwargs):
        self.analytics = analytics
        self.forecast_model = forecast_model
        self.model_v2 = model_v2
        super().__init__(*args, **kwargs)

    def log_message(self, format, *args):  # 保持单行、便于排查
        sys.stderr.write('%s %s\n' % (self.address_string(), format % args))

    def list_directory(self, path):
        self.send_error(403, 'Directory listing disabled')
        return None

    def send_json(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlsplit(self.path)
        if not url.path.startswith('/api/'):
            if url.path == '/' and not (Path(self.directory) / 'index.html').exists():
                self.send_error(503, 'Dashboard UI not built', explain=BUILD_HINT)
                return
            return super().do_GET()
        try:
            params = parse_qs(url.query, keep_blank_values=True)
            if any(len(v) != 1 for v in params.values()):
                raise ValueError('筛选参数不能重复')
            filters = {k: v[0] for k, v in params.items()}
            if url.path == '/api/overview':
                data = self.analytics.overview(filters)
            elif url.path == '/api/station-hour':
                data = self.analytics.station_hour(filters)
            elif url.path == '/api/forecast':
                if self.model_v2:
                    data = model_api.forecast(self.model_v2, filters)
                elif self.forecast_model:
                    weekday = filters.pop('weekday', None) or 'Mon'
                    horizon = int(filters.pop('horizon', 24))
                    if filters or horizon < 1 or horizon > 24:
                        raise ValueError('预测参数无效：仅支持 weekday 与 1–24 的 horizon')
                    data = forecast(self.forecast_model, weekday, horizon)
                else:
                    raise ValueError('未加载预测模型，请用 --model-v2 指定 model_v2.json')
            elif url.path == '/api/session-quantiles':
                if not self.model_v2:
                    raise ValueError('未加载第二版模型，请用 --model-v2 指定 model_v2.json')
                data = model_api.session_quantiles(self.model_v2, filters)
            elif url.path == '/api/classification':
                if not self.model_v2:
                    raise ValueError('未加载第二版模型，请用 --model-v2 指定 model_v2.json')
                data = model_api.classification(self.model_v2, filters)
            elif url.path == '/api/stations':
                if not self.model_v2:
                    raise ValueError('未加载第二版模型，请用 --model-v2 指定 model_v2.json')
                data = model_api.stations(self.model_v2, filters)
            elif url.path == '/api/model-options' and not filters:
                if not self.model_v2:
                    raise ValueError('未加载第二版模型，请用 --model-v2 指定 model_v2.json')
                data = model_api.model_options(self.model_v2)
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
                data = {'status': 'ok', 'version': self.analytics.version,
                        'rule_version': self.analytics.rule_version,
                        'sessions': len(self.analytics.sessions),
                        'stations': len(self.analytics.stations),
                        'model_version': (self.model_v2 or {}).get('version'),
                        'forecast_ready': bool((self.model_v2 or {}).get('forecast')),
                        'classification_ready': bool((self.model_v2 or {}).get('classification'))}
            else:
                self.send_error(404)
                return
            self.send_json(200, {'code': 0, 'data': data})
        except ValueError as error:
            self.send_json(400, {'code': 400, 'message': str(error)})
        except ContractError as error:
            # 数据契约被破坏属于服务端不可用，而不是请求参数问题：如实报 503 并给修复命令。
            self.send_json(503, {'code': 503, 'message': str(error)})


def create_server(data, port=8765, model=None, model_v2=None):
    analytics = Analytics(data)                     # 校验失败会抛 ContractError，由调用方打印
    forecast_model = json.loads(Path(model).read_text(encoding='utf-8')) if model else None
    v2_model = json.loads(Path(model_v2).read_text(encoding='utf-8')) if model_v2 else None
    handler = partial(Handler, analytics=analytics, forecast_model=forecast_model,
                      model_v2=v2_model, directory=str(UI_DIR))
    server = ThreadingHTTPServer(('127.0.0.1', port), handler)
    server.analytics = analytics           # 供启动日志与测试读取，不改响应体
    return server


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', required=True, help='已校验的清洗结果目录（含 manifest.json）')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--model', help='预测模型 model.json 路径（可选，启用 /api/forecast）')
    parser.add_argument('--model-v2', dest='model_v2',
                        help='第二版模型 model_v2.json 路径（启用 1/6/24 小时负荷预测、'
                             '单次分位数与站点画像）')
    args = parser.parse_args(argv)
    if not (UI_DIR / 'index.html').exists():
        print(f'未找到前端构建产物：{UI_DIR}\n{BUILD_HINT}', file=sys.stderr)
        return 2
    try:
        server = create_server(args.data, args.port, args.model, args.model_v2)
    except ContractError as error:
        print(f'数据契约校验失败，拒绝启动大屏：\n{error}', file=sys.stderr)
        return 2
    except OSError as error:
        print(f'端口 {args.port} 无法绑定：{error}', file=sys.stderr)
        return 2
    with server:
        print(f'第二阶段大屏：http://127.0.0.1:{server.server_port}', flush=True)
        print(f'数据版本 {server.analytics.version}（rule_version {server.analytics.rule_version}）：'
              f'{len(server.analytics.sessions)} 条会话 / {len(server.analytics.stations)} 个站点；'
              f'前端 {UI_DIR}', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
