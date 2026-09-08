#!/usr/bin/env bash
# ============================================================
# 新增可演示内容 分步演示脚本（中期后新增：登录/销售业绩/电桩状态/
# 桩管理/创新点自动链路/网络层测试）
# 用法：./new_features_demo.sh
# 流程：按回车逐步切换，便于讲解控场。
# ============================================================
set -u

REPO="/home/bit/桌面/dongruan_ws/ChargingPilePlatform"
SRC="$REPO/src"

echo "== 0/6 拉取并构建最新代码 =="
env -u LD_PRELOAD git -C "$REPO" pull --ff-only 2>&1 | tail -1
for d in pcserver innovation_demo; do
  ( cd "$SRC/$d" && qmake6 >/dev/null 2>&1 && make -s -j4 >/dev/null 2>&1 ) \
    && echo "  ✅ $d 构建完成" || echo "  ⚠️  $d 构建失败"
done

open_term() { # 打开一个保留窗口执行命令
  if command -v gnome-terminal >/dev/null 2>&1; then
    gnome-terminal -- bash -lc "$1" >/dev/null 2>&1 &
  else
    echo "（未找到 gnome-terminal，请手动执行：$1）"
  fi
}

pause() {
  echo ""
  read -r -p "👉 按回车进入下一步（$1）..."
}

echo "== 1/6 弹出 PC 服务器端（登录/销售业绩/电桩状态/桩管理） =="
echo "   登录：admin / 123456"
echo "   进入后依次演示：销售业绩(QChart 7/30日) → 电桩状态 → 充电桩管理 → 远程重启"
( cd "$SRC/pcserver" && env -u LD_PRELOAD nohup ./PcServer >/tmp/pcserver_demo.log 2>&1 & )
pause "在 PcServer 里演示完成后继续"

echo "== 2/6 弹出 sqlite3 观察窗口（创新点1 自动折扣真实链路） =="
echo "   PcServer 已自动运行 PricingService：空闲电站应自动出现 8 折记录"
open_term "cd '$SRC/pcserver' && echo '先把演示电站置为全空闲(空闲率100%)，等待自动引擎下一轮(<=10s)...' && sqlite3 chargingpile.db 'UPDATE piles SET state=0;' && sleep 12 && echo '营销策略表（自动写入的闲时特惠）：' && sqlite3 -header -column chargingpile.db 'SELECT station_id,discount,is_active,rule_desc FROM marketing_strategy;' && echo '--- 电站空闲率 ---' && sqlite3 -header -column chargingpile.db 'SELECT station_id, COUNT(*) AS total, SUM(state=0) AS idle FROM piles GROUP BY station_id;' && echo '回车关闭' && read"
pause "看完营销策略表后继续"

echo "== 3/6 注入连续低功率并观察 创新点2 自愈自动重启 =="
echo "   将向充电桩 1 写入阈值与 3 条低功率日志，10 秒内 PcServer 应自动触发重启"
open_term "pgrep -x PcServer >/dev/null || echo '!! PcServer 未运行：请先启动并登录再重跑本步'; cd '$SRC/pcserver' && sqlite3 chargingpile.db \"INSERT OR IGNORE INTO pile_health_metrics(pile_id,avg_power,low_threshold,high_threshold) VALUES(1,60,48,72); INSERT INTO pile_power_logs(pile_id,real_power) VALUES(1,10),(1,10),(1,10);\" && echo '已注入 3 条低功率(10kW)，阈值 48kW，等待自愈服务(<=10s)...' && for i in \$(seq 1 12); do echo \"--- 第 \$i 秒 状态 ---\"; sqlite3 -header -column chargingpile.db 'SELECT p.id,p.state,p.code FROM piles p WHERE p.id=1; SELECT COUNT(*) AS restart_logs FROM pile_power_logs WHERE pile_id=1 AND real_power>45;'; sleep 2; done && echo '若 state 曾=2 后回 0、restart_logs 增加，说明自愈自动重启成功' && echo '回车关闭' && read"
pause "观察自愈触发后继续"

echo "== 4/6 弹出网络层测试窗口（NO.19，联调用底层） =="
open_term "cd '$SRC/common/tests' && qmake6 packet_assembler_tests.pro >/dev/null 2>&1 && make -s -j4 >/dev/null 2>&1; qmake6 net_socket_tests.pro >/dev/null 2>&1 && make -s -j4 >/dev/null 2>&1; echo '== 协议组包测试 ==' && ./packet_assembler_tests 2>&1 | tail -2 && echo '== 真实 TCP 联调测试 ==' && env -u LD_PRELOAD ./net_socket_tests 2>&1 | tail -2 && echo '回车关闭' && read"
pause "看完网络测试后继续"

echo "== 5/6 弹出自动服务单测窗口（创新点落地版） =="
open_term "cd '$SRC/pcserver/tests' && qmake6 services_tests.pro >/dev/null 2>&1 && make -s -j4 >/dev/null 2>&1 && ./tst_services 2>&1 | tail -3 && echo '回车关闭' && read"
pause "看完服务测试后继续"

echo "== 6/6 完成 =="
echo "新增可演示内容窗口已依次弹出完毕。汇总："
echo " 1) PcServer：登录/销售业绩/电桩状态/桩管理/远程重启"
echo " 2) 创新点1：marketing_strategy 自动 8 折（无需人工点击）"
echo " 3) 创新点2：低功率日志 → 自动需检查+模拟远程重启"
echo " 4) 网络层：packet 10 + net 7 项测试"
echo " 5) 落地服务：tst_services 4 项测试"
