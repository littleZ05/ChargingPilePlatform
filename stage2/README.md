# 第二阶段：充电数据大数据链路与大屏

第4组《东软电动汽车充电桩应用管理平台》。本目录是第二阶段全部交付内容，与第一阶段 Qt 平台解耦：
只读课程数据集与平台导出的演示库副本，不修改第一阶段任何代码或数据。

## 一条命令跑通全链路

```bash
bash stage2/run_all.sh                 # 采集 → 清洗 → 分层存储 → 分析 → 预测 → 启动大屏
START_DASHBOARD=0 bash stage2/run_all.sh   # 只跑数据链路，不起服务
```

产物写入 `build-stage2/run-<时间戳>/`，包含采集清单、清洗结果、数仓、数据字典、分析报告、模型与预测评估。

## 七大环节对应模块

| 环节 | 模块 | 运行方式 |
|---|---|---|
| 1 采集（业务库/日志/文件/设备事件流） | `ingest/collect.py` | `python3 stage2/ingest/collect.py --db build-demo/chargingpile.db --csv <数据集目录> --log <日志> --out <目录>` |
| 2 清洗（六步流程与质量报告） | `cleaning/clean.py` | `python3 stage2/cleaning/clean.py --input <数据集目录> --output <新目录>` |
| 3 分层存储（ODS/DWD/DWS/ADS） | `warehouse/load.py` | `python3 stage2/warehouse/load.py --data <清洗目录> --ingest <采集目录> --db warehouse.db` |
| 4 分析（统计/相关/聚类/回归/异常） | `analysis/analysis.py`、`analysis/query.py` | 见 `docs/architecture.md` |
| 5 可视化（ECharts 大屏与报表） | `dashboard/` | `python3 stage2/dashboard/server.py --data <清洗目录> --model <model.json>` |
| 6 预测（季节基线 + 线性回归） | `predict/train.py`、`predict/forecast.py` | `python3 stage2/predict/train.py --db warehouse.db --out model.json` |
| 7 业务应用（RESTful API、预警、集成） | `dashboard/server.py` | 接口见 `docs/api.md` |

## 测试

```bash
python3 -m unittest discover -s stage2/cleaning/tests -v     # 4 项
python3 -m unittest discover -s stage2/ingest/tests -v       # 6 项
python3 -m unittest discover -s stage2/analysis/tests -v     # 6 项
python3 -m unittest discover -s stage2/predict/tests -v      # 6 项
python3 -m unittest discover -s stage2/dashboard/tests -v    # 5 项
```

## 文档

- [架构与七大环节映射](docs/architecture.md)
- [边界声明：做了什么、没做什么](docs/boundary.md)
- [接口文档](docs/api.md)
- [清洗规则与六步流程](cleaning/README.md)
- [给 PPT 同学的汇报指导](cleaning/PPT同学制作指导.md)

## 环境说明（重要）

当前开发机为 2 核 / 7.9 GB 的单机容器：无 JDK、无 Hadoop/Spark/Flink/Kafka/Hive、无 Docker、无 MySQL、
Python 仅标准库（无 pandas/numpy/sklearn），外网受限且无 root 权限。因此本链路以**同构轻量实现**交付，
每个环节都给出与大数据组件的对应关系与迁移路径，未部署的组件不宣称已部署，详见 [边界声明](docs/boundary.md)。

课程共享目录中自带 JDK 8u261、Hadoop 3.2.1 单机安装包与 Spark 3.4.1 虚拟机镜像；
若在组员机器上完成伪分布式部署，可补充真实 `jps`、`hdfs dfs -ls` 截屏作为附加证据。
