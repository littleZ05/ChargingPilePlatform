#!/usr/bin/env bash
# ============================================================
# 一键生成“功能演示数据”（写入 PcServer 的 8 张表）
# 用法：./seed_demo_data.sh
# 说明：会先备份现有库到 /tmp，再清空并按 demo_seed.sql 重建。
#       请在脚本执行后重启 PcServer（S03-P02 会在启动 10 秒内自动演示自愈）。
# ============================================================
set -u

REPO="/home/bit/桌面/dongruan_ws/ChargingPilePlatform"
DB="$REPO/src/pcserver/chargingpile.db"

if pgrep -x PcServer >/dev/null 2>&1; then
  echo "检测到 PcServer 正在运行，先关闭（数据文件被占用）..."
  pkill -x PcServer
  sleep 1
fi

if [ -f "$DB" ]; then
  BK="/tmp/chargingpile.backup.$(date +%Y%m%d_%H%M%S).db"
  cp "$DB" "$BK" && echo "已备份旧库: $BK"
fi

echo "== 清空 8 张表 =="
sqlite3 "$DB" "PRAGMA foreign_keys=OFF;
DELETE FROM pile_power_logs;
DELETE FROM pile_health_metrics;
DELETE FROM marketing_strategy;
DELETE FROM orders;
DELETE FROM piles;
DELETE FROM stations;
DELETE FROM users;
DELETE FROM admins;" || { echo "清空失败"; exit 1; }

echo "== 重置自增序号（AUTOINCREMENT） =="
sqlite3 "$DB" "DELETE FROM sqlite_sequence;" 2>/dev/null || true

echo "== 应用表结构 =="
sqlite3 "$DB" < "$REPO/src/database/schema.sql" || { echo "schema 失败"; exit 1; }

echo "== 写入演示数据 =="
sqlite3 "$DB" < "$REPO/src/database/demo_seed.sql" || { echo "seed 失败"; exit 1; }

echo "== 结果统计 =="
sqlite3 -header -column "$DB" "
SELECT 'admins' t, COUNT(*) n FROM admins
UNION ALL SELECT 'users', COUNT(*) FROM users
UNION ALL SELECT 'stations', COUNT(*) FROM stations
UNION ALL SELECT 'piles', COUNT(*) FROM piles
UNION ALL SELECT 'orders', COUNT(*) FROM orders
UNION ALL SELECT 'marketing', COUNT(*) FROM marketing_strategy
UNION ALL SELECT 'metrics', COUNT(*) FROM pile_health_metrics
UNION ALL SELECT 'logs', COUNT(*) FROM pile_power_logs;"

echo ""
echo "✅ 演示数据已就绪。请重启 PcServer 演示；"
echo "   启动后 S03-P02 的 3 条异常功率会在 10 秒内自动触发【自愈检查→模拟远程重启】。"
