#!/usr/bin/env bash
# 第二阶段答辩演示：按顺序弹出七环节效果图与实时大屏窗口。
#
# 用法：
#   bash stage2/defense_demo.sh                # 手动推进（空格/→ 下一步，← 上一步，R 重来，Esc 退出）
#   bash stage2/defense_demo.sh --auto 25      # 每 25 秒自动进入下一个窗口
#   bash stage2/defense_demo.sh --selftest     # 自检：把 13 个窗口逐帧导出 PNG，不用于答辩现场
#
# 默认数据：共享目录已清洗结果；默认模型：build-stage2/run-defense/model.json 与 model_v2.json（缺失时自动生成）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA="${DATA:-/home/bit/vmware_share/大数据开发/06.清洗结果_20260916_v2}"
SOURCE="${SOURCE:-/home/bit/vmware_share/大数据开发/04.数据集最终版}"
RUN="${RUN:-$ROOT/build-stage2/run-defense}"
BIN="${BIN:-$ROOT/build-stage2-tools/defense_demo}"
REPO_SERVER="$ROOT/stage2/dashboard/server.py"

# 上一次运行若中断在建模之前（有 ingest 但没有数仓），run_all.sh 会按"不覆盖既有产物"拒绝重跑：
# 这时自动换一个带时间戳的运行目录，现场演示不会被上一次的残留卡住。
if [ -d "$RUN/ingest" ] && [ ! -f "$RUN/warehouse.db" ]; then
  echo "== $RUN 是上次中断的残留，改用新目录重跑"
  RUN="$RUN-$(date +%Y%m%d-%H%M%S)"
fi

MODEL="${MODEL:-$RUN/model.json}"
MODEL_V2="${MODEL_V2:-$RUN/model_v2.json}"

mkdir -p "$RUN"

# 大屏按 rule_version 2.0 的字段定义读取清洗产物（含时段与估算电费列）。
# 共享目录里若仍是旧版本结果，就从原始数据集重新清洗一份 2.0 产物，不改动共享目录。
if [ ! -f "$DATA/manifest.json" ] || ! grep -q '"rule_version": *"2.0"' "$DATA/manifest.json"; then
  CLEAN2="$RUN/cleaning-v2"
  [ -d "$CLEAN2" ] && [ ! -f "$CLEAN2/manifest.json" ] && rm -r "$CLEAN2"   # 清理上次中断的残留
  if [ ! -f "$CLEAN2/manifest.json" ]; then
    echo "== 共享清洗结果为旧版本，从原始数据集重新清洗出 rule_version 2.0 产物（$CLEAN2）"
    python3 "$ROOT/stage2/cleaning/clean.py" --input "$SOURCE" --output "$CLEAN2" >/dev/null
  fi
  DATA="$CLEAN2"
fi

if [ ! -f "$MODEL" ] || [ ! -f "$MODEL_V2" ]; then
  if [ -f "$RUN/warehouse.db" ]; then
    # 已有数仓（run_all.sh 拒绝覆盖既有产物），只补齐缺失的模型文件，不重跑采集与清洗。
    echo "== 复用已有数仓，补齐模型产物（$RUN）"
    if [ ! -f "$MODEL_V2" ]; then
      if ! python3 "$ROOT/stage2/predict/train_v2.py" --db "$RUN/warehouse.db" \
          --out "$RUN/model_v2.json" --report "$RUN/预测评估_v2.md"; then
        echo "数仓 $RUN/warehouse.db 不是当前规则版本的产物，无法直接训练；" >&2
        echo "请换一个空的 RUN 目录重跑：RUN=build-stage2/run-$(date +%Y%m%d-%H%M%S) bash stage2/defense_demo.sh" >&2
        exit 1
      fi
      python3 "$ROOT/stage2/predict/publish.py" --db "$RUN/warehouse.db" \
        --model "$RUN/model_v2.json" --dictionary "$RUN/数据字典.md"
    fi
    if [ ! -f "$MODEL" ]; then
      python3 "$ROOT/stage2/predict/train.py" --db "$RUN/warehouse.db" \
        --out "$RUN/model.json" --report "$RUN/预测评估.md"
    fi
  else
    echo "== 生成演示用模型（$RUN，含第二版预测产物）"
    START_DASHBOARD=0 CLEANING="$DATA" RUN="$RUN" bash "$ROOT/stage2/run_all.sh" >/dev/null
  fi
fi

if [ ! -x "$BIN" ] || [ "$ROOT/tools/defense_demo.cpp" -nt "$BIN" ]; then
  echo "== 编译演示程序（约 10 秒）"
  mkdir -p "$(dirname "$BIN")" "$ROOT/build-stage2-tools/pro"
  cat > "$ROOT/build-stage2-tools/pro/defense_demo.pro" <<'PRO'
QT += core gui widgets network webenginewidgets
CONFIG += console c++17
TARGET = defense_demo
TEMPLATE = app
SOURCES += $$PWD/../../tools/defense_demo.cpp
PRO
  ( cd "$ROOT/build-stage2-tools/pro" && qmake6 defense_demo.pro >/dev/null && make -j2 >/dev/null )
  cp "$ROOT/build-stage2-tools/pro/defense_demo" "$BIN"
fi

# 容器内需要绕开 proxychains 对 127.0.0.1 的劫持
exec env -u LD_PRELOAD -u PROXYCHAINS_CONF_FILE "$BIN" \
  --assets "$ROOT/stage2/docs/PPT素材" \
  --data "$DATA" --model "$MODEL" --model-v2 "$MODEL_V2" "$@"
