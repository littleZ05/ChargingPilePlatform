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
| 5 可视化（Vue3 + ECharts 大屏与报表） | `dashboard/` | `python3 stage2/dashboard/server.py --data <清洗目录> --model <model.json>` |
| 6 预测（负荷 + 单次分位数 + 站点画像 + 会话分类树 + 落库） | `predict/train_v2.py`（主入口）、`predict/sessions.py`、`predict/stations.py`、`predict/classification.py`、`predict/tree.py`、`predict/publish.py`、`predict/train.py`（旧版，待切换后移除） | `python3 stage2/predict/train_v2.py --db warehouse.db --out model_v2.json --report 预测评估_v2.md`，再 `python3 stage2/predict/publish.py --db warehouse.db --model model_v2.json` |
| 7 业务应用（RESTful API、预警、集成） | `dashboard/server.py` | 接口见 `docs/api.md` |

## 测试

```bash
python3 -m unittest discover -s stage2/cleaning/tests -v     # 7 项
python3 -m unittest discover -s stage2/ingest/tests -v       # 6 项
python3 -m unittest discover -s stage2/analysis/tests -v     # 7 项
python3 -m unittest discover -s stage2/predict/tests -v      # 111 项（时间轴/特征/基线/模型/评估/训练/单次分位数/站点画像/分类树/分类服务/落库 + 既有预测）
python3 -m unittest discover -s stage2/dashboard/tests -v    # 21 项（HTTP 用例需要能访问 127.0.0.1）
```

> 代理环境注意：本机若设置了 `LD_PRELOAD=libproxychains`，loopback 请求会被劫持，
> 大屏的 HTTP 用例会误判为"环境不允许"而跳过。请用
> `env -u LD_PRELOAD -u PROXYCHAINS_CONF_FILE python3 -m unittest discover -s stage2/dashboard/tests -v`
> 复跑，21 项会全部真实执行。

## 大屏前端（Vue3）

源码在 `stage2/dashboard/web/`，构建产物 `web/dist/` 已入库，**离线可跑、不依赖 CDN**。
五个视图：总览、站点与报表、**预测**（1/6/24 小时负荷、单次分位数、站点画像）、电池样本、数据来源与质量。

```bash
cd stage2/dashboard/web && npm install && npm run build   # 只有改前端源码时才需要重新构建
python3 stage2/dashboard/server.py --data <清洗目录> --model <model.json> --model-v2 <model_v2.json> --port 8765
```

服务只托管 `web/dist/`；未构建时启动会直接报出构建命令，不会白屏。数据契约不符时打印原因并非零退出，
前端则显示错误横幅与重跑命令，不退回演示数据。数据来源与字段定义见 [docs/数据来源与字段定义.md](docs/数据来源与字段定义.md)。

## 文档

- [架构与七大环节映射](docs/architecture.md)
- [边界声明：做了什么、没做什么](docs/boundary.md)
- [接口文档](docs/api.md)
- [清洗规则与六步流程](cleaning/README.md)
- [数据来源与字段定义（v2.0，已确认）](docs/数据来源与字段定义.md)
- [给 PPT 同学的汇报指导](cleaning/PPT同学制作指导.md)

## 环境说明（重要）

当前开发机为 2 核 / 7.9 GB 的单机容器：无 JDK、无 Hadoop/Spark/Flink/Kafka/Hive、无 Docker、无 MySQL、
Python 仅标准库（无 pandas/numpy/sklearn），外网受限且无 root 权限。因此本链路以**同构轻量实现**交付，
每个环节都给出与大数据组件的对应关系与迁移路径，未部署的组件不宣称已部署，详见 [边界声明](docs/boundary.md)。

课程共享目录中自带 JDK 8u261、Hadoop 3.2.1 单机安装包与 Spark 3.4.1 虚拟机镜像；
若在组员机器上完成伪分布式部署，可补充真实 `jps`、`hdfs dfs -ls` 截屏作为附加证据。
