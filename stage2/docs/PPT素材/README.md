# PPT 素材来源与重拍方式

这些 PNG 都是真实运行结果，不做后期改数。改了模型或数仓定义之后，必须按下面的方式重拍，
不要让图里的数字和报告对不上。

| 文件 | 内容 | 生成方式 | 最近重拍 |
|---|---|---|---|
| `01-数据采集.png` | 采集四类数据源 5655 行 | `stage2/ingest/collect.py` 输出截屏 | 2026-09-16 |
| `02-数据清洗.png` | 清洗六步与总账 | `stage2/cleaning/clean.py` 输出截屏 | 2026-09-16 |
| `03-分层存储.png` | 分层建仓与预测层落库 | `tools/shot_terminal.cpp` + `证据输入/03-分层存储.txt` | 2026-09-18 |
| `04-数据分析.png` | 描述统计 / 相关 / 聚类 / 回归 | `stage2/analysis/analysis.py` 输出截屏 | 2026-09-16 |
| `05-数据预测.png` | 多跨度滚动评估与采纳规则 | `tools/shot_terminal.cpp` + `证据输入/05-数据预测.txt` | 2026-09-18 |
| `06-即席查询.png` | 数仓即席 SQL 查询 | `stage2/analysis/query.py` 输出截屏 | 2026-09-16 |
| `07-接口实测.png` | 9 个只读接口逐个实测 | `tools/shot_terminal.cpp` + `证据输入/07-接口实测.txt` | 2026-09-18 |
| `08-大屏-运营概览.png` | 大屏总览（七维筛选） | `bash stage2/defense_demo.sh --selftest` 导出 | 2026-09-18 |
| `09-大屏-筛选联动.png` | 平台 iOS + 仅正电量（3395 → 2195） | 同上 | 2026-09-18 |
| `10-大屏-预测.png` | 预测视图：24 小时负荷 + 分位数 + 站点画像 | 同上 | 2026-09-18 |
| `11-大屏-电池样本.png` | 电池样本（独立数据，不做 JOIN） | 同上 | 2026-09-18 |
| `12-大屏-数据质量.png` | 数据来源与质量标签 | 同上 | 2026-09-18 |

## 重拍终端类素材（03 / 05 / 07）

1. 跑一遍链路，拿到新的数仓、模型与报告：

   ```bash
   RUN=build-stage2/run-$(date +%Y%m%d-%H%M%S) START_DASHBOARD=0 bash stage2/run_all.sh
   ```

2. 用真实输出更新 `证据输入/*.txt`（只放真实命令输出或报告原文，不改数字），然后渲染：

   ```bash
   mkdir -p build-stage2-tools/pro-shot
   # pro 文件见本目录说明，或复用 build-stage2-tools/pro-shot/shot.pro
   cd build-stage2-tools/pro-shot && qmake6 shot.pro && make -j2
   ./shot_terminal "③ 分层存储：ODS/DWD/DWS/ADS 与对账" \
     ../../../stage2/docs/PPT素材/03-分层存储.png \
     ../../../stage2/docs/PPT素材/证据输入/03-分层存储.txt
   ```

3. 接口实测素材需要先起服务，再抓真实响应：

   ```bash
   python3 stage2/dashboard/server.py --data <清洗结果> --port 8765 \
     --model <run>/model.json --model-v2 <run>/model_v2.json
   env -u LD_PRELOAD curl -s http://127.0.0.1:8765/api/health
   ```

## 重拍大屏素材（08–12）

```bash
bash stage2/defense_demo.sh --selftest --out /tmp/defense-selftest
```

导出顺序与上表一致；把 08–12 五个文件按同名覆盖即可。演示脚本默认读取
`build-stage2/run-defense/`（含 `model.json` 与 `model_v2.json`），缺失时自动生成。
