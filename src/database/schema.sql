-- ============================================================
-- 东软电动汽车充电桩应用管理平台 - SQLite 建表脚本
-- 维护人：陈庚泉（FlavourCatie）
-- 约定：表名字段名小写，字段注释见各表；结构变更需评审后合入 main
-- 执行：sqlite3 chargingpile.db < schema.sql
-- ============================================================

PRAGMA foreign_keys = ON;

-- 管理员账号表（默认账号 admin / 123456，密码建议存哈希）
CREATE TABLE IF NOT EXISTS admins (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    username    TEXT NOT NULL UNIQUE,
    password    TEXT NOT NULL,
    created_at  TEXT DEFAULT (datetime('now','localtime'))
);

-- 用户表（手机号免密登录）
CREATE TABLE IF NOT EXISTS users (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    phone       TEXT NOT NULL UNIQUE,            -- 11 位手机号
    nickname    TEXT NOT NULL DEFAULT '',        -- 默认：用户+手机号后4位
    avatar_path TEXT,                            -- 头像路径（资源文件/本地）
    balance     REAL NOT NULL DEFAULT 0,         -- 钱包余额（元）
    status      INTEGER NOT NULL DEFAULT 0,      -- 0 正常 / 1 冻结
    gmt_create  TEXT DEFAULT (datetime('now','localtime')),
    gmt_modified TEXT DEFAULT (datetime('now','localtime'))
);

-- 充电站表
CREATE TABLE IF NOT EXISTS stations (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    name         TEXT NOT NULL,
    address      TEXT NOT NULL,
    longitude    REAL NOT NULL DEFAULT 0,        -- 经度（腾讯地图坐标）
    latitude     REAL NOT NULL DEFAULT 0,        -- 纬度
    total_piles  INTEGER NOT NULL DEFAULT 0,     -- 总电桩数
    online_rate  REAL NOT NULL DEFAULT 0,        -- 当前在线率 0~100
    base_price   REAL NOT NULL DEFAULT 1.0,      -- 基础电价（元/度），创新点1使用
    gmt_create   TEXT DEFAULT (datetime('now','localtime'))
);

-- 充电桩表（归属充电站）
CREATE TABLE IF NOT EXISTS piles (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id    INTEGER NOT NULL REFERENCES stations(id),
    code          TEXT NOT NULL UNIQUE,          -- 电桩编号（如 S01-P01）
    type          TEXT NOT NULL DEFAULT '快充',  -- 快充 / 慢充
    power_kw      REAL NOT NULL DEFAULT 0,       -- 功率(kW)
    state         INTEGER NOT NULL DEFAULT 0,    -- 0 闲置/1 充电中/2 故障
    charge_count  INTEGER NOT NULL DEFAULT 0,    -- 累计充电次数
    charge_seconds INTEGER NOT NULL DEFAULT 0    -- 累计充电时长(秒)
);

-- 充电订单表（预约-充电-计费-结算）
CREATE TABLE IF NOT EXISTS orders (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id      INTEGER NOT NULL REFERENCES users(id),
    pile_id      INTEGER NOT NULL REFERENCES piles(id),
    station_id   INTEGER NOT NULL REFERENCES stations(id),
    start_time   TEXT,
    end_time     TEXT,
    kwh          REAL NOT NULL DEFAULT 0,        -- 充电量（度）
    price        REAL NOT NULL DEFAULT 0,        -- 结算单价（元/度，可含折扣）
    amount       REAL NOT NULL DEFAULT 0,        -- 结算金额（元）
    state        INTEGER NOT NULL DEFAULT 0      -- 0 充电中/1 已完成/2 已取消
);

-- 电站营销策略表（创新点1：闲时“反向激励”动态计费）
CREATE TABLE IF NOT EXISTS marketing_strategy (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id   INTEGER NOT NULL REFERENCES stations(id),
    base_price   REAL NOT NULL DEFAULT 1.0,      -- 基础价
    discount     REAL NOT NULL DEFAULT 1.0,      -- 折扣率（如 0.8 = 8折）
    rule_desc    TEXT,                           -- 规则说明（如 空闲率>60% 生效）
    is_active    INTEGER NOT NULL DEFAULT 1,     -- 是否启用
    valid_from   TEXT,
    valid_to     TEXT,
    gmt_create   TEXT DEFAULT (datetime('now','localtime'))
);

-- 电桩健康阈值表（创新点2：自愈告警，移动平均极差法阈值）
CREATE TABLE IF NOT EXISTS pile_health_metrics (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_id      INTEGER NOT NULL UNIQUE REFERENCES piles(id),
    avg_power    REAL NOT NULL DEFAULT 0,        -- 历史平均功率
    low_threshold  REAL NOT NULL DEFAULT 0,      -- 低异常阈值（低于均值20%等）
    high_threshold REAL NOT NULL DEFAULT 0,      -- 高异常阈值
    sample_count INTEGER NOT NULL DEFAULT 0,     -- 样本数
    updated_at   TEXT DEFAULT (datetime('now','localtime'))
);

-- 每次充电结束后的实功率记录（创新点2数据来源）
CREATE TABLE IF NOT EXISTS pile_power_logs (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_id   INTEGER NOT NULL REFERENCES piles(id),
    order_id  INTEGER REFERENCES orders(id),
    real_power REAL NOT NULL DEFAULT 0,
    logged_at TEXT DEFAULT (datetime('now','localtime'))
);

-- 常用索引
CREATE INDEX IF NOT EXISTS idx_piles_station ON piles(station_id);
CREATE INDEX IF NOT EXISTS idx_orders_user   ON orders(user_id);
CREATE INDEX IF NOT EXISTS idx_orders_pile   ON orders(pile_id);
