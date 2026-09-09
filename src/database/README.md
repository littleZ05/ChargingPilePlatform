# SQLite 数据库设计说明（NO.14）

> 维护人：陈庚泉（FlavourCatie）
> 契约说明：`schema.sql` 是全组公共约定，结构变更须评审后合入 main，
> 各端不得私自复制或改写建表语句。

## 一、核心表与关系

| 表 | 说明 | 主键 | 外键/引用 | 与业务模块 |
|---|---|---|---|---|
| `admins` | 管理员账号 | `id` | — | PC 服务器端登录（NO.8） |
| `users` | 充电用户 | `id` | — | 用户端（NO.4-7）/用户管理（NO.13） |
| `stations` | 充电站 | `id` | — | 附近查站/电站管理（NO.4、NO.12） |
| `piles` | 充电桩 | `id` | `station_id → stations(id)` 删除级联 | 桩状态/桩管理（NO.10-11） |
| `orders` | 充电订单 | `id` | `user_id → users(id)`、`pile_id → piles(id)`、`station_id → stations(id)` | 充电流程/销售业绩（NO.7、NO.9） |

```text
admins（独立）

stations 1──n piles 1──n orders n──1 users
stations 1────────────n orders
```

另有创新点扩展表：`marketing_strategy`、`pile_health_metrics`、`pile_power_logs`，
核心业务不依赖，删除/变更前仍须评审。

## 二、字段与约束要点

- 所有表统一：`INTEGER PRIMARY KEY AUTOINCREMENT` 自增主键 `id`；时间统一存
  `TEXT` ISO 格式（`datetime('now','localtime')`），页面按业务转换显示。
- `admins.username`：`COLLATE NOCASE` 唯一，登录不区分大小写；
  `password` 演示库存明文 `123456`，生产环境必须改为哈希后存储。
- `users.phone`：11 位纯数字且唯一；`balance >= 0`；`status ∈ {0 正常, 1 冻结}`。
- `stations`：经纬度范围 `[-180,180] / [-90,90]`，`total_piles ∈ [0,1000]`，
  `online_rate ∈ [0,100]`，`base_price >= 0`。
- `piles.code`：全局唯一（如 `S001-P01`）；`type ∈ {快充, 慢充}`；
  `state ∈ {0 闲置, 1 充电中, 2 故障}`（与 `src/common/common.h` 的
  `cp::PileState` 一一对应）；`power_kw > 0`。
- `orders`：金额与电量非负；`state ∈ {0 充电中, 1 已完成, 2 已取消}`
  （与 `cp::OrderState` 对应）。

## 三、数据一致性机制

1. 建库连接执行 `PRAGMA foreign_keys = ON`（Qt 侧每次打开库由 `StationStore` 执行 schema）；
2. 外键 + `UNIQUE`/`CHECK` 约束保证引用完整性与取值合法；
3. 触发器 `trg_orders_validate_pile_station_insert/update`：
   插入或修改订单时强制 `orders.pile_id` 所属电站等于 `orders.station_id`，
   防止“跨站记单”脏数据；
4. `piles.station_id ON DELETE CASCADE`：删除电站自动删除其电桩；
5. 订单引用 user/pile/station 为默认 `NO ACTION`，保留历史单据，
   不允许删除仍被订单引用的主体（先关单再删）。

## 四、索引与查询性能

| 索引 | 表 | 列 | 支撑查询 |
|---|---|---|---|
| `idx_stations_geo` | stations | `(latitude, longitude)` | 附近充电站范围查询/按位置排序 |
| `idx_piles_station` | piles | `station_id` | 按电站查桩列表、站内统计 |
| `idx_piles_state` | piles | `state` | 在线率/故障/状态筛选 |
| `idx_orders_user` | orders | `user_id` | 我的订单、账单 |
| `idx_orders_pile` | orders | `pile_id` | 按桩查历史/运维 |
| `idx_orders_station_start` | orders | `(station_id, start_time)` | 电站维度近 7/30 日营收聚合 |
| `idx_orders_state` | orders | `state` | 按订单状态统计/待结算 |
| `idx_users_status` | users | `status` | 用户管理冻结列表 |

`username/phone/code` 的唯一约束由 SQLite 自动建唯一索引，无需重复声明。

常用示例：

```sql
-- 某电站近 30 天已完成订单（命中 idx_orders_station_start）
SELECT date(start_time) AS day, SUM(amount) AS revenue
FROM orders INDEXED BY idx_orders_station_start
WHERE station_id = ? AND state = 1
  AND start_time >= datetime('now', 'localtime', '-30 days')
GROUP BY day;

-- 某电站电桩在线率（命中 idx_piles_station）
SELECT COUNT(*) AS total,
       SUM(CASE WHEN state <> 2 THEN 1 ELSE 0 END) AS online
FROM piles INDEXED BY idx_piles_station
WHERE station_id = ?;
```

## 五、建库/升级

```bash
sqlite3 chargingpile.db < schema.sql
```

脚本对同库重复执行是幂等的（`IF NOT EXISTS`），但**已存在的旧结构不会自动补约束**。
课程演示阶段建议删除旧 `*.db` 后重建；真实项目应走评审后的迁移脚本，禁止生产环境直接删表。

## 六、数据库开发与管理（NO.15）

- 连接管理：`StationStore::open` 统一执行内置 `schema.sql`，并确保
  `PRAGMA foreign_keys = ON`；连接按名称隔离，测试/多模块可并行打开互不干扰；
- 事务：`StationStore::runInTransaction` 提供“回调成功提交、失败整体回滚”，
  新增电站/批量生成电桩等写操作不再允许“写一半”；
- 健康检查：`StationStore::integrityCheck` 执行 `PRAGMA integrity_check`；
- 备份：`StationStore::backupTo` 用 `VACUUM INTO` 生成一致性快照，
  路径含单引号等危险字符会被拒绝；
- 统一写入口：`StationStore::execPrepared(sql, binds)` 强制 prepare + bindValue，
  禁止把用户输入拼进 SQL 字符串。

### 数据安全规则（NO.15）

1. **参数校验前置**：业务输入先经统一规则（手机号/长度/数值区间/枚举，
   见 `common/protocol.h` 的 `cp::Validate`），数据库层再以 CHECK 兜底；
2. **防注入**：所有数据 SQL 使用占位符绑定；表名/列名等标识符必须是代码内常量，
   不允许由外部输入直接拼接；
3. **权限控制**：应用层消息按 `cp::Role` 与 `messageAccessLevel` 白名单/角色矩阵
   授权后再落库（心跳/登录公开，用户业务需 user，管理/销售需 operator/admin）；
4. **密码安全**：课程演示库仍用明文 `123456` 便于演示；合入真实部署前必须改为
   加盐哈希（如 PBKDF2/SHA-256），schema 中 `password` 字段只存哈希，不存原文。

## 七、自动化测试

数据库契约/一致性/索引测试位于 `src/pcserver/tests/schema_tests.pro`：

```bash
mkdir -p /tmp/tst-schema && cd /tmp/tst-schema
qmake6 <仓库>/src/pcserver/tests/schema_tests.pro && make && ./tst_schema
```

覆盖：五表存在/主键/列类型、外键声明、UNIQUE、CHECK、订单电站一致性触发器、
电站删除级联、索引列与 `EXPLAIN QUERY PLAN` 命中。

数据库管理与防注入用例位于 `src/pcserver/tests/stationstore_tests.pro`
（事务回滚/完整性/备份/危险路径拒绝/注入字符串仅存为数据）。
