-- ============================================================
-- 东软电动汽车充电桩应用管理平台 - SQLite 建表脚本（v2）
-- 维护人：陈庚泉（FlavourCatie）
-- 约定：表名字段名小写；结构变更需评审后合入 main
-- 核心五表：admins / users / stations / piles / orders
-- 执行：sqlite3 chargingpile.db < schema.sql（新建库；旧演示库建议删除后重建）
-- ============================================================

PRAGMA foreign_keys = ON;

-- 管理员账号表（默认演示账号 admin / 123456；生产环境密码应存哈希）
CREATE TABLE IF NOT EXISTS admins (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    username    TEXT NOT NULL COLLATE NOCASE UNIQUE
                CHECK (length(trim(username)) BETWEEN 1 AND 32),
    password    TEXT NOT NULL
                CHECK (length(password) >= 6),
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 用户表（手机号 11 位数字、余额非负、status 0 正常 / 1 冻结）
CREATE TABLE IF NOT EXISTS users (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    phone       TEXT NOT NULL UNIQUE
                CHECK (length(phone) = 11 AND phone NOT GLOB '*[^0-9]*'),
    nickname    TEXT NOT NULL DEFAULT ''
                CHECK (length(nickname) <= 32),
    avatar_path TEXT,                            -- 头像路径（资源文件/本地）
    balance     REAL NOT NULL DEFAULT 0          -- 钱包余额（元）
                CHECK (balance >= 0),
    status      INTEGER NOT NULL DEFAULT 0       -- 0 正常 / 1 冻结
                CHECK (status IN (0, 1)),
    gmt_create  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    gmt_modified TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 充电站表（经纬度范围、总桩数/在线率取值范围约束）
CREATE TABLE IF NOT EXISTS stations (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    name         TEXT NOT NULL
                 CHECK (length(trim(name)) BETWEEN 1 AND 64),
    address      TEXT NOT NULL
                 CHECK (length(trim(address)) BETWEEN 1 AND 200),
    longitude    REAL NOT NULL DEFAULT 0         -- 经度（腾讯地图坐标）
                 CHECK (longitude BETWEEN -180 AND 180),
    latitude     REAL NOT NULL DEFAULT 0         -- 纬度
                 CHECK (latitude BETWEEN -90 AND 90),
    total_piles  INTEGER NOT NULL DEFAULT 0      -- 总电桩数
                 CHECK (total_piles BETWEEN 0 AND 1000),
    online_rate  REAL NOT NULL DEFAULT 0         -- 当前在线率 0~100
                 CHECK (online_rate BETWEEN 0 AND 100),
    base_price   REAL NOT NULL DEFAULT 1.0       -- 基础电价（元/度），创新点1使用
                 CHECK (base_price >= 0),
    gmt_create   TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 充电桩表（归属充电站；删除电站级联删除电桩；state 0 闲置/1 充电中/2 故障）
CREATE TABLE IF NOT EXISTS piles (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id    INTEGER NOT NULL
                  REFERENCES stations(id) ON DELETE CASCADE ON UPDATE CASCADE,
    code          TEXT NOT NULL UNIQUE
                  CHECK (length(code) BETWEEN 3 AND 32),
    type          TEXT NOT NULL DEFAULT '快充'   -- 快充 / 慢充
                  CHECK (type IN ('快充', '慢充')),
    power_kw      REAL NOT NULL DEFAULT 0        -- 功率(kW)
                  CHECK (power_kw > 0),
    state         INTEGER NOT NULL DEFAULT 0     -- 0 闲置/1 充电中/2 故障
                  CHECK (state IN (0, 1, 2)),
    health_level  INTEGER NOT NULL DEFAULT 0     -- 创新点2 自愈分级：0 正常/1 预警(需检查)/2 故障
                  CHECK (health_level IN (0, 1, 2)),
    charge_count  INTEGER NOT NULL DEFAULT 0     -- 累计充电次数
                  CHECK (charge_count >= 0),
    charge_seconds INTEGER NOT NULL DEFAULT 0    -- 累计充电时长(秒)
                  CHECK (charge_seconds >= 0),
    gmt_create    TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 充电订单表（预约-充电-计费-结算；金额类字段非负、state 0 充电中/1 已完成/2 已取消）
CREATE TABLE IF NOT EXISTS orders (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id      INTEGER NOT NULL REFERENCES users(id),
    pile_id      INTEGER NOT NULL REFERENCES piles(id),
    station_id   INTEGER NOT NULL REFERENCES stations(id),
    start_time   TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    end_time     TEXT,
    kwh          REAL NOT NULL DEFAULT 0         -- 充电量（度）
                 CHECK (kwh >= 0),
    price        REAL NOT NULL DEFAULT 0         -- 结算单价（元/度，可含折扣）
                 CHECK (price >= 0),
    amount       REAL NOT NULL DEFAULT 0         -- 结算金额（元）
                 CHECK (amount >= 0),
    state        INTEGER NOT NULL DEFAULT 0      -- 0 充电中/1 已完成/2 已取消
                 CHECK (state IN (0, 1, 2)),
    gmt_create   TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 一致性触发器：订单记录的电站必须与电桩实际所属电站一致（防止跨站误记/脏数据）
CREATE TRIGGER IF NOT EXISTS trg_orders_validate_pile_station_insert
BEFORE INSERT ON orders
FOR EACH ROW
WHEN NOT EXISTS (
    SELECT 1 FROM piles
    WHERE id = NEW.pile_id AND station_id = NEW.station_id
)
BEGIN
    SELECT RAISE(ABORT, 'orders: pile_id 与 station_id 不一致或引用了不存在的电桩');
END;

CREATE TRIGGER IF NOT EXISTS trg_orders_validate_pile_station_update
BEFORE UPDATE OF pile_id, station_id ON orders
FOR EACH ROW
WHEN NOT EXISTS (
    SELECT 1 FROM piles
    WHERE id = NEW.pile_id AND station_id = NEW.station_id
)
BEGIN
    SELECT RAISE(ABORT, 'orders: pile_id 与 station_id 不一致或引用了不存在的电桩');
END;

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
    predicted_idle_rate REAL NOT NULL DEFAULT 0, -- 创新点1：本次定价依据的预测空闲率(%)
    decided_at   TEXT,                           -- 创新点1：策略引擎最近一次决策时间
    gmt_create   TEXT DEFAULT (datetime('now','localtime'))
);

-- 自愈事件留痕表（创新点2：异常检测与自愈动作可追溯，供服务器端"自愈告警"页展示）
CREATE TABLE IF NOT EXISTS selfheal_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_id     INTEGER NOT NULL REFERENCES piles(id),
    pile_code   TEXT,
    station_id  INTEGER,
    level       INTEGER NOT NULL DEFAULT 1,      -- 1 预警(需检查) / 2 故障
    level_text  TEXT,
    threshold   REAL NOT NULL DEFAULT 0,         -- 判定使用的低功率阈值(kW)
    real_power  REAL NOT NULL DEFAULT 0,         -- 触发时的实测功率(kW)
    action      TEXT,                            -- 引擎执行的动作
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
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

-- ============================================================
-- 性能索引（按核心业务查询设计；UNIQUE 字段由 SQLite 自动建唯一索引）
-- ============================================================
-- 电站：附近充电站按经纬度范围查询
CREATE INDEX IF NOT EXISTS idx_stations_geo ON stations(latitude, longitude);

-- 电桩：按电站查桩列表、按状态统计在线率/运维查询
CREATE INDEX IF NOT EXISTS idx_piles_station ON piles(station_id);
CREATE INDEX IF NOT EXISTS idx_piles_state   ON piles(state);

-- 订单：按用户/电桩/电站+时间/状态聚合（销售业绩、账单、运维闭环高频查询）
CREATE INDEX IF NOT EXISTS idx_orders_user          ON orders(user_id);
CREATE INDEX IF NOT EXISTS idx_orders_pile          ON orders(pile_id);
CREATE INDEX IF NOT EXISTS idx_orders_station_start ON orders(station_id, start_time);
CREATE INDEX IF NOT EXISTS idx_orders_state         ON orders(state);

-- 用户：用户管理按状态筛选/冻结列表
CREATE INDEX IF NOT EXISTS idx_users_status ON users(status);

-- 自愈事件：按桩与时间检索（"自愈告警"页流水与最近事件查询）
CREATE INDEX IF NOT EXISTS idx_selfheal_pile ON selfheal_events(pile_id);
CREATE INDEX IF NOT EXISTS idx_selfheal_time ON selfheal_events(created_at);
