#!/usr/bin/env bash
# 第二阶段全链路：采集 -> 清洗 -> 分层存储 -> 分析 -> 预测 -> 大屏
#
# 用法：
#   bash stage2/run_all.sh                      # 全量跑一遍，输出到 build-stage2/run-<时间戳>
#   RUN=build-stage2/run-demo bash stage2/run_all.sh
#   CLEANING=/path/to/已清洗结果 bash stage2/run_all.sh   # 跳过清洗，复用已有清洗产物
#
# 说明：本脚本只使用 Python 标准库与 SQLite，不需要 Hadoop/Spark/Kafka 运行时；
# 各环节与大数据组件的等价关系见 stage2/docs/architecture.md。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE2="$ROOT/stage2"
DATA="${DATA:-/home/bit/vmware_share/大数据开发/04.数据集最终版}"
BUSINESS_DB="${BUSINESS_DB:-$ROOT/build-demo/chargingpile.db}"
LOG1="${LOG1:-$ROOT/build-demo/PcServer.log}"
LOG2="${LOG2:-$ROOT/build-demo/UserClient.log}"
RUN="${RUN:-$ROOT/build-stage2/run-$(date +%Y%m%d-%H%M%S)}"
CLEANING="${CLEANING:-}"

echo "== 运行目录：$RUN"
mkdir -p "$RUN"

echo "== 1/6 采集（业务库 + 批量文件 + 日志 + 设备与事件流）"
python3 "$STAGE2/ingest/collect.py" \
  --db "$BUSINESS_DB" --csv "$DATA" --log "$LOG1" --log "$LOG2" --out "$RUN/ingest"

if [ -z "$CLEANING" ]; then
  echo "== 2/6 清洗（六步流程：探查→评估→规则→执行→校验→报告）"
  python3 "$STAGE2/cleaning/clean.py" --input "$DATA" --output "$RUN/cleaning"
  CLEANING="$RUN/cleaning"
else
  echo "== 2/6 清洗：复用已有清洗结果 $CLEANING"
fi

echo "== 3/6 分层存储（ODS/DWD/DWS/ADS -> SQLite 数仓）"
python3 "$STAGE2/warehouse/load.py" --data "$CLEANING" --ingest "$RUN/ingest" \
  --db "$RUN/warehouse.db" --dictionary "$RUN/数据字典.md" --manifest "$RUN/warehouse_manifest.json"

echo "== 4/6 数据分析（描述统计、相关性、聚类、回归、季节性、异常检测）"
python3 "$STAGE2/analysis/analysis.py" --db "$RUN/warehouse.db" \
  --out "$RUN/analysis.json" --report "$RUN/分析报告.md"

echo "== 5/6 数据预测（多跨度岭回归 + 基线对照，扩展窗口滚动前进）"
python3 "$STAGE2/predict/train_v2.py" --db "$RUN/warehouse.db" \
  --out "$RUN/model_v2.json" --report "$RUN/预测评估_v2.md"
echo "   旧版单跨度模型（大屏仍在用，P4 切换后移除）"
python3 "$STAGE2/predict/train.py" --db "$RUN/warehouse.db" \
  --out "$RUN/model.json" --report "$RUN/预测评估.md"

echo "== 6/6 大屏服务（Ctrl+C 结束）"
echo "   启动命令：python3 stage2/dashboard/server.py --data $CLEANING --model $RUN/model.json"
if [ ! -f "$STAGE2/dashboard/web/dist/index.html" ]; then
  echo "   ⚠ 未找到前端构建产物，服务会拒绝启动。请先执行："
  echo "     cd stage2/dashboard/web && npm install && npm run build"
fi
if [ "${START_DASHBOARD:-1}" = "1" ]; then
  exec python3 "$STAGE2/dashboard/server.py" --data "$CLEANING" --model "$RUN/model.json"
fi
