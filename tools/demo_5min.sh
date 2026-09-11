#!/usr/bin/env bash
# ============================================================
# 五端联动演示脚本（5 分钟视频版，2026-09-11）
#
# 依次弹出：PcServer 后台 → UserClient 用户端 → Web 大屏 → 数据佐证终端
# 用法：
#   ./tools/demo_5min.sh              # 逐步推进（每步按回车，便于控场）
#   ./tools/demo_5min.sh --auto       # 不暂停，全部直接拉起
#   ./tools/demo_5min.sh --no-seed    # 跳过重新灌库（已有演示库时更快）
#   ./tools/demo_5min.sh --no-map     # 未配置 TENCENT_MAP_KEY 时使用（地图走兜底）
#
# 时间轴（对应 docs/演示口播稿.md）：
#   0:00-0:20  开场/架构
#   0:20-1:30  用户端（登录→特惠电站→真实桩→充电→结算）
#   1:30-2:20  服务端后台 + 数据库
#   2:20-3:00  负荷预测 + Web 大屏
#   3:00-3:50  创新点① 价格策略页
#   3:50-4:30  创新点② 自愈告警页（注入低功率样本演示）
#   4:30-5:00  交付自检页 + 收尾
# ============================================================
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO/src"
SEED=1
AUTO=0
NEED_MAP=1

for a in "$@"; do
  [ "$a" = "--no-seed" ] && SEED=0
  [ "$a" = "--auto" ] && AUTO=0 && AUTO=1
  [ "$a" = "--no-map" ] && NEED_MAP=0
done

pause() {
  if [ "$AUTO" = "1" ]; then return 0; fi
  echo ""
  read -r -p "👉 按回车进入下一步（$1）..." _
}

step() { echo ""; echo "===== $1 ====="; }

open_term() {
  if command -v gnome-terminal >/dev/null 2>&1; then
    gnome-terminal -- bash -lc "$1" >/dev/null 2>&1 &
  elif command -v x-terminal-emulator >/dev/null 2>&1; then
    x-terminal-emulator -e bash -lc "$1" >/dev/null 2>&1 &
  else
    echo "  （未找到终端模拟器，请手动执行：$1）"
  fi
}

launch() { # $1 目录名 $2 可执行名 $3 显示名
  local d="$SRC/$1" e="$2" n="$3"
  if [ ! -x "$d/$e" ]; then
    echo "  ⏳ 首次运行，编译 $n ..."
    ( cd "$d" && qmake6 >/dev/null 2>&1 && make -s -j4 >/dev/null 2>&1 ) \
      || { echo "  ❌ $n 编译失败，请先手动构建"; return 1; }
  fi
  ( cd "$d" && env -u LD_PRELOAD nohup "./$e" >"/tmp/demo_$n.log" 2>&1 & )
  sleep 2
  if pgrep -f "[/.]$e" >/dev/null 2>&1; then
    echo "  ✅ $n 已启动（日志 /tmp/demo_$n.log）"
  else
    echo "  ⚠️  $n 未检测到，请查看 /tmp/demo_$n.log"
  fi
}

# ------------------------------------------------------------
echo "=============================================="
echo " 东软充电桩平台 · 五端联动演示（5 分钟版）"
echo " 仓库：$REPO"
echo "=============================================="

step "0/5 环境检查与代码同步"
env -u LD_PRELOAD git -C "$REPO" pull --ff-only 2>&1 | tail -2
echo "  当前提交：$(git -C "$REPO" log --oneline -1)"

# Qt 图形程序需要图形会话；在纯 SSH 终端里运行会直接 abort（缺 DISPLAY）
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
  echo ""
  echo "  ⚠️  当前终端没有图形会话（DISPLAY / WAYLAND_DISPLAY 均为空）。"
  echo "      PcServer / UserClient 是 Qt 图形程序，会启动失败。"
  echo "      请在虚拟机桌面上的终端里运行本脚本，或先执行：export DISPLAY=:0"
  if [ "$AUTO" = "0" ]; then
    read -r -p "      仍要继续吗？（回车继续 / Ctrl+C 退出）" _
  fi
fi

if [ -z "${TENCENT_MAP_KEY:-}" ] && [ "$NEED_MAP" = "1" ]; then
  echo "  ⚠️  未设置 TENCENT_MAP_KEY（地图/导航将走兜底）"
  echo "      要跳过该提示可加 --no-map"
fi

# 清理上一次演示残留，避免端口 9999/8890 被占用
for pid in $(pgrep -f '[/.]PcServer' 2>/dev/null); do kill "$pid" 2>/dev/null; done
for pid in $(pgrep -f '[/.]UserClient' 2>/dev/null); do kill "$pid" 2>/dev/null; done
sleep 1

if [ "$SEED" = "1" ]; then
  step "1/5 重置演示数据库（北京 5 站 / 37 桩 / 128 订单）"
  "$REPO/tools/seed_demo_data.sh" 2>&1 | tail -8
else
  echo "  （已跳过灌库）"
fi

step "2/5 启动 PcServer（服务器端后台）"
launch pcserver PcServer PcServer
cat <<'TIP'

  ▶ 登录框：账号 admin 密码 123456
  ▶ 启动后自动运行：Socket 9999 / 大屏 API 8890 / 价格策略引擎 / 自愈检查

TIP
pause "登录并停留在「销售业绩」页"

step "3/5 启动 UserClient（充电用户端）"
launch userclient UserClient UserClient
cat <<'TIP'

  ▶ 输入 11 位手机号登录（未注册自动注册并赠送初始余额）
  ▶ 首页看电站列表与「闲时特惠」徽标 → 进详情看真实桩列表
  ▶ 选一根空闲桩「预约充电」→ 结束充电看服务器结算金额

TIP
pause "完成一次完整充电结算"

step "4/5 打开 Web 运营大屏"
if command -v xdg-open >/dev/null 2>&1; then
  xdg-open "file://$SRC/webdashboard/index.html" >/dev/null 2>&1 &
  echo "  ✅ 大屏已打开（每 10 秒自动拉取 127.0.0.1:8890 聚合数据）"
else
  echo "  请手动打开：$SRC/webdashboard/index.html"
fi
echo "  ▶ 依次点顶部三个书签：总览 / 负荷预测 / 营收订单"
pause "看完大屏总览与预测页"

step "5/5 打开数据佐证终端（订单与定价落库证据）"
open_term "cd '$SRC/pcserver' && echo '=== 最近 5 笔订单（服务器口径金额） ===' && sqlite3 -header -column chargingpile.db \"SELECT id, pile_id, kwh, price, amount, state, end_time FROM orders ORDER BY id DESC LIMIT 5;\" && echo && echo '=== 价格策略：预测空闲率 → 折扣 → 执行价 ===' && sqlite3 -header -column chargingpile.db \"SELECT s.name, s.base_price, ROUND(m.predicted_idle_rate,1) AS idle_pct, m.discount, ROUND(s.base_price*m.discount,2) AS price, m.is_active, m.decided_at FROM stations s LEFT JOIN marketing_strategy m ON m.station_id=s.id ORDER BY s.id;\" && echo && echo '=== 自愈事件流水 ===' && sqlite3 -header -column chargingpile.db \"SELECT created_at, pile_code, level_text, ROUND(threshold,1) AS thr, ROUND(real_power,1) AS real, action FROM selfheal_events ORDER BY id DESC LIMIT 5;\" && echo && echo '回车关闭' && read"
pause "给完数据库佐证"

step "完成"
cat <<'TIP'

  收尾补充（回到 PcServer 演示）：
  ▶ 「价格策略」页：每站预测空闲率 / 折扣 / 执行价 / 决策时间，可点「立即重算」
  ▶ 「自愈告警」页：选一根桩 → 连续点 3 次「注入低功率样本」→ 看预警与事件流水
                    再点「注入正常功率样本」→ 看预警解除
  ▶ 「交付自检」页：点「开始自检」→ 9 项检查全部 PASS
  ▶ 「运行日志」页：协议层错误码与告警统一留痕

  口播稿：docs/演示口播稿.md

TIP
