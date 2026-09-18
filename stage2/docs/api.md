# 第二阶段接口文档（RESTful API）

服务：`python3 stage2/dashboard/server.py --data <清洗结果目录> [--model <model.json>] [--model-v2 <model_v2.json>] [--port 8765]`

`--model-v2` 指向第二阶段机器学习重构后的模型产物（负荷 1/6/24 小时 + 单次分位数 + 站点画像 + 会话分类树）。
两个参数可以同时给：`/api/forecast` 优先用 v2 产物，未加载 v2 时退回旧版单跨度模型。

- 监听 `127.0.0.1`，不对外网暴露；只读，不写任何业务数据。
- 统一响应体：`{"code": 0, "data": {...}}`；参数错误返回 400（`{"code": 400, "message": "..."}`），未知接口返回 404。
- 数据契约被破坏（缺列、规则版本不符、对账不一致、文件被改）返回 **503**（`{"code": 503, "message": "..."}`），
  消息含具体缺失项与修复命令；不降级、不补默认值、不退回演示数据。
- 重复参数、非法枚举、未知筛选项一律拒绝，不静默忽略。
- 服务启动时按 `stage2/common/contract.py` 校验规则版本、必需列、行数、SHA256 与总量对账，校验失败**拒绝启动**并打印原因。
- 前端为 Vue3 构建产物（`stage2/dashboard/web/dist`）；未构建时服务拒绝启动并给出构建命令。

| 接口 | 参数 | 说明 |
|---|---|---|
| `GET /api/health` | 无 | 服务状态与数据版本（manifest 哈希前 16 位）、`forecast_ready` 与 `classification_ready`（是否加载了对应的模型产物段） |
| `GET /api/options` | 无 | 筛选枚举（站点、桩类型、峰谷时段、平台、星期、电量）、站名映射、取值说明表、清洗对账与总量 |
| `GET /api/overview` | `station_id`、`facility_type`、`time_period`、`platform`、`weekday`、`energy`（all/positive/zero） | 交集筛选后的 KPI、七维聚合、时长分箱、质量标签、覆盖说明 |
| `GET /api/battery` | 无 | 独立电池样本：SOC/电流/温度分布 + 匿名 SOC-电压散点（不受会话筛选影响） |
| `GET /api/report` | `kind`（station_id/facility_type/time_period/platform/weekday/start_hour/day_type）+ 上述筛选 | 按维度导出当前筛选的 CSV 报表（UTF-8 BOM，可直接用 Excel 打开） |
| `GET /api/forecast` | v2：`base`（sessions/kwh）、`horizon`（1/6/24）；旧版：`weekday`、`horizon`（1–24） | 未来一天的逐小时预测、上线的预测器与对照基线、报告集指标（v2 需 `--model-v2`） |
| `GET /api/session-quantiles` | `target`（duration_hours/kwh）、`facility`、`period`、`hour`（0–23）、`platform`、`weekend`（0/1）、`station`（可选） | 单次充电时长/电量的 P10/P50/P90，附经验覆盖率与校准偏差 |
| `GET /api/stations` | `limit`（1–105，默认 20） | 站点画像：聚类结果、繁忙度分档、Top 站点与分半一致率 |
| `GET /api/model-options` | 无 | 单次分位数接口的合法取值（设施类型、峰谷时段、平台、分位档），供前端下拉框取用 |
| `GET /api/classification` | `facility`、`period`、`hour`（0–23，必填）、`platform`、`weekend`（0/1）、`station`（可选） | 会话级长时长占用预警：这次会话的概率与"提前提示/不需要额外动作"判定、落点叶子规则、校准区间、24 小时概率曲线、方法对照表与分段历史比例 |
| `GET /api/station-hour` | 与总览相同的筛选项 + `limit`（1–50，默认 20） | 站点 × 开始小时热力图：按会话数取前 N 个站点，行是站点、列是 0–23 点，空格子是 0 |

## 字段定义

- `summary.sessions`：会话次数（含零电量）；`positive_energy`：正电量会话次数。
- `summary.total_kwh`：原始合法电量合计（Decimal 求和，JSON 以字符串返回，避免浮点长尾）。
- `summary.source_fee_unverified`：原字段费用合计，**币种与语义未核实，不是营收**。
- `summary.estimated_fee_model`：`电量 × 时段单价` 的**按项目设定电价估算的派生值**（高峰 1.5 / 平时 1.0 / 低谷 0.7 元/kWh），
  与原费用字段分列、不相加，**不是真实收费或营收**。
- `summary.mean_*`：空集合返回 `null`，不伪造 0。
- `dimensions.*[]`：七个维度（`station_id`/`facility_type`/`time_period`/`platform`/`weekday`/`start_hour`/`day_type`），
  每个分桶包含 `key`、`label` 与同一套统计字段；小时按 0–23 补零，星期按周一至周日，时段按高峰→平时→低谷。
- `time_period` 取值为 `peak`/`normal`/`off_peak`（接口层统一英文，中文标签只在 `label`）。
- `coverage`：全量/当前筛选会话数、站点数、电池记录数，以及小时、星期、时段的可用条数，用于说明各图合计可能不同的原因。
- `options.assumptions`：时段小时、时段中文标签、电价、桩类型码表、工作日/周末标签，前端脚注由此自动生成，避免界面写死数字。

## 示例

```bash
# 健康检查
curl -s http://127.0.0.1:8765/api/health

# iOS 平台正电量会话概览
curl -s 'http://127.0.0.1:8765/api/overview?platform=ios&energy=positive'

# 高峰时段 + 直交流一体（编码 3）的概览
curl -s 'http://127.0.0.1:8765/api/overview?time_period=peak&facility_type=3'

# 导出开始小时维度报表
curl -s -o report.csv 'http://127.0.0.1:8765/api/report?kind=start_hour'

# 导出峰谷时段维度报表
curl -s -o report-period.csv 'http://127.0.0.1:8765/api/report?kind=time_period'

# 周二 24 小时预测
curl -s 'http://127.0.0.1:8765/api/forecast?weekday=Tue&horizon=24'

# v2：未来 24 小时的电量预测（含上线方法与指标）
curl -s 'http://127.0.0.1:8765/api/forecast?base=kwh&horizon=24'

# 直流站高峰时段 9 点的单次时长分位数
curl -s 'http://127.0.0.1:8765/api/session-quantiles?target=duration_hours&facility=直流&period=peak&hour=9'

# 站点画像与繁忙度（前 10 个站点）
curl -s 'http://127.0.0.1:8765/api/stations?limit=10'

# 交流桩高峰时段 9 点的长时长占用预警
curl -s 'http://127.0.0.1:8765/api/classification?facility=交流&period=peak&hour=9'

# 带站点：用该站点的历史时长中位数细化概率（站点样本少时自动向全局退让）
curl -s 'http://127.0.0.1:8765/api/classification?facility=交流&period=peak&hour=9&station=171000000'

# 站点 × 小时热力图（前 20 个站点）
curl -s 'http://127.0.0.1:8765/api/station-hour?limit=20'
```

## 预测类接口的取值说明

- `hours[].predicted`：未来一天的逐小时预测值。`day_index` 是**相对天数**，源年份字段不可信，接口不声称真实日历日期。
- `method`：该任务实际上线的预测器。基线胜出时如实返回基线（例如 `moving_average`），不假装所有格子都用模型。
- `metrics`：报告集（未参与挑方法的后 30% 测试日）上的指标；`baseline_metrics` 是对照基线在同一时段的指标。
- `quantiles`：键为 `0.1`/`0.5`/`0.9`；`coverage` 是测试期上"实际值不超过预测分位数"的比例，理想值等于该分位；
  `calibration_gap` 是它与名义分位的偏差（绝对值）。偏差超过 0.05 说明这个分位数没校准好。
- `stations.clusters[].profile`：7 维画像特征；`tiers.counts` 是繁忙度各档的站点数；`sparse` 是样本不足、未参与聚类的站点数。
- `classification.lookup`：这一次会话的预警结果。`probability` 是**上线方法**给出的概率（`method` 字段写明是哪一支），
  `rule_rate` 与 `tree_rate` 是两条对照值——上线经验规则时 `probability` 等于 `rule_rate`，上线树时等于 `tree_rate`；
  `decision`=1 表示概率达到 `threshold`（阈值在每个训练窗口内按 F1 选，随模型产物一起存下来）。
- `classification.methods`：四个候选方法在测试块上的平均精度（PR-AUC）、ROC-AUC、准确率、精确率、召回率、F1 与 Brier。
  采纳要求模型 F1 领先最佳基线 10%，且平均精度不低于最佳基线；不满足时如实上线经验规则，不把树说成赢了。
- `classification.f1_interval`：F1 差（CART − 最佳基线）的自助法 95% 区间。两个方法在同一组重采样下标上比较，
  种子固定（`seed`），`excludes_zero` 说明这个差是稳定方向还是抽样噪声，`share_better` 是树胜出的重采样占比。
  报告区间而不是只报一个点估计，是为了让"树差多少"这句话可复核。
- `classification.curve`：同一设施类型与峰谷时段下 0–23 点的概率；`probability` 是上线方法，`tree_rate` 是树的对照曲线。
  上线查表规则时曲线是常数——这是事实，不做美化。`segments` 是设施类型 × 峰谷时段的历史样本量与长时长比例。
- `classification.lookup.occupancy`：这次判定用到的"扫码时该站占用"三个数（过去 2 小时开单数 `recent`、
  估计仍在占用数 `active`、过去 24 小时开单数 `day`）。大屏服务没有实时会话流，这三个数取自训练窗口内
  **该站点该小时的平均占用**（`source` 字段写明来源，`samples` 是参与的会话数）；在充会话的结束时间用
  站点历史中位时长估算。接入实时数据后同一字段可直接换成实测值，接口形状不变。
- `station-hour.data`：三元组 `[小时下标, 站点下标, 会话数]`，站点下标与 `stations` 数组同序，
  直接喂给 ECharts 的 heatmap；没有会话的格子是 0，不是缺数据。`stations[].label` 是站名（缺失时退化成站点 ID），
  `max_sessions` 就是色阶上界。只统计开始小时可用的记录，与总览页一致。

## 与第一阶段的关系

第一阶段 Qt 平台（用户端/服务端/SQLite/TCP）继续独立运行，本服务只读第一阶段演示库导出的副本与课程数据集，
不会写入 Qt 业务库；两套数据的站点范围不同（北京演示站 vs 郑州课程数据集），不做合并统计。
