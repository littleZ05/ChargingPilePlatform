# 第二阶段接口文档（RESTful API）

服务：`python3 stage2/dashboard/server.py --data <清洗结果目录> [--model <model.json>] [--port 8765]`

- 监听 `127.0.0.1`，不对外网暴露；只读，不写任何业务数据。
- 统一响应体：`{"code": 0, "data": {...}}`；参数错误返回 400（`{"code": 400, "message": "..."}`），未知接口返回 404。
- 重复参数、非法枚举、未知筛选项一律拒绝，不静默忽略。
- 服务启动时校验 `manifest.json` 中三张 DWD 表的 SHA256 与行数，校验失败**拒绝启动**，不退回演示数据。

| 接口 | 参数 | 说明 |
|---|---|---|
| `GET /api/health` | 无 | 服务状态与数据版本（manifest 哈希前 16 位） |
| `GET /api/options` | 无 | 筛选枚举（站点、设施编码、平台、星期、电量）与站名映射 |
| `GET /api/overview` | `station_id`、`facility_type`、`platform`、`weekday`、`energy`（all/positive/zero） | 交集筛选后的 KPI、五维聚合、时长分箱、质量标签、覆盖说明 |
| `GET /api/battery` | 无 | 独立电池样本：SOC/电流/温度分布 + 匿名 SOC-电压散点（不受会话筛选影响） |
| `GET /api/report` | `kind`（station_id/facility_type/platform/weekday/start_hour）+ 上述筛选 | 按维度导出当前筛选的 CSV 报表（UTF-8 BOM，可直接用 Excel 打开） |
| `GET /api/forecast` | `weekday`（Mon–Sun）、`horizon`（1–24） | 小时级会话量/电量预测、最空闲时段推荐、模型评估指标（需 `--model`） |

## 字段口径

- `summary.sessions`：会话次数（含零电量）；`positive_energy`：正电量会话次数。
- `summary.total_kwh`：原始合法电量合计（Decimal 求和，JSON 以字符串返回，避免浮点长尾）。
- `summary.source_fee_unverified`：原字段费用合计，**币种与语义未核实，不是营收**。
- `summary.mean_*`：空集合返回 `null`，不伪造 0。
- `dimensions.*[]`：每个分桶包含 `key`、`label` 与同一套统计字段；小时按 0–23 补零，星期按周一至周日。
- `coverage`：全量/当前筛选会话数、站点数、电池记录数，用于说明各图合计可能不同的原因。

## 示例

```bash
# 健康检查
curl -s http://127.0.0.1:8765/api/health

# iOS 平台正电量会话概览
curl -s 'http://127.0.0.1:8765/api/overview?platform=ios&energy=positive'

# 导出开始小时维度报表
curl -s -o report.csv 'http://127.0.0.1:8765/api/report?kind=start_hour'

# 周二 24 小时预测
curl -s 'http://127.0.0.1:8765/api/forecast?weekday=Tue&horizon=24'
```

## 与第一阶段的关系

第一阶段 Qt 平台（用户端/服务端/SQLite/TCP）继续独立运行，本服务只读第一阶段演示库导出的副本与课程数据集，
不会写入 Qt 业务库；两套数据的站点口径不同（北京演示站 vs 郑州课程数据集），不做合并统计。
