#!/usr/bin/env bash
# ============================================================
# 整体演示脚本（答辩用）：按顺序弹出各功能窗口
# 用法：./final_demo.sh [--no-seed] [--no-map]
#   默认：一键灌北京演示库 → 启动 PcServer → 启动 UserClient →
#         打开 Web 大屏 → 打开数据佐证终端
#   --no-seed 跳过重新灌库（已有演示库时更快）
#   --no-map  未配置 TENCENT_MAP_KEY 时仍继续（地图走兜底）
# 每步“按回车”推进，便于讲解控场。
# ============================================================
set -u

REPO="/home/bit/桌面/dongruan_ws/ChargingPilePlatform"
SRC="$REPO/src"
SEED=1
NEED_MAP=1
for a in "$@"; do
  [ "$a" = "--no-seed" ] && SEED=0
  [ "$a" = "--no-map" ] && NEED_MAP=0
done

echo "== 0/6 环境检查 =="
git -C "$REPO" pull --ff-only 2>&1 | tail -1
if [ -z "${TENCENT_MAP_KEY:-}" ]; then
  if [ "$NEED_MAP" = "1" ]; then
    echo "⚠️  未设置 TENCENT_MAP_KEY。"
    read -r -p "    请先 export TENCENT_MAP_KEY=你的key，或按回车以 --no-map 继续： " _
  fi
fi

if [ "$SEED" = "1" ]; then
  echo "== 1/6 一键灌演示库（北京5站/128订单/特惠/自愈彩蛋） =="
  "$REPO/tools/seed_demo_data.sh" | tail -12
fi

open_term() {
  if command -v gnome-terminal >/dev/null 2>&1; then
    gnome-terminal -- bash -lc "$1" >/dev/null 2>&1 &
  else
    echo "（未找到 gnome-terminal，请手动执行：$1）"
  fi
}
launch() { # dir exe name
  local d="$SRC/$1" e="$2" n="$3"
  if [ ! -x "$d/$e" ]; then
    ( cd "$d" && qmake6 >/dev/null 2>&1 && make -s -j4 >/dev/null 2>&1 ) \
      || { echo "!! $n 构建失败"; return; }
  fi
  ( cd "$d" && env -u LD_PRELOAD nohup "./$e" >/tmp/demo_$n.log 2>&1 & )
  sleep 1.5
  pgrep -f "$e" >/dev/null 2>&1 && echo "  ✅ $n 已启动" || echo "  ⚠️ $n 未检测到"
}
pause() { echo ""; read -r -p "👉 按回车进入下一步（$1）..."; }

echo "== 2/6 启动 PcServer（登录 admin/123456） =="
launch pcserver PcServer PcServer
echo "   启动后自动运行：Socket 9999 / 大屏API 8890 / 价格策略 / 自愈 / 负荷预测"
pause "在 PcServer 演示：营收/状态/桩/负荷预测页签；S03-P02 10秒内自愈"

echo "== 3/6 启动 UserClient（手机号登录 → 北京电站/特惠） =="
launch userclient UserClient UserClient
pause "输入 11 位手机号登录（自动注册），看“我的”昵称/余额与首页特惠电站"

echo "== 4/6 打开 Web 大屏（动态数据 8890 + 6h预测） =="
if command -v xdg-open >/dev/null 2>&1; then
  xdg-open "file://$SRC/webdashboard/index.html" >/dev/null 2>&1 &
  echo "  大屏页面已打开（每 10s 自动拉取 127.0.0.1:8890 动态数据）"
else
  echo "  请手动打开：$SRC/webdashboard/index.html"
fi
pause "查看 KPI/图表/预测曲线，可 Ctrl+F5 刷新"

echo "== 5/6 数据佐证终端 =="
open_term "cd '$SRC/pcserver' && echo '== 电站与营销策略 ==' && sqlite3 -header -column chargingpile.db 'SELECT id,name,base_price FROM stations; SELECT station_id,discount,is_active FROM marketing_strategy;' && echo '== 今日/本月营收 ==' && sqlite3 -header -column chargingpile.db \"SELECT '今日' k,ROUND(SUM(amount),2) v FROM orders WHERE state=1 AND date(start_time)=date('now','localtime') UNION ALL SELECT '本月',ROUND(SUM(amount),2) FROM orders WHERE state=1 AND strftime('%Y-%m',start_time)=strftime('%Y-%m','now','localtime');\" && echo '回车关闭' && read"
pause "看完数据库佐证后进入收尾"

echo "== 6/6 完成 =="
echo "整体演示窗口已依次弹出。可选加分演示：tools/new_features_demo.sh"
echo "收尾口径：机制真实、132 项测试通过、数据为北京演示库（数据源可替换）。"
