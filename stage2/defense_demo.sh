#!/usr/bin/env bash
# 第二阶段答辩演示：按顺序弹出七环节效果图与实时大屏窗口。
#
# 用法：
#   bash stage2/defense_demo.sh                # 手动推进（空格/→ 下一步，← 上一步，R 重来，Esc 退出）
#   bash stage2/defense_demo.sh --auto 25      # 每 25 秒自动进入下一个窗口
#   bash stage2/defense_demo.sh --selftest     # 自检：把 12 个窗口逐帧导出 PNG，不用于答辩现场
#
# 默认数据：共享目录已清洗结果；默认模型：build-stage2/run-demo/model.json（缺失时自动生成）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA="${DATA:-/home/bit/vmware_share/大数据开发/06.清洗结果_20260916_v2}"
RUN="${RUN:-$ROOT/build-stage2/run-demo}"
MODEL="${MODEL:-$RUN/model.json}"
BIN="${BIN:-$ROOT/build-stage2-tools/defense_demo}"
REPO_SERVER="$ROOT/stage2/dashboard/server.py"

if [ ! -f "$MODEL" ]; then
  echo "== 生成演示用模型（$RUN）"
  START_DASHBOARD=0 CLEANING="$DATA" RUN="$RUN" bash "$ROOT/stage2/run_all.sh" >/dev/null
fi

if [ ! -x "$BIN" ]; then
  echo "== 首次运行，编译演示程序（约 10 秒）"
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
  --data "$DATA" --model "$MODEL" "$@"
