#!/usr/bin/env bash
# ============================================================
# 中期评审一键演示启动脚本（第4组，2026-09-07）
# 用法：
#   ./midterm_demo.sh            # 启动 PcServer / UserClient / InnovationDemo / Web大屏
#   ./midterm_demo.sh --docs     # 额外打开需求矩阵 Excel 与概要设计 PDF
#   ./midterm_demo.sh --sqlite   # 额外打开 sqlite3 终端（.tables 查看 8 张表）
# 说明：自动 git pull；程序未构建会自动 qmake6+make；大屏用 xdg-open 打开浏览器。
# ============================================================
set -u

REPO="/home/bit/桌面/dongruan_ws/ChargingPilePlatform"
SRC="$REPO/src"
OPEN_DOCS=0
OPEN_SQLITE=0
for arg in "$@"; do
  case "$arg" in
    --docs)   OPEN_DOCS=1 ;;
    --sqlite) OPEN_SQLITE=1 ;;
  esac
done

# 确保最新代码
echo "== 1/5 拉取最新代码 =="
git -C "$REPO" pull --ff-only 2>&1 | tail -1

# 工具函数：确保某工程已构建
build_app() { # $1=目录名
  local dir="$SRC/$1"
  local pro; pro=$(ls "$dir"/*.pro 2>/dev/null | head -1)
  if [ ! -f "$pro" ]; then
    echo "!! 找不到工程文件: $dir"; return 1
  fi
  ( cd "$dir" && qmake6 "$pro" >/dev/null 2>&1 && make -s -j4 >/dev/null 2>&1 ) \
    && echo "  构建完成: $1" || { echo "!! 构建失败: $1"; return 1; }
}

# 工具函数：后台启动 GUI 程序（在自身目录运行，保证 db 落位）
launch_gui() { # $1=显示名 $2=目录 $3=可执行名
  local exe="$SRC/$2/$3"
  if [ ! -x "$exe" ]; then
    echo "== 构建 $1 =="
    build_app "$2" || return 1
  fi
  ( cd "$SRC/$2" && env -u LD_PRELOAD nohup "./$3" >/dev/null 2>&1 & )
  sleep 1.2
  if pgrep -f "$3" >/dev/null 2>&1; then
    echo "  ✅ $1 已启动（窗口应已弹出）"
  else
    echo "  ⚠️  $1 未检测到进程，请检查 $SRC/$2 下日志或手动运行"
  fi
}

echo "== 2/5 启动 PC 服务器端（数据库+充电站管理） =="
launch_gui "PcServer" pcserver PcServer

echo "== 3/5 启动 充电用户端（腾讯地图 POI） =="
launch_gui "UserClient" userclient UserClient

echo "== 4/5 启动 创新点模拟演示 =="
launch_gui "InnovationDemo" innovation_demo InnovationDemo

echo "== 5/5 打开 Web 大屏（离线 ECharts） =="
if command -v xdg-open >/dev/null 2>&1; then
  xdg-open "file://$SRC/webdashboard/index.html" >/dev/null 2>&1 &
  echo "  已用默认浏览器打开大屏页面（Ctrl+F5 强刷）"
else
  echo "  未找到 xdg-open，请手动打开: $SRC/webdashboard/index.html"
fi

if [ "$OPEN_DOCS" = "1" ]; then
  echo "== 附加：打开文档 =="
  [ -f "$REPO/docs/01需求矩阵第4组张芮萌.xlsx" ] && xdg-open "$REPO/docs/01需求矩阵第4组张芮萌.xlsx" >/dev/null 2>&1 &
  [ -f "$REPO/docs/概要设计说明书第4组.pdf" ] && xdg-open "$REPO/docs/概要设计说明书第4组.pdf" >/dev/null 2>&1 &
fi

if [ "$OPEN_SQLITE" = "1" ]; then
  echo "== 附加：打开 sqlite3 终端 =="
  if command -v gnome-terminal >/dev/null 2>&1; then
    gnome-terminal -- bash -lc "cd '$SRC/pcserver' && sqlite3 chargingpile.db '.tables'; echo '--- 按回车关闭 ---'; read" >/dev/null 2>&1 &
  else
    echo "  未找到 gnome-terminal，请手动执行: cd $SRC/pcserver && sqlite3 chargingpile.db .tables"
  fi
fi

echo ""
echo "=============================================="
echo " 演示窗口已启动。核对清单："
echo " 1) PcServer      —— 电站列表/明细 3 秒刷新/新增电站"
echo " 2) UserClient    —— 附近电站（腾讯 POI）/ 详情 / 定位兜底"
echo " 3) InnovationDemo—— 动态计费 80%→闲时特惠；连点 3 次异常→自愈重启"
echo " 4) Web 大屏      —— KPI+3 图，可断网 Ctrl+F5"
echo " 若窗口被遮挡，用 Alt+Tab 切换。"
echo "=============================================="
