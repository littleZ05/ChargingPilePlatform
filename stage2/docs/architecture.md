# 第二阶段架构：七大环节与大数据技术栈映射

第4组《东软电动汽车充电桩应用管理平台》。第二阶段在保留第一阶段 Qt 平台（用户端/服务端/SQLite/TCP）不动的前提下，
新增一条独立的大数据链路：课程数据集与平台运行数据 → 采集 → 清洗 → 分层存储 → 分析 → 预测 → 大屏与接口。

## 一、数据流

```
业务库(chargingpile.db) ┐
应用日志(PcServer.log)  ┤   采集      ┌── ODS 原始层 ──┐
课程CSV(3份数据集)      ├───────────► │  原样留存+清单  │
设备与事件流(功率/告警) ┘  ingest     └────────┬────────┘
                                              │ 清洗 clean.py（六步）
                                       ┌──────▼──────┐
                                       │ DWD 明细层   │ 标准化、校验、标记
                                       └──────┬───────┘
                              建模（load.py） │
                    ┌─────────────┬───────────┴───────────┐
                    │ DWS 汇总层   │                       │ ADS 应用层
                    │ 站点×时段    │                       │ 大屏/接口直接消费
                    │ 设施×平台    │                       │
                    │ 星期×时段    │                       │
                    │ 用户行为(哈希)│                       │
                    └──────┬──────┘                        └────┬──────┘
                           │ 分析 analysis.py                    │ 展示 dashboard
                           │ 预测 train_v2.py                    │
                           ▼                                     ▼
                  分析报告 / 聚类 / 回归 / 异常         Web 大屏 + RESTful API
```

## 二、七大环节落地对照

| 环节 | 课程要求 | 本项目实现 | 产物 | 大数据等价物 |
|---|---|---|---|---|
| 1 数据采集 | 业务库、日志、第三方接口、传感器/设备；结构化+半结构化 | `stage2/ingest/collect.py`：SQLite 业务库全表同步、日志解析、CSV 批量接入、设备功率与告警事件流回放 | `ods/business/*`、`ods/logs/*`、`ods/files/*`、`ods/stream/*`、`manifest.json`、`采集清单.md` | Sqoop/CDC、Flume/Logstash、HDFS 批量导入、Kafka 事件流 |
| 2 数据清洗 | 去重、缺失、异常、噪声、一致性；质量报告 | `stage2/cleaning/clean.py`：六步流程、R01–R05 规则、隔离层、逐行审计 | `dwd/`、`ads/`、`audit/`、`quarantine/`、`质量报告.md`、`manifest.json` | Spark SQL / Pandas 等价实现（规则可迁移） |
| 3 数据存储 | ODS/DWD/DWS/ADS 分层；HDFS/Hive/HBase 等 | `stage2/warehouse/load.py`：四层装载进 SQLite 数仓，并建模 DWS；`stage2/predict/publish.py` 把预测结果回落成 DWS/ADS 表（日×小时面板、站点特征、繁忙度、负荷预测、单次分位数） | `warehouse.db`（49 张表）、`数据字典.md` | HDFS 分区目录、Hive/ClickHouse 表、Iceberg 快照（manifest） |
| 4 数据分析 | 离线+实时计算；统计/关联/聚类/分类/回归/时序 | `stage2/analysis/analysis.py`：描述统计、Pearson 相关、K-Means 聚类、最小二乘回归、季节性、IQR/3σ 异常；`query.py` 即席 SQL | `analysis.json`、`分析报告.md` | Spark SQL、MLlib；SQLite 即席查询对应 Presto/Impala |
| 5 数据可视化 | ECharts/Grafana/Superset；多维报表与钻取 | `stage2/dashboard/`：**Vue3 + Vite + ECharts** 离线大屏，五个视图（总览/站点与报表/预测/电池样本/数据来源与质量）、七维筛选联动（含峰谷时段）、CSV 导出；构建产物 `web/dist` 入库 | 大屏页面、`/api/report` 报表 | Vue3 + ECharts（本地打包，无 CDN） |
| 6 数据预测 | 回归/分类/集成/深度学习/时序；模型管理 | `stage2/predict/`：相对时间轴 + 缺测掩码 + 基线对照 + 滚动评估。负荷侧用滞后特征岭回归预测未来 1/6/24 小时的会话数与电量（方法选择只看前 70% 测试日、指标只报后 30%）；单次侧用分位数回归（pinball loss + IRLS）给时长/电量的 P10/P50/P90，按"每 30 天重训一次、最近 90 天窗口"评估并核对经验覆盖率；站点侧用 K-Means 聚类站点画像（k 由轮廓系数择优）＋繁忙度分档，并用分半稳定性检验归属 | `model.json`、`model_v2.json`、`预测评估.md`、`预测评估_v2.md`、`分位数评估.md`、站点画像 JSON、`/api/forecast` | 对应 sklearn/Spark MLlib 的最小可解释实现 |
| 7 业务应用 | 大屏、预警、API、微服务集成 | 大屏 + RESTful API（health/options/overview/battery/report/forecast/session-quantiles/stations/model-options）+ 自愈告警联动第一阶段平台 | API 文档 `api.md` | API 网关/微服务的单机等价 |

## 三、底层支撑体系的单机等价实现

| 课程要求组件 | 本项目实现 | 说明 |
|---|---|---|
| YARN / Mesos / Kubernetes | 单进程脚本 + SQLite | 2 核 8G 单机，不部署集群；迁移路径见 `boundary.md` |
| ZooKeeper / Airflow / DolphinScheduler | `run_all.sh` 顺序编排 + 各环节清单 | 等价任务依赖、失败即停、重复运行不覆盖证据 |
| Atlas / Hive Metastore | `manifest.json` + `数据字典.md` | 元数据、血缘（输入/输出 SHA256）与字段定义 |
| Ranger / Kerberos / LDAP | 只读连接 + user_id 哈希 + 公开只出聚合 | 最小权限与脱敏 |
| DataHub / OpenLineage | 质量报告 + 规则计数 + 行数对账 | 数据质量监控 |
| Prometheus / Grafana / ELK | 运行日志 + `/api/health` + 采集/装载清单 | 可观测性 |

## 四、可复现性

- 每个环节输出目录必须不存在才允许写入，重复运行换新目录，不覆盖既有证据。
- 采集、清洗、数仓、模型均记录 SHA256 或行数对账；DWD 三张明细表哈希在服务启动时校验，不一致直接拒绝启动。
- 算法全部使用标准库实现并配单元测试，固定随机种子，结果可复现。
- 全链路一条命令：`bash stage2/run_all.sh`。
