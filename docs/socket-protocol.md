# Socket 通信协议规范（v1.0）

> 维护人：吴羽桐（feat/shimmer-ywt）
> 状态：已实现（PcServer 业务层挂载）
> 公共契约：`src/common/common.h`（端口 / MsgType / 状态枚举），
> 协议实现：`src/common/packet_assembler.*`、`src/common/net_server.*`、`src/common/net_client.*`，
> 业务接入：`src/pcserver/mainwindow.*` + `src/pcserver/stationstore.*`

## 0. 目标与范围

本文档定义「PC 服务器 ↔ 用户端 / 充电桩设备」之间的完整 Socket 业务协议，
包括底层二进制帧格式与心跳、电站查询、充电订单上报三类业务消息。

- 服务器默认监听端口：`cp::kServerPort = 9999`（修改端口属公共契约变更，须小组评审）。
- 业务消息 Payload 一律为 UTF-8 JSON；字段命名统一 `snake_case`。
- 同一 TCP 连接可连续收发多帧，粘包 / 半包由 `cp::PacketAssembler` 自动处理。

## 1. 底层二进制帧格式

每个数据包为 **8 字节定长帧头 + 变长 Body**，字段间无分隔符：

| 字段 | 长度 | 类型 / 字节序 | 说明 |
|---|---|---|---|
| Magic | 2 字节 | 固定 `0x43 0x50`（即 `'C' 'P'`） | 帧头魔数，用于快速识别与重对齐 |
| MsgType | 2 字节 | `quint16`，大端序 | 业务消息类型，取值见 2.1 |
| BodyLen | 4 字节 | `quint32`，大端序 | Body 负载字节数（不含帧头） |
| Body | `BodyLen` 字节 | 原始字节 | 业务负载（本文档三类消息均为 JSON） |

### 1.1 帧示例（心跳请求，Body 38 字节）

请求 Body：

```json
{"client_id":"pc-001","ts":1750000000}
```

线上完整帧（十六进制）：

```text
43 50 00 00 00 00 00 26
7b 22 63 6c 69 65 6e 74 5f 69 64 22 3a 22 70 63
2d 30 30 31 22 2c 22 74 73 22 3a 31 37 35 30 30
30 30 30 30 30 30 7d
```

对应关系：

- `43 50`：Magic；
- `00 00`：MsgType = 0（`kHeartbeat`）；
- `00 00 00 26`：BodyLen = 38（大端序）；
- 其余为 38 字节 UTF-8 JSON Body。

### 1.2 组包 / 解包规则

- 发送端按「帧头 + Body」顺序写入 TCP；接收端按帧头解析，Body 不足时缓存等待（半包），
  一次到达多个完整帧时逐帧取出（粘包）。
- Magic 不匹配或声明的 BodyLen 超过 `cp::kPacketMaxBodySize`（64 MiB 防呆上限）时，
  逐字节滑动重新扫描对齐，丢弃损坏数据；正常业务 JSON 远小于该上限。
- 帧头魔数 / 长度常量与实现细节见 `src/common/packet_assembler.h`。

## 2. 消息总览

### 2.1 MsgType 取值（与 `src/common/common.h` 一一对应）

| 值 | 常量 | 方向 | 用途 |
|---|---|---|---|
| 0 | `cp::MsgType::kHeartbeat` | 客户端 → 服务器 / 服务器 → 客户端 | 心跳请求与 ACK |
| 1 | `cp::MsgType::kStationQuery` | 客户端 → 服务器 / 服务器 → 客户端 | 电站列表 / 空闲桩与当前价格查询 |
| 2 | `cp::MsgType::kOrderReport` | 客户端 → 服务器 / 服务器 → 客户端 | 充电订单上报与处理结果 |

> 服务器应答统一使用与请求相同的 MsgType；未知 MsgType 忽略并记录日志，不回包。

### 2.2 通用应答字段与错误码

服务器每个业务回包均为 JSON 对象，公共字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `code` | int | 0 = 成功，非 0 见错误码表 |
| `message` | string | 可读结果 / 错误描述 |
| `server_time` | string | 服务器本地时间 `yyyy-MM-dd HH:mm:ss` |

错误码表：

| code | 含义 |
|---|---|
| 0 | 成功 |
| 400 | 请求非法：Body 非 JSON 对象、必填字段缺失或取值范围非法 |
| 404 | 资源不存在：电站 ID 或电桩编码未找到 |
| 409 | 状态冲突：如故障电桩上报订单结算 |
| 500 | 服务器内部错误：数据读取 / 状态更新失败 |
| 503 | 服务未就绪：数据服务尚未打开（服务器刚启动或数据库异常） |

## 3. 业务消息定义

### 3.1 心跳 `kHeartbeat = 0`

用途：客户端周期性探测连接与服务器存活；服务器收到后立即回 ACK。

#### 请求

```json
{
  "client_id": "userclient-01",
  "ts": 1750000000
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `client_id` | string | 否 | 客户端标识（建议填写，便于服务器日志区分来源） |
| `ts` | number | 否 | 客户端发送时刻（Unix 秒时间戳） |

Body 允许为空对象 `{}`；服务器仍回成功 ACK。

#### 响应

```json
{
  "code": 0,
  "message": "pong",
  "server_time": "2026-09-09 10:00:00",
  "client_id": "userclient-01",
  "ts": 1750000000
}
```

`client_id` / `ts` 在请求提供时原样回显，未提供则省略；Body 非 JSON 对象时返回 `code=400`。

### 3.2 电站查询 `kStationQuery = 1`

用途：客户端拉取电站列表（含站名、空闲桩数、当前价格）；可指定电站 ID 只查单站。

#### 请求

```json
{
  "station_id": 0,
  "lat": 41.7152,
  "lng": 123.4958,
  "radius_km": 10.0,
  "limit": 50
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `station_id` | int | 否 | `>0` 只返回该站；`0` / 缺省返回全部电站 |
| `lat` / `lng` | number | 否 | 预留：后续按距离排序 / 圈选；当前版本不参与过滤 |
| `radius_km` | number | 否 | 预留：查询半径；当前版本忽略 |
| `limit` | int | 否 | 最大返回条数，默认 100，范围 1~200 |

#### 响应

```json
{
  "code": 0,
  "message": "ok",
  "server_time": "2026-09-09 10:00:00",
  "total": 3,
  "stations": [
    {
      "id": 1,
      "name": "中关村软件园充电站",
      "address": "北京市海淀区东北旺西路8号",
      "longitude": 123.4958,
      "latitude": 41.7152,
      "total_piles": 8,
      "idle_piles": 5,
      "online_piles": 8,
      "online_rate": 100.0,
      "base_price": 1.20,
      "price": 1.20
    }
  ]
}
```

`stations[]` 每项字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `id` | int | 电站 ID |
| `name` | string | 站名 |
| `address` | string | 地址 |
| `longitude` / `latitude` | number | 经纬度（腾讯地图坐标） |
| `total_piles` | int | 总电桩数 |
| `idle_piles` | int | 空闲桩数（`piles.state = 0`） |
| `online_piles` | int | 在线桩数（非故障） |
| `online_rate` | number | 在线率 0~100 |
| `base_price` | number | 基础电价（元/度） |
| `price` | number | 当前执行价（元/度）：基础价 × 最新生效营销折扣；无生效策略即基础价 |

数据来源：服务器实时调用 `StationStore::listStations()`（内部聚合 stations / piles /
marketing_strategy 三表后回填总数、空闲数、在线率与价格），非缓存快照；
`station_id > 0` 且不存在时返回 `code=404`，数据服务未打开时返回 `code=503`。

### 3.3 充电订单上报 `kOrderReport = 2`

用途：桩端 / 客户端在充电结束结算后向服务器上报一次订单结果。

#### 请求

```json
{
  "order_no": "NO20260909120001",
  "pile_code": "S01-P01",
  "kwh": 12.50,
  "amount": 24.60,
  "ts": 1750000000
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `order_no` | string | 是 | 上报方订单号（去首尾空格后非空） |
| `pile_code` | string | 是 | 电桩编码（`piles.code`，全局唯一） |
| `kwh` | number | 是 | 充电量（kWh），`0 ≤ kwh ≤ 1,000,000` |
| `amount` | number | 是 | 结算金额（元），`0 ≤ amount ≤ 1,000,000,000` |
| `ts` | number | 否 | 上报时刻（Unix 秒时间戳） |

#### 服务器处理规则

1. 校验请求为 JSON 对象且必填字段齐全、数值在合法范围；否则回 `code=400`。
2. 按 `pile_code` 查桩（`StationStore::findPileByCode`）；不存在回 `code=404`。
3. 桩处于「充电中」时，视为本次充电结束：置回「闲置」并刷新所属站在线率，
   响应 `pile_freed=true`；桩已闲置时幂等成功（`pile_freed=false`）。
4. 桩处于「故障」时不执行状态变更，回 `code=409`（需先运维修复 / 远程重启）。
5. 成功后记录服务器运行日志，作为后续订单落库 / 结算链路的入口。

> 说明：当前 `orders` 表由服务器侧建单流程维护（订单号、用户、电站与桩绑定）；
> 本消息负责设备侧结束结算结果的校验与电桩释放，属于该链路的收口，不在此处直接写订单表。

#### 响应

```json
{
  "code": 0,
  "message": "ok",
  "server_time": "2026-09-09 10:00:00",
  "order_no": "NO20260909120001",
  "pile_code": "S01-P01",
  "pile_id": 1,
  "station_id": 1,
  "kwh": 12.5,
  "amount": 24.6,
  "received": true,
  "pile_freed": true
}
```

字段说明：

| 字段 | 类型 | 说明 |
|---|---|---|
| `order_no` / `pile_code` | string | 回显上报内容 |
| `pile_id` / `station_id` | int | 命中的电桩 / 所属电站 ID |
| `kwh` / `amount` | number | 回显上报数值 |
| `received` | bool | 服务器是否受理（`false` 表示校验或状态未通过） |
| `pile_freed` | bool | 本次是否把「充电中」桩释放为闲置；已闲置为 `false` |

## 4. 服务器接入说明

- `MainWindow` 初始化时创建 `cp::NetServer`，以 `cp::kServerPort`（9999）开始监听；
  析构 / 退出时 `stopServer()` 并释放连接资源，全程 Qt 信号槽 + 事件循环，无裸线程 / 锁。
- `cp::NetServer::packetReceived` → `MainWindow::handleSocketPacket`
  → 按 MsgType 分发到心跳 / 电站查询 / 订单上报处理函数。
- 电站与桩数据统一经 `StationStore` 读取与更新；业务应答使用 `sendPacket`
  写回对应客户端套接字。

## 5. 验证方式

```bash
# 1) 底层帧协议与 NetClient/NetServer 传输联调（仓库已有）
cd /tmp/net-socket-test
qmake6 <仓库>/src/common/tests/net_socket_tests.pro && make && ./net_socket_tests

# 2) PcServer 业务层协议测试（本协议文档配套）
cd /tmp/socketbiz-test
qmake6 <仓库>/src/pcserver/tests/tst_socketbiz.pro && make
QT_QPA_PLATFORM=offscreen ./tst_socketbiz
```

---

## 6. 消息类型补充（v1.1，2026-09-11）

> 本次补充把"用户端本地模拟"改为"服务器真实建单"，并新增充值消息。
> 所有应答仍使用与请求相同的 MsgType（帧头 type 字段），Body 为 UTF-8 JSON。

### 6.1 开始充电 `kStartCharge = 4`（用户端 → 服务器）

请求：

```json
{ "phone": "13800138001", "pile_code": "S01-P02", "ts": 1750000000 }
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `phone` | string | 是 | 用户手机号（必须先登录/自动注册） |
| `pile_code` | string | 是 | 电桩编码（`piles.code`，如 `S01-P02`） |
| `ts` | number | 否 | 客户端时刻（Unix 秒） |

服务器处理（单事务）：

1. 校验用户存在且 `users.status = 0`（冻结账号返回 `403`）；
2. 校验电桩存在（否则 `404`）、非故障（否则 `409`）、非充电中（否则 `409`）；
3. 校验该桩没有未完成订单（否则 `409`，防止重复建单）；
4. 取本次执行价 = `stations.base_price × marketing_strategy.discount`（创新点① 直接作用于结算单价）；
5. `INSERT orders(state=0, start_time=now)` → `UPDATE piles SET state=1` → 刷新电站在线率。

响应：

```json
{ "code": 0, "message": "充电已开始", "order_id": 129,
  "pile_code": "S01-P02", "station_id": 1, "unit_price": 0.96,
  "start_time": "2026-09-11 23:23:52" }
```

### 6.2 订单结算金额改由服务器计算（`kOrderReport = 2` 行为修订）

客户端上报的 `amount` 仅作对账参考，**结算金额以服务器口径为准**：

`amount = round(kwh × stations.base_price × 生效折扣, 2)`

响应新增字段：

| 字段 | 说明 |
|---|---|
| `amount` | 服务器按折后执行价计算的**实际结算金额**（元） |
| `amount_reported` | 客户端上报金额（对账用） |
| `unit_price` | 本次执行价（元/度，含折扣） |
| `discount` | 生效折扣（1.0 = 无折扣） |
| `on_sale` | 是否处于闲时特惠 |

余额不足时返回 `409` 与明确中文原因（`余额不足：当前 ¥x，本次需 ¥y，请先充值`），
不再依赖 `balance >= 0` 约束抛出底层错误。

### 6.3 余额充值 `kRechargeRequest = 30`（用户端 → 服务器）

请求：`{ "phone": "13800138001", "amount": 100.0 }`

服务器在同一事务内累加 `users.balance` 并回读最新余额；金额非法返回 `422`。

响应：

```json
{ "code": 0, "message": "充值成功", "phone": "13800138001",
  "amount": 100.0, "balance": 200.0 }
```

### 6.4 站内电桩明细（`kStationQuery = 1` 扩展）

当请求带 `station_id > 0` 时，响应除 `stations[]` 外额外返回 `piles[]`，
字段为数据库真实值：`id / station_id / code / type / power_kw / state / state_text / charge_count / charge_seconds`。
用户端「电站详情」直接渲染该列表（取代此前按数量本地合成的桩列表）。
